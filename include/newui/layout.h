#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <newui/uicomponent.h>

namespace newui {

    class View;
    class SubView;

    // Per-child metadata a Layout needs beyond what SubView itself already
    // tracks (bounds, visibility, name, ...) - e.g. which edges
    // AnchorLayout pins, or how much of a StackLayout's main axis a child
    // should claim. Each Layout subclass that needs per-child data defines
    // its own subclass of this (AnchorLayoutParams, StackLayoutParams,
    // ...) - see SubView::setLayoutParams(). A child with no LayoutParams
    // set (or one of the wrong kind for whichever Layout is attached to
    // its parent) gets that Layout's own default behavior for the unset
    // case - see each Layout subclass's arrange() for what that is.
    class LayoutParams : public UIComponent {
    public:
        virtual ~LayoutParams() = default;

        // No fields of its own - trivial no-op base, overridden by
        // AnchorLayoutParams/StackLayoutParams for their own fields.
        void writeFields(json5::builder& w) const override {}
        void readFields(const json5::value& obj) override {}
    };

    // Arranges a View's direct children (its childViews() - always
    // SubView*) within that View's own bounds. Attach one via
    // View::setLayout() - see View::updateLayout(), which calls arrange()
    // automatically whenever the View's own size changes
    // (SubView::setBounds()/RootView::setBounds()) or its child list
    // changes (addChild()/removeChild()), so normal use never needs to
    // call arrange() directly.
    //
    // Deliberately arranges into container's full bounds size, (0,0)
    // being the origin, rather than getClientBounds() - clientBounds()
    // is only populated by paintStyle() during an actual paint pass, so
    // using it here would make layout's result depend on whether a paint
    // has happened yet. A style that paints chrome (a border, an inset)
    // will have children slightly overlap it; pad the affected children
    // (e.g. AnchorLayoutParams' margins, or StackLayout::setPadding()) to
    // compensate if that matters.
    class Layout : public UIComponent {
    public:
        virtual ~Layout() = default;

        // Positions the SubViews in container.childViews() by calling
        // their setBounds(). AnchorLayout and StackLayout skip children
        // that are currently invisible (isVisible() == false) and leave
        // them untouched - hiding a child removes it from the
        // arrangement entirely rather than reserving its space, matching
        // how most modern layout systems treat a hidden widget.
        // CardLayout is the exception: it owns visibility itself (see
        // its class comment), so it doesn't pre-filter by it.
        virtual void arrange(View& container) = 0;

        // No fields of its own - trivial no-op base, overridden by
        // AnchorLayout/StackLayout/CardLayout for their own fields.
        void writeFields(json5::builder& w) const override {}
        void readFields(const json5::value& obj) override {}
    };

    // Which edges (and/or centerlines) of the container a child pinned
    // via AnchorLayoutParams attaches to - combine with operator| (e.g.
    // Anchor::Left | Anchor::Top). Opposing edges set together (Left +
    // Right, or Top + Bottom) means the child stretches to fill that
    // axis instead of just being positioned along it - see
    // AnchorLayout::arrange().
    enum class Anchor : std::uint8_t {
        None    = 0,
        Left    = 1 << 0,
        Top     = 1 << 1,
        Right   = 1 << 2,
        Bottom  = 1 << 3,
        CenterX = 1 << 4,
        CenterY = 1 << 5,
    };

