#include "newui/automation.h"

#include "newui/controls.h"
#include "newui/menus.h"
#include "newui/reflection.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/segmentedcontrol.h"
#include "newui/subview.h"
#include "newui/utils.h"

#include <json5/json5.hpp>
#include <json5/json5_input.hpp>

#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <future>
#include <mutex>
#include <any>
#include <thread>
#include <typeindex>
#include <typeinfo>

// Protocol v1: one JSON object per line in, one per line out.
//   {"id":1,"cmd":"hello"}                        -> {"id":1,"ok":true,"protocol":1}
//   {"cmd":"findView","name":"filterEdit"}        -> {"ok":true,"x":..,"y":..,"w":..,"h":..,"visible":true}
//       x/y/w/h are screen pixels of the view's whole bounds, ready for SendInput.
//   {"cmd":"focused"}                             -> {"ok":true,"name":"..","type":".."}
//   {"cmd":"getText","name":"editor"}             -> {"ok":true,"text":".."}   (TextControl or TextField)
//   {"cmd":"scrollInfo","name":"tree scroll"}     -> v/h + Value,Min,Max,Page,x,y,w,h,Visible (bar state and thumb screen rect)
//   {"cmd":"menuBar","name":"bar"}              -> menus:[{text,x,y,w,h}] (a MenuBar's top-level buttons)
//   {"cmd":"popupMenu"}                         -> items:[{text,separator,enabled,checked,hasSubmenu,x,y,w,h}] of the native menu open now
//   {"cmd":"caret","name":"editor"}              -> line, column (1-based), offset (a TextControl's caret)
//   {"cmd":"describe","name":"v"}                -> type, properties:{name:value,..} (every reflected property with a plain value)
//   {"cmd":"get","name":"v","property":"text"}  -> value (one reflected property)
//   {"cmd":"tree","name":"tree","limit":50}      -> count (visible rows), rows:[{depth,text,expanded,hasChildren,x,y,w,h}] (a TreeView)
//   {"cmd":"segment","name":"modes","index":1}   -> count, selected, label, x/y/w/h of that segment (a SegmentedControl)
//   {"cmd":"screenshot","name":"root","path":"C:\\x.bmp"}  -> {"ok":true,"path":".."}  (name = a RootView)
//   {"cmd":"idle"}                                -> replies once the loop has nothing pending
// Failure: {"id":..,"ok":false,"error":".."}. "id" is echoed when given.

namespace newui {

namespace {

constexpr const char* kPipePrefix = "\\\\.\\pipe\\";

struct Request {
    json5::document doc;
    bool valid = false;

    explicit Request(const std::string& text) {
        valid = !json5::from_string(text, doc) && doc.is_object();
    }

    const json5::value* find(const char* key) const {
        for (const auto& [k, v] : json5::object_view(doc)) {
            if (std::string(k) == key) {
                lastValue_ = v;
                return &lastValue_;
            }
        }
        return nullptr;
    }

    std::string text(const char* key) const {
        const json5::value* value = find(key);
        return value != nullptr && value->is_string() ? value->get_c_str() : std::string();
    }

    bool hasNumber(const char* key) const {
        const json5::value* value = find(key);
        return value != nullptr && value->is_number();
    }

