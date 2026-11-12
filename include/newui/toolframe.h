#pragma once

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/frame.h>

namespace newui {

    // A floating tool window: a persistent top-level window with a thin caption and a sizing border (so Windows moves and resizes it), owned by
    // another window (always stacked above it, no taskbar button) and not activating by default - clicking it leaves the owner active. Like PopupFrame
    // it is a Frame that hosts a RootView (add content through rootView()), but it is not layered, not topmost, has no outside-click dismissal,
    // and the close button hides it rather than destroying it, so its content keeps its state between shows.
    // @reflect ignore=true
    class ToolFrame : public Frame {
    public:
        ToolFrame();
        ~ToolFrame() override;

        // owner is required: it keeps this window above the window that spawned it and closes it with that window.
        bool initialize(HWND owner);

        // Moves and sizes the real window; screenBounds is in screen coordinates.
        void moveTo(const Rect& screenBounds);
        void show();
        void hide();
        bool isVisible() const;

        // Off by default (a click leaves the owner active). A window with a text field turns it on, so a click activates it and the field takes
        // keystrokes; the window is still never activated by show(). Can be changed while the window is up.
        void setActivatesOnClick(bool activates);
        bool activatesOnClick() const { return activatesOnClick_; }

        // The smallest size (outer window size) the user can drag it to.
        void setMinimumSize(const Size& size) { minimumSize_ = size; }

        // Fired when the user closes it from the caption (the window is hidden first).
        typedef Delegate<ToolFrame> ClosedDelegate;
        ClosedDelegate onClosed;

    protected:
        bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT) override;

    private:
        HWND owner_ = nullptr;
        bool activatesOnClick_ = false;
        Size minimumSize_ = Size(200.0f, 120.0f);
    };

}
