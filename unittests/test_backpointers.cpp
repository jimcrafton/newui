// Non-owning pointers to views: each lets go by itself when its view is destroyed or leaves the tree.

#include "newui/automation.h"
#include "newui/controls.h"
#include "newui/dragndrop.h"
#include "newui/frame.h"
#include "newui/models.h"
#include "newui/mouse_constants.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/uiinputmanager.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using newui::Component;
using newui::Point;
using newui::Rect;
using newui::SubView;
using newui::SyncReturn;

class HeadlessRoot : public newui::RootView {
public:
    explicit HeadlessRoot(const std::string& name = "root") : newui::RootView(nullptr, Rect(0, 0, 200, 200), name) {}

    using newui::RootView::mouseDown;
    using newui::RootView::mouseMove;
};

SubView* makeView(const std::string& name) {
    auto* view = new SubView();
    view->setName(name);
    view->setVisible(true);
    view->setBounds(Rect(0, 0, 100, 100));
    return view;
}

void dispose(newui::View* view) {
    view->destroy();
    delete view;
}

class TestableDropDownList : public newui::DropDownList {
public:
    using newui::DropDownList::buttonRect;
};

class RowModel : public newui::ListModel {
public:
    std::size_t size() const override { return 3; }
    std::any value(const std::any&) override { return std::string("row"); }
};

// The popup window a Frame owns, found by its window class.
HWND popupOwnedBy(HWND owner) {
    struct Search {
        HWND owner;
        HWND found;
    } search{owner, nullptr};
    ::EnumThreadWindows(::GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        auto* wanted = reinterpret_cast<Search*>(data);
        char name[64] = {};
        ::GetClassNameA(window, name, sizeof(name));
        if (std::string(name) == "PopupFrame" && ::GetWindow(window, GW_OWNER) == wanted->owner) {
            wanted->found = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

}  // namespace

TEST(BackPointers, AModelForgetsAViewThatIsDeletedWithoutBeingDestroyed) {
    newui::Model model;
    SubView* view = makeView("view");
    model.addView(view);

    delete view;

    EXPECT_EQ(model.viewCount(), 0u);
    model.updateAllViews();

    SubView* next = makeView("next");
    model.addView(next);
    EXPECT_EQ(model.viewCount(), 1u);
    dispose(next);
}

TEST(BackPointers, AModelMayBeDestroyedBeforeItsViews) {
    SubView* view = makeView("view");
    {
        newui::Model model;
        model.addView(view);
    }
    dispose(view);
}

TEST(BackPointers, AModelDoesNotTakeAViewThatIsAlreadyDestroyed) {
    newui::Model model;
    SubView* view = makeView("view");
    view->destroy();

    model.addView(view);

    EXPECT_EQ(model.viewCount(), 0u);
    delete view;
}

TEST(BackPointers, AutomationForgetsARootDeletedWithoutBeingDestroyed) {
    auto* root = new HeadlessRoot("deletedRoot");
    newui::Automation::expose(*root);
    newui::Automation::expose(*root);
    ASSERT_NE(newui::Automation::handle("{\"cmd\":\"findView\",\"name\":\"deletedRoot\"}").find("\"ok\":true"), std::string::npos);

    delete root;

    EXPECT_NE(newui::Automation::handle("{\"cmd\":\"findView\",\"name\":\"deletedRoot\"}").find("\"ok\":false"), std::string::npos);
}

TEST(BackPointers, AFocusGuideLosesATargetThatIsDestroyed) {
    auto* guide = new newui::FocusGuide();
    SubView* target = makeView("target");
    guide->setRedirectTarget(target);
    ASSERT_EQ(guide->redirectTarget(), target);

    dispose(target);

    EXPECT_EQ(guide->redirectTarget(), nullptr);
    dispose(guide);
}

TEST(BackPointers, ADragLeavingAfterTheHoveredViewWasDestroyedDoesNotTouchIt) {
    auto* root = new HeadlessRoot();
    SubView* child = makeView("child");
    root->addChild(child);
    int over = 0;
    int left = 0;
    auto dropTarget = std::make_unique<newui::DropTarget>();
    dropTarget->onTextDropped.add([](newui::DropTarget&, const std::wstring&) { return SyncReturn::Handled; });
    dropTarget->onTextDragOver.add([&over](newui::DropTarget&, const std::wstring&, const Point&, newui::DropEffect&) {
        ++over;
        return SyncReturn::Handled;
    });
    dropTarget->onDragLeave.add([&left](newui::DropTarget&) {
        ++left;
        return SyncReturn::Handled;
    });
    child->setDropTarget(std::move(dropTarget));
    auto dataObject = Microsoft::WRL::Make<newui::TextDataObject>(L"payload");
    auto comDropTarget = Microsoft::WRL::Make<newui::COMDropTarget>(*root);
    DWORD effect = DROPEFFECT_COPY;
    comDropTarget->dragEnterAt(dataObject.Get(), Point(50, 50), &effect);
    ASSERT_EQ(over, 1);

    root->removeChild(child);
    dispose(child);
    comDropTarget->DragLeave();

    EXPECT_EQ(left, 0);
    dispose(root);
}

TEST(BackPointers, AViewRemovedBetweenPressAndDragStartsNoDrag) {
    auto* root = new HeadlessRoot();
    SubView* child = makeView("child");
    root->addChild(child);
    int asked = 0;
    auto dragSource = std::make_unique<newui::DropSource>();
    dragSource->onProvideText.add([&asked](newui::DropSource&, std::wstring&) {
        ++asked;
        return SyncReturn::Ignored;
    });
    child->setDragSource(std::move(dragSource));
    root->mouseDown(Point(10, 10), newui::mbmLeftButton, 0);

    root->removeChild(child);
    root->mouseMove(Point(60, 60), newui::mbmLeftButton, 0);

    EXPECT_EQ(asked, 0);
    dispose(child);
    dispose(root);
}

TEST(BackPointers, ADropDownListOpensAgainAfterItsPopupWindowWasDestroyed) {
    newui::Frame frame;
    ASSERT_TRUE(frame.initialize());
    ::ShowWindow(frame.frameHandle(), SW_HIDE);
    auto* list = new TestableDropDownList();
    list->setVisible(true);
    list->setModel(std::make_unique<RowModel>());
    frame.rootView().addChild(list);
    list->setBounds(Rect(0, 0, 120, 24));
    const Rect button = list->buttonRect();
    const Point onButton(button.left() + 2.0f, button.top() + 2.0f);

    list->onMouseDown(*list, onButton, newui::mbmLeftButton, 0);
    ASSERT_TRUE(list->isOpen());
    const HWND first = popupOwnedBy(frame.frameHandle());
    ASSERT_NE(first, nullptr);
    ::DestroyWindow(first);
    ASSERT_FALSE(list->isOpen());

    list->setSelectedIndex(1u);
    list->onMouseDown(*list, onButton, newui::mbmLeftButton, 0);

    EXPECT_TRUE(list->isOpen());
    ::DestroyWindow(frame.frameHandle());
    MSG message;
    while (::PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message != WM_QUIT) {
            ::DispatchMessage(&message);
        }
    }
}