    double number(const char* key) const {
        const json5::value* value = find(key);
        return value != nullptr ? value->get<double>() : 0.0;
    }

private:
    mutable json5::value lastValue_;
};

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

std::string number(double value) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

std::string reply(const Request& request, bool ok, const std::string& fields) {
    std::string out = "{";
    if (request.hasNumber("id")) {
        out += "\"id\":" + std::to_string(static_cast<long long>(request.number("id"))) + ",";
    }
    out += ok ? "\"ok\":true" : "\"ok\":false";
    if (!fields.empty()) {
        out += "," + fields;
    }
    return out + "}";
}

std::string failure(const Request& request, const std::string& message) {
    return reply(request, false, "\"error\":" + quote(message));
}

// Only windows the app exposed are visible to a driver.
std::mutex exposedMutex;
std::vector<RootView*> exposed;

std::vector<RootView*> exposedRoots() {
    std::lock_guard<std::mutex> lock(exposedMutex);
    return exposed;
}

void forget(RootView* root) {
    std::lock_guard<std::mutex> lock(exposedMutex);
    exposed.erase(std::remove(exposed.begin(), exposed.end(), root), exposed.end());
}

SyncReturn rootDestroyed(View& sender) {
    forget(static_cast<RootView*>(&sender));
    return SyncReturn::Ignored;
}

View* findNamed(const std::string& name) {
    for (RootView* root : exposedRoots()) {
        if (root->name() == name) {
            return root;
        }
        if (View* view = root->findView(name)) {
            return view;
        }
    }
    return nullptr;
}

std::string typeName(const View& view) {
    std::string name = typeid(view).name();   // "class newui::TextControl"
    size_t colons = name.rfind("::");
    size_t space = name.rfind(' ');
    size_t start = colons != std::string::npos ? colons + 2 : (space != std::string::npos ? space + 1 : 0);
    return name.substr(start);
}

std::string findViewReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    if (view == nullptr) {
        return failure(request, "no view named '" + request.text("name") + "'");
    }
    Rect bounds = view->screenBounds();
    return reply(request, true,
        "\"x\":" + number(bounds.left()) + ",\"y\":" + number(bounds.top()) +
        ",\"w\":" + number(bounds.width()) + ",\"h\":" + number(bounds.height()) +
        ",\"visible\":" + (view->isVisible() ? "true" : "false"));
}

std::string focusedReply(const Request& request) {
    // The window the user is in wins; otherwise any window with a focused view.
    SubView* found = nullptr;
    HWND foreground = ::GetForegroundWindow();
    for (RootView* root : exposedRoots()) {
        SubView* focused = root->focusedSubView();
        if (focused == nullptr) {
            continue;
        }
        HWND window = root->windowHandle();
        bool inForeground = window != nullptr && (window == foreground || ::GetAncestor(window, GA_ROOT) == foreground);
        if (found == nullptr || inForeground) {
            found = focused;
        }
        if (inForeground) {
            break;
        }
    }
    if (found == nullptr) {
        return reply(request, true, "\"name\":\"\",\"type\":\"\"");
    }
    return reply(request, true, "\"name\":" + quote(found->name()) + ",\"type\":" + quote(typeName(*found)));
}

std::string getTextReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    if (view == nullptr) {
        return failure(request, "no view named '" + request.text("name") + "'");
    }
    if (auto* edit = dynamic_cast<TextControl*>(view)) {
        return reply(request, true, "\"text\":" + quote(wideToUtf8(edit->text())));
    }
    if (auto* field = dynamic_cast<TextField*>(view)) {
        return reply(request, true, "\"text\":" + quote(wideToUtf8(field->text())));
    }
    return failure(request, "'" + request.text("name") + "' (" + typeName(*view) + ") has no text to read");
}

// A ScrollView's bars as flat fields: <prefix>Value/Min/Max/Page and the thumb's screen rect.
std::string barFields(const char* prefix, const ScrollBar& bar) {
    Rect thumb = bar.localToScreen(bar.thumbBounds());
    std::string p = prefix;
    return "\"" + p + "Value\":" + number(bar.value()) + ",\"" + p + "Min\":" + number(bar.minValue()) +
        ",\"" + p + "Max\":" + number(bar.maxValue()) + ",\"" + p + "Page\":" + number(bar.pageSize()) +
        ",\"" + p + "x\":" + number(thumb.left()) + ",\"" + p + "y\":" + number(thumb.top()) +
        ",\"" + p + "w\":" + number(thumb.width()) + ",\"" + p + "h\":" + number(thumb.height()) +
        ",\"" + p + "Visible\":" + (bar.isVisible() ? "true" : "false");
}

std::string scrollInfoReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    auto* scroll = dynamic_cast<ScrollView*>(view);
    if (scroll == nullptr) {
        return failure(request, "'" + request.text("name") + "' is not a ScrollView");
    }
    std::string fields;
    if (scroll->vBar() != nullptr) {
        fields += barFields("v", *scroll->vBar());
    }
    if (scroll->hBar() != nullptr) {
        fields += (fields.empty() ? "" : ",") + barFields("h", *scroll->hBar());
    }
    return reply(request, true, fields);
}