    inline Anchor operator|(Anchor a, Anchor b) {
        return static_cast<Anchor>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    inline Anchor operator&(Anchor a, Anchor b) {
        return static_cast<Anchor>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }

    inline Anchor& operator|=(Anchor& a, Anchor b) {
        a = a | b;
        return a;
    }

    inline bool hasAnchor(Anchor set, Anchor flag) {
        return (set & flag) != Anchor::None;
    }

    // AnchorLayout's per-child params - which of the container's edges
    // (and/or centerlines) this child pins to, the margin kept from each
    // pinned edge, and the size to use along any axis that isn't
    // stretched by a pair of opposing anchors. A child AnchorLayout
    // doesn't have params for (see SubView::layoutParams()) is left where
    // it already was - see AnchorLayout::arrange().
    class AnchorLayoutParams : public LayoutParams {
    public:
        AnchorLayoutParams() = default;
        explicit AnchorLayoutParams(Anchor anchors) : anchors(anchors) {}

        Anchor anchors = Anchor::Left | Anchor::Top;

        // Gap kept from the corresponding container edge, only meaningful
        // when that edge's flag is set in anchors.
        float leftMargin = 0.0f;
        float topMargin = 0.0f;
        float rightMargin = 0.0f;
        float bottomMargin = 0.0f;

        // Size used along an axis that isn't stretched by opposing
        // anchors (Left alone, Right alone, or CenterX - and the same
        // vertically for height). Ignored for a stretched axis (Left +
        // Right, or Top + Bottom), where size comes from the container
        // instead.
        float width = 0.0f;
        float height = 0.0f;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;
    };

    // Pins each child to some combination of its container's edges and
    // centerlines, WinForms Anchor-style (extended with CenterX/CenterY,
    // the way iOS/Android's constraint-based anchors do) - the
    // lightweight end of "modern" layout, well short of a full
    // constraint solver but enough for docking a toolbar to the top,
    // stretching a body to fill what's left, centering a dialog's
    // buttons, and similar fixed relationships to the container itself.
    class AnchorLayout : public Layout {
    public:
        void arrange(View& container) override;

        // No fields of its own (unlike StackLayout/CardLayout) - inherits
        // Layout's no-op writeFields()/readFields().
    };

    // Which direction a StackLayout arranges its children along - the
    // "main axis" the rest of this file's stacking vocabulary
    // (MainAxisAlignment, StackLayoutParams::weight, ...) is relative to.
    enum class Orientation {
        Horizontal,
        Vertical,
    };

    // How a StackLayout distributes leftover main-axis space (container
    // size minus every child's resolved size and the spacing between
    // them) when no child claims it via StackLayoutParams::weight - CSS
    // flexbox's justify-content, or Flutter's MainAxisAlignment.
    // Meaningless (ignored) when every child fits exactly or any child
    // has a non-zero weight, since a weighted child already absorbed
    // whatever leftover space there was.
    enum class MainAxisAlignment {
        Start,
        Center,
        End,
        SpaceBetween,  // gaps only between children, none at the ends
        SpaceAround,   // half-size gaps at the ends, full-size between
        SpaceEvenly,   // equal-size gaps everywhere, ends included
    };

    // How a StackLayout positions each child across the axis
    // perpendicular to its main one - CSS flexbox's align-items/
    // align-self, or Flutter's CrossAxisAlignment. Stretch (the default)
    // is the one that needs no per-child cross-axis size of its own;
    // the other three keep each child's existing cross-axis size and
    // just choose where along that axis it sits.
    enum class CrossAxisAlignment {
        Start,
        Center,
        End,
        Stretch,
    };

    // StackLayout's per-child params - how much of the container's
    // leftover main-axis space this child claims (weight), and an
    // optional override of the StackLayout's own crossAxisAlignment for
    // just this child. A child StackLayout doesn't have params for (or
    // whose weight is 0, the default) keeps its current main-axis size
    // untouched by leftover distribution - see StackLayout::arrange().
    class StackLayoutParams : public LayoutParams {
    public:
        StackLayoutParams() = default;
        explicit StackLayoutParams(float weight) : weight(weight) {}

        // This child's share of leftover main-axis space, proportional
        // to every other weighted sibling's own weight - CSS flex-grow.
        // 0 (the default) means "natural size only", i.e. this child's
        // current bounds size along the main axis.
        float weight = 0.0f;

        // Overrides the StackLayout's own crossAxisAlignment for this
        // child alone; unset (the default) means "use the container's".
        std::optional<CrossAxisAlignment> crossAxisAlignment;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;
    };

    // Arranges children in a single row (Orientation::Horizontal) or
    // column (Orientation::Vertical) - CSS flexbox/Flutter's Row and
    // Column collapsed into one class parameterized by orientation()
    // instead of two, since they only ever differ in which axis is
    // "main" and which is "cross". spacing() sits between consecutive
    // children; padding() insets the whole arrangement from the
    // container's edges on all four sides.
    class StackLayout : public Layout {
    public:
        explicit StackLayout(Orientation orientation = Orientation::Vertical)
            : orientation_(orientation) {}

        Orientation orientation() const {
            return orientation_;
        }

        void setOrientation(Orientation orientation) {
            orientation_ = orientation;
        }

        MainAxisAlignment mainAxisAlignment() const {
            return mainAxisAlignment_;
        }

        void setMainAxisAlignment(MainAxisAlignment alignment) {
            mainAxisAlignment_ = alignment;
        }

        CrossAxisAlignment crossAxisAlignment() const {
            return crossAxisAlignment_;
        }

        void setCrossAxisAlignment(CrossAxisAlignment alignment) {
            crossAxisAlignment_ = alignment;
        }

        float spacing() const {
            return spacing_;
        }

        void setSpacing(float spacing) {
            spacing_ = spacing;
        }

        float padding() const {
            return padding_;
        }

        void setPadding(float padding) {
            padding_ = padding;
        }

        void arrange(View& container) override;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    private:
        Orientation orientation_;
        MainAxisAlignment mainAxisAlignment_ = MainAxisAlignment::Start;
        CrossAxisAlignment crossAxisAlignment_ = CrossAxisAlignment::Stretch;
        float spacing_ = 0.0f;
        float padding_ = 0.0f;
    };

    // Shows exactly one child at a time, sized to fill the container
    // completely - every other child is hidden via setVisible(false).
    // Java AWT's CardLayout; useful for wizard steps, tabbed content
    // panes, or any single-active-view stack. Children keep their place
    // in container.childViews() (index-based); show()'s by-name overload
    // matches against SubView::getName().
    //
    // Unlike AnchorLayout/StackLayout, CardLayout remembers the last
    // container arrange() ran on, so show()/next()/previous() can
    // re-arrange immediately instead of waiting for the next resize or
    // child-list change to pick up the new selection.
    class CardLayout : public Layout {
    public:
        void arrange(View& container) override;

        // Selects childViews()[index] as the visible card. index is
        // clamped to the last valid child index the next time arrange()
        // runs (immediately, if this CardLayout is already attached to a
        // container - see the class comment); out-of-range values are
        // otherwise kept as given until then. A no-op if there are no
        // children at all.
        void show(std::size_t index);

        // Selects the first child whose getName() == name; a no-op if
        // none matches, or if this CardLayout isn't attached to a
        // container yet.
        void show(const std::string& name);

        // Selects the next/previous child, wrapping around at either
        // end; a no-op if there are no children, or if this CardLayout
        // isn't attached to a container yet.
        void next();
        void previous();

        std::size_t activeIndex() const {
            return activeIndex_;
        }

        // Written/read as a plain index (not resolved through show(name)'s
        // by-name lookup) - the tree-walker rebuilds children before
        // restoring layout state, but readFields() itself has no way to
        // reach this Layout's container to validate/clamp the index against
        // childViews() at read time; arrange() already clamps it lazily
        // the next time it runs (see the class comment), so an
        // out-of-range value loaded here is harmless.
        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    private:
        std::size_t activeIndex_ = 0;
        View* container_ = nullptr;
    };

}
