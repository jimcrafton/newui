#include "newui/bundle.h"
#include "newui/application.h"
#include "newui/dialogs.h"
#include "newui/frame.h"
#include "newui/reflectionio.h"
#include "newui/rootview.h"
#include "newui/view.h"

#include <json5/json5.hpp>
#include <json5/json5_input.hpp>

#include <fstream>
#include <sstream>
#include <vector>

namespace {

    // GetModuleFileNameA's buffer-too-small signal is "returned length ==
    // buffer size" (and, since Vista, ERROR_INSUFFICIENT_BUFFER) - grow
    // and retry rather than guessing a size upfront.
    std::string getExecutablePath() {
        std::vector<char> buffer(MAX_PATH);

        for (;;) {
            DWORD len = ::GetModuleFileNameA(nullptr, buffer.data(), DWORD(buffer.size()));
            if (len == 0) {
                return std::string();
            }
            if (len < buffer.size()) {
                return std::string(buffer.data(), len);
            }
            if (buffer.size() > 32768) {
                return std::string();  // pathological - bail rather than growing forever
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::string directoryOf(const std::string& path) {
        size_t pos = path.find_last_of("\\/");
        return pos == std::string::npos ? std::string() : path.substr(0, pos);
    }

    bool fileExists(const std::string& path) {
        DWORD attrs = ::GetFileAttributesA(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    // Writes clazz's own properties and public fields for instancePtr
    // into writer's currently-open object scope - the properties/fields
    // portion of what newui::reflection::TypedClass<T>::write()
    // (reflection.h) does, reusable here without a compile-time T (clazz
    // is resolved at runtime - see Bundle::writeFrame()/writeView(), both
    // of which need the instance's *real* runtime class, not whatever
    // static type the caller holds it as). Deliberately skips the
    // "delegates" block TypedClass<T>::write() also writes - a described,
    // reconnectable Delegate<> connection is a rare, code-driven feature
    // (see reflectionio.h's own comment on it), not something a saved UI
    // layout file normally carries; every real Bundle::write*() caller
    // today has nothing connected that way regardless, so there is
    // nothing to lose in practice, only unused complexity to skip.
    void writeInstanceBody(const newui::reflection::Class* clazz, void* instancePtr,
                             newui::reflection::ObjectWriter& writer) {
        std::vector<const newui::reflection::Property*> properties;
        clazz->allProperties(properties);
        for (const auto* property : properties) {
            property->write(instancePtr, &writer);
        }

        std::vector<const newui::reflection::Field*> fields;
        clazz->allFields(fields);
        for (const auto* field : fields) {
            if (field->scope() == newui::reflection::Scope::Public) {
                field->write(instancePtr, &writer);
            }
        }
    }

}

namespace newui {

    Bundle::Bundle() {
        executableDir_ = directoryOf(getExecutablePath());
        resourcesDir_ = executableDir_ + "\\Resources";
    }

    Bundle& Bundle::instance() {
        static Bundle bundle;
        return bundle;
    }

    std::string Bundle::resourcePath(const std::string& relativePath) const {
        std::string path = resourcesDir_ + "\\" + relativePath;
        return fileExists(path) ? path : std::string();
    }

    bool Bundle::loadImage(const std::string& relativePath, BLImage& outImage) const {
        std::string path = resourcePath(relativePath);
        if (path.empty()) {
            return false;
        }
        return outImage.read_from_file(path.c_str()) == BL_SUCCESS;
    }

    std::string Bundle::loadTextFile(const std::string& relativePath) const {
        std::string path = resourcePath(relativePath);
        if (path.empty()) {
            return std::string();
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return std::string();
        }

        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

    // Shared by loadFrame()/loadRootView()/loadView() below - loads
    // "<name>.newui" (resolved via loadTextFile(), so relativePath is
    // exactly a Bundle-relative name, no extension) and parses it into
    // outReader.doc. Returns false (outReader.doc left however
    // json5::from_string() leaves a failed parse) if the file doesn't
    // resolve or isn't valid JSON5.
    bool Bundle::parseNewuiFile(const std::string& name, reflection::ObjectReader& outReader) const {
        std::string text = loadTextFile(name + ".newui");
        if (text.empty()) {
            return false;
        }
        return !json5::from_string(text, outReader.doc);
    }

    bool Bundle::loadFrame(Frame& frame) const {
        if (frame.getName().empty()) {
            return false;
        }

        reflection::ObjectReader reader;
        if (!parseNewuiFile(frame.getName(), reader)) {
            return false;
        }

        // Frame has no registered "rootView" property - reflectgen refuses
        // to build one (RootView isn't copy-constructible; see reflectgen's
        // own MSVC is_copy_constructible_v note, reflectgen.py) - so
        // read()'s normal per-property walk would never reach the file's
        // own "rootView" node at all. frame.rootView() is always live
        // regardless (Frame owns it unconditionally, no reflection
        // involved in reaching it), so readNested() reads that node
        // straight into it directly - same file, same document root, just
        // bypassing the property link reflection can't provide here.
        reader.read(&frame);
        reader.readNested("rootView", &frame.rootView());
        return true;
    }

    bool Bundle::loadDialog(Dialog& dialog) const {
        return loadFrame(dialog.frame());
    }

    bool Bundle::loadRootView(RootView& rootView) const {
        Frame* frame = rootView.getFrame();
        if (frame == nullptr || frame->getName().empty()) {
            return false;
        }

        reflection::ObjectReader reader;
        if (!parseNewuiFile(frame->getName(), reader)) {
            return false;
        }

        reader.readNested("rootView", &rootView);
        return true;
    }

    View* Bundle::loadView(const std::string& name) const {
        if (name.empty()) {
            return nullptr;
        }

        reflection::ObjectReader reader;
        if (!parseNewuiFile(name, reader)) {
            return nullptr;
        }

        return reader.readNew<View>();
    }

    bool Bundle::writeTextFile(const std::string& relativePath, const std::string& contents) const {
        ::CreateDirectoryA(resourcesDir_.c_str(), nullptr);  // idempotent - already-exists is fine, checked next via the actual write

        std::ofstream file(resourcesDir_ + "\\" + relativePath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << contents;
        return bool(file);
    }

    bool Bundle::writeFrame(Frame& frame) const {
        if (frame.getName().empty()) {
            return false;
        }

        const reflection::Class* clazz = reflection::classinfo(typeid(frame));
        if (clazz == nullptr) {
            return false;
        }

        reflection::ObjectWriter writer;

        //writer.write(&frame);
        
        writer.beginObject(std::string(), clazz);
        writeInstanceBody(clazz, &frame, writer);
        // See loadFrame()'s own comment - the same missing "rootView"
        // property means the write side needs the same direct bridge.
        writer.writeNested("rootView", &frame.rootView());

        writer.endObject(std::string(), clazz);
        
        return writeTextFile(frame.getName() + ".newui", json5::to_string(writer.doc));
    }

    bool Bundle::writeDialog(Dialog& dialog) const {
        return writeFrame(dialog.frame());
    }

    bool Bundle::writeView(View& view, const std::string& name) const {
        if (name.empty()) {
            return false;
        }

        const reflection::Class* clazz = reflection::classinfo(typeid(view));
        if (clazz == nullptr) {
            return false;
        }

        reflection::ObjectWriter writer;
        clazz->write(&view, &writer, std::string());

        return writeTextFile(name + ".newui", json5::to_string(writer.doc));
    }

    void Bundle::ensureInfoLoaded() const {
        if (infoLoaded_) {
            return;
        }
        infoLoaded_ = true;

        json5::document doc;
        if (json5::from_file(executableDir_ + "\\Info.json", doc)) {
            return;  // missing or unparseable - appName_/appVersion_ stay ""
        }

        appName_ = doc["name"].get_c_str("");
        appVersion_ = doc["version"].get_c_str("");
    }

    const std::string& Bundle::appName() const {
        ensureInfoLoaded();
        if (!appName_.empty()) {
            return appName_;
        }
        // Application::getName() returns by value, and appName_ must stay
        // untouched (it's the "did Info.json provide one" signal) - so the
        // live fallback goes into its own scratch member instead, purely
        // to give this by-reference return something to point at that
        // outlives the call.
        resolvedAppName_ = Application::instance().getName();
        return resolvedAppName_;
    }

    const std::string& Bundle::appVersion() const {
        ensureInfoLoaded();
        return appVersion_;
    }

}