// A reflected property value as a JSON literal; empty when its type has no plain form (collections,
// objects, enums), which callers skip.
std::string valueToJson(const std::any& value) {
    if (const bool* v = std::any_cast<bool>(&value)) return *v ? "true" : "false";
    if (const int* v = std::any_cast<int>(&value)) return std::to_string(*v);
    if (const unsigned* v = std::any_cast<unsigned>(&value)) return std::to_string(*v);
    if (const long long* v = std::any_cast<long long>(&value)) return std::to_string(*v);
    if (const unsigned long long* v = std::any_cast<unsigned long long>(&value)) return std::to_string(*v);
    if (const std::size_t* v = std::any_cast<std::size_t>(&value)) return std::to_string(*v);
    if (const float* v = std::any_cast<float>(&value)) return number(*v);
    if (const double* v = std::any_cast<double>(&value)) return number(*v);
    if (const std::string* v = std::any_cast<std::string>(&value)) return quote(*v);
    if (const std::wstring* v = std::any_cast<std::wstring>(&value)) return quote(wideToUtf8(*v));
    return std::string();
}

// The reflected class of a view's real (most derived) type, and the pointer its properties read from.
const reflection::Class* classOf(View& view, void*& instance) {
    instance = dynamic_cast<void*>(&view);
    return reflection::classinfo(std::type_index(typeid(view)));
}

// Every reflected property of a view that has a plain value, and its class name. Reflection is
// whatever newui registers for the class, so this needs no per-control code.
std::string describeReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    if (view == nullptr) {
        return failure(request, "no view named '" + request.text("name") + "'");
    }
    void* instance = nullptr;
    const reflection::Class* clazz = classOf(*view, instance);
    if (clazz == nullptr) {
        return failure(request, typeName(*view) + " has no reflection info");
    }
    std::vector<const reflection::Property*> properties;
    clazz->allProperties(properties);
    std::string fields;
    for (const reflection::Property* property : properties) {
        try {
            std::string json = valueToJson(property->get(instance));
            if (!json.empty()) {
                fields += (fields.empty() ? "" : ",") + quote(property->name()) + ":" + json;
            }
        } catch (const std::exception&) {
            // a property that can't be read just isn't listed
        }
    }
    return reply(request, true, "\"type\":" + quote(typeName(*view)) + ",\"properties\":{" + fields + "}");
}

// One reflected property by name: {"cmd":"get","name":"filterInput","property":"text"}
std::string getReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    if (view == nullptr) {
        return failure(request, "no view named '" + request.text("name") + "'");
    }
    void* instance = nullptr;
    const reflection::Class* clazz = classOf(*view, instance);
    if (clazz == nullptr) {
        return failure(request, typeName(*view) + " has no reflection info");
    }
    std::vector<const reflection::Property*> properties;
    clazz->allProperties(properties);
    for (const reflection::Property* property : properties) {
        if (property->name() != request.text("property")) {
            continue;
        }
        std::string json;
        try {
            json = valueToJson(property->get(instance));
        } catch (const std::exception& error) {
            return failure(request, std::string("reading it failed: ") + error.what());
        }
        if (json.empty()) {
            return failure(request, "'" + property->name() + "' has no plain value to report");
        }
        return reply(request, true, "\"value\":" + json);
    }
    return failure(request, typeName(*view) + " has no reflected property '" + request.text("property") + "'");
}

