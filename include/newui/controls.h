#pragma once

#include <cstdint>

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/subview.h>

#include <blend2d/blend2d.h>

namespace newui {

    // The "basic Control" half of this toolkit's Control/Controller split
    // (see controllers.h's class comment): a SubView that represents
    // something the user interacts with (a button) or that displays live
    // status (a progress bar), and that may itself host other SubViews -
    // but that doesn't own or need any real data behind it. A Control
    // that *does* need real data (a future ListView/TreeView) is expected
    // to own a Controller as a member, not become one - see controllers.h.
    //
    // Not meant to be instantiated directly (constructor is protected) -
    // a subclassing point, like UIKit's UIControl, for Button/Progress/
    // EditControl/TextControl below and future controls.
    class Control : public SubView {
    public:
        enum StateFlags {
            Disabled    = 0x0001,
            HighLighted = 0x0002,
            Selected    = 0x0004,
            Focused     = 0x0008,
        };

        class State {
            public:
                void setEnabled(bool v) { setDisabled(!v); }

                void setDisabled(bool v) {
                    state_ = v ? state_ | StateFlags::Disabled : state_ & ~StateFlags::Disabled;
                }

                bool isEnabled() const { return (state_ & StateFlags::Disabled) == 0; }

                operator std::uint32_t () const { return state_; }
                State& operator=(const std::uint32_t& rhs) { state_ = rhs; return *this; }
            private:
            std::uint32_t   state_ = 0;
        };

        typedef Delegate<Control> StateChangedDelegate;

        // Fired on a "press, then release while still over this Control's
        // bounds" gesture - this toolkit's equivalent of UIControl's
        // target-action .touchUpInside, built on top of View's own raw
        // onMouseDown/onMouseUp (see Control::Control()) rather than a
        // separate dispatch mechanism, since Delegate already covers what
        // UIControl's addTarget:action:for: does (and more - multicast,
        // lambdas, member functions). Does not fire while disabled() -
        // see setEnabled().
        typedef Delegate<Control> ClickDelegate;

        virtual ~Control() {}

        StateChangedDelegate onStateChanged;
        ClickDelegate onClick;

        void setEnabled(bool v) {
            state_.setEnabled(v);
            if (!v) {
                // A press already in progress when this Control becomes
                // disabled shouldn't still produce a click on release -
                // matches UIControl, which never sends actions while
                // isEnabled is false.
                tracking_ = false;
            }
            onStateChanged(*this);
        }

        bool isEnabled() const { return state_.isEnabled(); }

    protected:
        Control();

    private:
        State state_;
        bool tracking_ = false;

        SyncReturn handleTrackingMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleTrackingMouseUp(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
    };




    class Button : public Control {
    public:
        virtual ~Button() {}
    };

    class Progress : public Control {
    public:
        virtual ~Progress() {}
    };



    class TextCaret {

    };

    class TextSelection {
    public:
        static const size_t Invalid = (size_t)-1;

        class Range {
            public:

            size_t rowStart = Invalid;
            size_t colStart = Invalid;

            size_t rowEnd = Invalid;
            size_t colEnd = Invalid;
        };

        size_t start = Invalid;
        size_t end = Invalid;

        Range range;

        void draw(BLContext& ctx);
    };


    //single line text editing control
    class EditControl : public Control {

    };

    //multiline line text editing control
    class TextControl : public Control {

    };
}