// A TreeView's visible rows, top to bottom: total count and the first `limit` rows' depth and text
// (the model's value() for the row's path, when it answers with a string).
std::string treeReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    auto* tree = dynamic_cast<TreeView*>(view);
    if (tree == nullptr) {
        return failure(request, "'" + request.text("name") + "' is not a TreeView");
    }
    TreeController& controller = tree->controller();
    TreeModel* model = controller.model();
    size_t count = controller.visibleCount();
    size_t limit = request.hasNumber("limit") ? static_cast<size_t>(request.number("limit")) : 50;
    std::string rows;
    for (size_t i = 0; i < count && i < limit; ++i) {
        const std::vector<std::size_t>& path = controller.pathAt(i);
        std::string text;
        if (model != nullptr) {
            std::any value = model->value(std::any(path));
            if (const std::string* shown = std::any_cast<std::string>(&value)) {
                text = *shown;
            }
        }
        Rect rowBox = tree->localToScreen(Rect(0, controller.itemOffset(i), tree->screenBounds().width(), controller.itemHeight(i)));
        rows += (rows.empty() ? "" : ",") + std::string("{\"depth\":") + std::to_string(path.size()) + ",\"text\":" + quote(text) +
            ",\"expanded\":" + (controller.isExpanded(path) ? "true" : "false") +
            ",\"hasChildren\":" + (model != nullptr && model->hasChildren(path) ? "true" : "false") +
            ",\"x\":" + number(rowBox.left()) + ",\"y\":" + number(rowBox.top()) +
            ",\"w\":" + number(rowBox.width()) + ",\"h\":" + number(rowBox.height()) + "}";
    }
    return reply(request, true, "\"count\":" + std::to_string(count) + ",\"rows\":[" + rows + "]");
}

// A MenuBar's top-level menus, left to right: the text and screen rect of each button.
std::string menuBarReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    auto* bar = dynamic_cast<MenuBar*>(view);
    if (bar == nullptr) {
        return failure(request, "'" + request.text("name") + "' is not a MenuBar");
    }
    const std::vector<MenuItem*>& menus = bar->menus();
    const std::vector<SubView*>& buttons = bar->childViews();
    std::string out;
    for (size_t i = 0; i < menus.size() && i < buttons.size(); ++i) {
        Rect box = buttons[i]->screenBounds();
        out += (out.empty() ? "" : ",") + std::string("{\"text\":") + quote(stripMnemonics(menus[i]->text())) +
            ",\"x\":" + number(box.left()) + ",\"y\":" + number(box.top()) +
            ",\"w\":" + number(box.width()) + ",\"h\":" + number(box.height()) + "}";
    }
    return reply(request, true, "\"menus\":[" + out + "]");
}

HWND openPopupMenuWindow() {
    struct Search { HWND found = nullptr; } search;
    ::EnumThreadWindows(::GetCurrentThreadId(), [](HWND window, LPARAM param) -> BOOL {
        char className[16] = {};
        ::GetClassNameA(window, className, sizeof(className));
        if (std::string(className) == "#32768" && ::IsWindowVisible(window)) {
            reinterpret_cast<Search*>(param)->found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// The native popup menu that is open right now (a menu bar button's dropdown or a context menu): each item's
// label (mnemonic marker and shortcut text removed), state and screen rect. Native menus run their own modal
// loop, so this is how a driver finds what to click.
std::string popupMenuReply(const Request& request) {
    HWND window = openPopupMenuWindow();
    if (window == nullptr) {
        return failure(request, "no menu is open");
    }
    constexpr UINT kGetMenuHandle = 0x01E1;   // MN_GETHMENU
    HMENU menu = reinterpret_cast<HMENU>(::SendMessageW(window, kGetMenuHandle, 0, 0));
    if (menu == nullptr) {
        return failure(request, "the open menu has no handle");
    }
    std::string items;
    const int count = ::GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        wchar_t text[256] = {};
        MENUITEMINFOW info = { sizeof(info) };
        info.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_STATE | MIIM_SUBMENU;
        info.dwTypeData = text;
        info.cch = 255;
        ::GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info);
        RECT box = {};
        ::GetMenuItemRect(nullptr, menu, static_cast<UINT>(i), &box);
        std::string label = stripMnemonics(wideToUtf8(text));
        size_t tab = label.find('\t');
        if (tab != std::string::npos) {
            label.resize(tab);
        }
        const bool separator = (info.fType & MFT_SEPARATOR) != 0;
        const bool enabled = (info.fState & (MFS_DISABLED | MFS_GRAYED)) == 0;
        items += (items.empty() ? "" : ",") + std::string("{\"text\":") + quote(label) +
            ",\"separator\":" + (separator ? "true" : "false") + ",\"enabled\":" + (enabled ? "true" : "false") +
            ",\"checked\":" + ((info.fState & MFS_CHECKED) != 0 ? "true" : "false") +
            ",\"hasSubmenu\":" + (info.hSubMenu != nullptr ? "true" : "false") +
            ",\"x\":" + std::to_string(box.left) + ",\"y\":" + std::to_string(box.top) +
            ",\"w\":" + std::to_string(box.right - box.left) + ",\"h\":" + std::to_string(box.bottom - box.top) + "}";
    }
    return reply(request, true, "\"items\":[" + items + "]");
}

// A TextControl's caret: 1-based line and column, and the offset it is at.
std::string caretReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    auto* edit = dynamic_cast<TextControl*>(view);
    if (edit == nullptr) {
        return failure(request, "'" + request.text("name") + "' is not a TextControl");
    }
    const size_t offset = edit->caret().position().offset();
    const size_t line = edit->model().storage().lineOfOffset(offset);
    const size_t column = offset - edit->model().storage().lineStart(line);
    return reply(request, true, "\"line\":" + std::to_string(line + 1) + ",\"column\":" + std::to_string(column + 1) +
                                ",\"offset\":" + std::to_string(offset));
}

// One segment of a SegmentedControl: the control's count/selected plus the segment's label and screen rect.
std::string segmentReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    auto* control = dynamic_cast<SegmentedControl*>(view);
    if (control == nullptr) {
        return failure(request, "'" + request.text("name") + "' is not a SegmentedControl");
    }
    size_t index = static_cast<size_t>(request.number("index"));
    if (index >= control->segments().size()) {
        return failure(request, "segment " + std::to_string(index) + " is out of range");
    }
    Rect bounds = control->localToScreen(control->segmentBounds(index));
    return reply(request, true,
        "\"count\":" + std::to_string(control->segments().size()) +
        ",\"selected\":" + std::to_string(control->selectedIndex()) +
        ",\"label\":" + quote(control->segments()[index]) +
        ",\"x\":" + number(bounds.left()) + ",\"y\":" + number(bounds.top()) +
        ",\"w\":" + number(bounds.width()) + ",\"h\":" + number(bounds.height()));
}

std::string screenshotReply(const Request& request) {
    View* view = findNamed(request.text("name"));
    auto* root = dynamic_cast<RootView*>(view);
    if (root == nullptr) {
        return failure(request, "'" + request.text("name") + "' is not a RootView");
    }
    std::string path = request.text("path");
    if (path.empty()) {
        return failure(request, "screenshot needs a path");
    }
    BLResult result = root->getImageBuffer().write_to_file(path.c_str());
    if (result != BL_SUCCESS) {
        return failure(request, "could not write " + path + " (Blend2D error " + std::to_string(result) + ")");
    }
    return reply(request, true, "\"path\":" + quote(path));
}

// A protected DACL granting full access to the current user's SID alone.
struct SecurityDescriptor {
    PSECURITY_DESCRIPTOR descriptor = nullptr;

    SecurityDescriptor() = default;
    SecurityDescriptor(const SecurityDescriptor&) = delete;
    SecurityDescriptor& operator=(const SecurityDescriptor&) = delete;
    ~SecurityDescriptor() {
        if (descriptor != nullptr) {
            ::LocalFree(descriptor);
        }
    }

    bool forCurrentUser() {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }
        DWORD size = 0;
        ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<char> buffer(size);
        bool ok = size != 0 && ::GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
        ::CloseHandle(token);
        if (!ok) {
            return false;
        }
        LPWSTR sid = nullptr;
        if (!::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) {
            return false;
        }
        std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
        ::LocalFree(sid);
        return ::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != FALSE;
    }
};

// Server state: one pipe instance, one client at a time (a test run).
std::atomic<bool> running{false};
std::atomic<HANDLE> pipeHandle{INVALID_HANDLE_VALUE};
std::thread serverThread;

bool readLine(HANDLE pipe, std::string& line) {
    line.clear();
    char c;
    DWORD got = 0;
    while (::ReadFile(pipe, &c, 1, &got, nullptr) && got == 1) {
        if (c == '\n') {
            return true;
        }
        if (c != '\r') {
            line += c;
        }
    }
    return false;
}

void writeLine(HANDLE pipe, const std::string& text) {
    std::string out = text + "\n";
    DWORD written = 0;
    ::WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
}

std::string runOnLoop(RunLoop& loop, const std::string& line) {
    auto promise = std::make_shared<std::promise<std::string>>();
    std::future<std::string> future = promise->get_future();
    Request request(line);
    if (request.valid && request.text("cmd") == "idle") {
        loop.postIdle([promise, line]() {
            promise->set_value(Automation::handle(line));
            return true;
        });
    } else {
        loop.post([promise, line]() { promise->set_value(Automation::handle(line)); });
    }
    return future.get();
}

}

std::string Automation::handle(const std::string& requestJson) {
    Request request(requestJson);
    if (!request.valid) {
        return "{\"ok\":false,\"error\":\"the request is not a JSON object\"}";
    }
    std::string cmd = request.text("cmd");
    if (cmd == "hello") {
        return reply(request, true, "\"protocol\":" + std::to_string(kProtocolVersion));
    }
    if (cmd == "findView") {
        return findViewReply(request);
    }
    if (cmd == "focused") {
        return focusedReply(request);
    }
    if (cmd == "getText") {
        return getTextReply(request);
    }
    if (cmd == "scrollInfo") {
        return scrollInfoReply(request);
    }
    if (cmd == "menuBar") {
        return menuBarReply(request);
    }
    if (cmd == "popupMenu") {
        return popupMenuReply(request);
    }
    if (cmd == "caret") {
        return caretReply(request);
    }
    if (cmd == "describe") {
        return describeReply(request);
    }
    if (cmd == "get") {
        return getReply(request);
    }
    if (cmd == "tree") {
        return treeReply(request);
    }
    if (cmd == "segment") {
        return segmentReply(request);
    }
    if (cmd == "screenshot") {
        return screenshotReply(request);
    }
    if (cmd == "idle") {
        return reply(request, true, "");
    }
    return failure(request, "unknown command '" + cmd + "'");
}

void Automation::expose(RootView& root) {
    {
        std::lock_guard<std::mutex> lock(exposedMutex);
        if (std::find(exposed.begin(), exposed.end(), &root) != exposed.end()) {
            return;
        }
        exposed.push_back(&root);
    }
    root.onDestroyed.add(&rootDestroyed);
}

void Automation::unexpose(RootView& root) {
    forget(&root);
}

bool Automation::start(const std::string& pipeName, RunLoop& loop) {
    if (running.exchange(true)) {
        return false;
    }
    std::wstring path = utf8ToWide(kPipePrefix + pipeName);
    // Only the account that runs the app may connect, and never over the network.
    SecurityDescriptor access;
    if (!access.forCurrentUser()) {
        running = false;
        return false;
    }
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), access.descriptor, FALSE };
    HANDLE pipe = ::CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &attributes);
    if (pipe == INVALID_HANDLE_VALUE) {
        running = false;
        return false;
    }
    pipeHandle = pipe;
    serverThread = std::thread([pipe, &loop]() {
        while (running) {
            bool connected = ::ConnectNamedPipe(pipe, nullptr) || ::GetLastError() == ERROR_PIPE_CONNECTED;
            if (!connected) {
                break;
            }
            std::string line;
            while (running && readLine(pipe, line)) {
                writeLine(pipe, runOnLoop(loop, line));
            }
            ::DisconnectNamedPipe(pipe);
        }
    });
    return true;
}

void Automation::stop() {
    if (!running.exchange(false)) {
        return;
    }
    HANDLE pipe = pipeHandle.exchange(INVALID_HANDLE_VALUE);
    if (pipe != INVALID_HANDLE_VALUE) {
        ::CancelIoEx(pipe, nullptr);
        ::DisconnectNamedPipe(pipe);
    }
    if (serverThread.joinable()) {
        serverThread.join();
    }
    if (pipe != INVALID_HANDLE_VALUE) {
        ::CloseHandle(pipe);
    }
}

}
