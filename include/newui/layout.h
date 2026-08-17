#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace newui {

    class View;
    class SubView;

    // Per-child metadata a Layout needs beyond what SubView itself already
    // tracks (bounds, visibility, name, ...) - e.g. which edges
    // AnchorLayout pins, or how much of a FlexLayout's main axis a child
    // should claim. Each Layout subclass that needs per-child data defines
    // its own subclass of this (AnchorLayoutParams, FlexLayoutParams,
    // GridLayoutParams, ...) - see SubView::setLayoutParams(). A child with
    // no LayoutParams set (or one of the wrong kind for whichever Layout is
    // attached to its parent) gets that Layout's own default behavior for
    // the unset case - see each Layout subclass's arrange() for what that is.
    class LayoutParams {
    public:
        virtual ~LayoutParams() = default;
    };

    // Arranges a View's direct children (its childViews() - always
    // SubView*) within that View's own bounds. Attach one via
    // View::setLayout() - see View::updateLayout(), which calls arrange()
    // automatically whenever the View's own size changes
    // (SubView::setBounds()/RootView::setBounds()) or its child list
    // changes (addChild()/removeChild()), so normal use never needs to
    // call arrange() directly.
    //
    // Arranges into container.getClientBounds(), not its full getBounds()
    // - getClientBounds() is computed live from the container's current
    // style() (see ViewStyle::computeClientBounds()), so a style that
    // paints chrome (a border, a 3D edge, a checkbox glyph) is
    // automatically excluded from where children get positioned, with no
    // dependency on whether a paint has happened yet. AnchorLayoutParams'
    // margins / FlexLayout::setPadding() still work exactly as before on
    // top of that, for spacing beyond what the style itself needs.
    class Layout {
    public:
        virtual ~Layout() = default;

        // Positions the SubViews in container.childViews() by calling
        // their setBounds(). AnchorLayout and FlexLayout skip children
        // that are currently invisible (isVisible() == false) and leave
        // them untouched - hiding a child removes it from the
        // arrangement entirely rather than reserving its space, matching
        // how most modern layout systems treat a hidden widget.
        // CardLayout is the exception: it owns visibility itself (see
        // its class comment), so it doesn't pre-filter by it.
        virtual void arrange(View& container) = 0;
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
    };

    // Which direction a FlexLayout arranges its children along - the
    // "main axis" the rest of this file's stacking vocabulary
    // (MainAxisAlignment, FlexLayoutParams::weight, ...) is relative to.
    enum class Orientation {
        Horizontal,
        Vertical,
    };

    // How a FlexLayout distributes leftover space among N things with
    // uniform gaps between them - CSS flexbox's justify-content (applied
    // to items along the main axis - container size minus every child's
    // resolved size and the spacing between them, when no child claims it
    // via FlexLayoutParams::weight) and, when FlexLayout::wrap() is on,
    // also align-content (applied to whole lines along the cross axis -
    // see FlexLayout::alignContent()). Meaningless (ignored) for the
    // main-axis case when every child fits exactly or any child has a
    // non-zero weight, since a weighted child already absorbed whatever
    // leftover space there was.
    enum class MainAxisAlignment {
        Start,
        Center,
        End,
        SpaceBetween,  // gaps only between things, none at the ends
        SpaceAround,   // half-size gaps at the ends, full-size between
        SpaceEvenly,   // equal-size gaps everywhere, ends included
    };

    // How a FlexLayout positions each child across the axis
    // perpendicular to its main one - CSS flexbox's align-items/
    // align-self, or Flutter's CrossAxisAlignment. Stretch (the default)
    // is the one that needs no per-child cross-axis size of its own;
    // the other three keep each child's existing cross-axis size and
    // just choose where along that axis it sits. When FlexLayout::wrap()
    // is on, "the cross axis" for Stretch/positioning purposes means the
    // child's own line's thickness, not the whole container's cross size
    // - see FlexLayout::arrange()'s comment. Also reused by
    // GridLayoutParams for per-cell horizontal/vertical alignment.
    enum class CrossAxisAlignment {
        Start,
        Center,
        End,
        Stretch,
    };

    // FlexLayout's per-child params - how much of the container's
    // leftover main-axis space this child claims (weight), and an
    // optional override of the FlexLayout's own crossAxisAlignment for
    // just this child. A child FlexLayout doesn't have params for (or
    // whose weight is 0, the default) keeps its current main-axis size
    // untouched by leftover distribution - see FlexLayout::arrange().
    class FlexLayoutParams : public LayoutParams {
    public:
        FlexLayoutParams() = default;
        explicit FlexLayoutParams(float weight) : weight(weight) {}

        // This child's share of leftover main-axis space, proportional
        // to every other weighted sibling's own weight - CSS flex-grow.
        // 0 (the default) means "natural size only", i.e. this child's
        // desiredSize() along the main axis (see View::desiredSize()).
        float weight = 0.0f;

        // Overrides the FlexLayout's own crossAxisAlignment for this
        // child alone; unset (the default) means "use the container's".
        std::optional<CrossAxisAlignment> crossAxisAlignment;

    };

    // Arranges children in a single row (Orientation::Horizontal) or
    // column (Orientation::Vertical) - CSS flexbox/Flutter's Row and
    // Column collapsed into one class parameterized by orientation()
    // instead of two, since they only ever differ in which axis is
    // "main" and which is "cross". spacing() sits between consecutive
    // children; padding() insets the whole arrangement from the
    // container's edges on all four sides.
    //
    // wrap() (default false) is what makes this real CSS-flexbox-style
    // wrapping, not just single-line stacking: when on, children that
    // don't fit the current line spill onto a new one instead of
    // overflowing, lineSpacing() sits between consecutive lines (along the
    // cross axis, independent of spacing()'s main-axis gap), and
    // alignContent() distributes leftover cross-axis space across the set
    // of lines - the same problem mainAxisAlignment() already solves for
    // items within a line, just applied to lines instead (reuses
    // MainAxisAlignment rather than a separate enum). wrap() == false (the
    // default) is unaffected by lineSpacing()/alignContent() and behaves
    // exactly as this class always has.
    class FlexLayout : public Layout {
    public:
        explicit FlexLayout(Orientation orientation = Orientation::Vertical)
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

        bool wrap() const {
            return wrap_;
        }

        void setWrap(bool wrap) {
            wrap_ = wrap;
        }

        // Gap between consecutive lines along the cross axis - only
        // meaningful when wrap() is true. Independent of spacing(), which
        // stays the main-axis gap between items within a line.
        float lineSpacing() const {
            return lineSpacing_;
        }

        void setLineSpacing(float lineSpacing) {
            lineSpacing_ = lineSpacing;
        }

        // How leftover cross-axis space is distributed across the set of
        // lines - only meaningful when wrap() is true. See the class
        // comment; no "stretch line thickness to fill leftover space"
        // option (CSS align-content: stretch) - out of scope, Start is
        // the sane default.
        MainAxisAlignment alignContent() const {
            return alignContent_;
        }

        void setAlignContent(MainAxisAlignment alignContent) {
            alignContent_ = alignContent;
        }

        void arrange(View& container) override;


    private:
        Orientation orientation_;
        MainAxisAlignment mainAxisAlignment_ = MainAxisAlignment::Start;
        CrossAxisAlignment crossAxisAlignment_ = CrossAxisAlignment::Stretch;
        float spacing_ = 0.0f;
        float padding_ = 0.0f;
        bool wrap_ = false;
        float lineSpacing_ = 0.0f;
        MainAxisAlignment alignContent_ = MainAxisAlignment::Start;
    };

    // Shows exactly one child at a time, sized to fill the container
    // completely - every other child is hidden via setVisible(false).
    // Java AWT's CardLayout; useful for wizard steps, tabbed content
    // panes, or any single-active-view stack. Children keep their place
    // in container.childViews() (index-based); show()'s by-name overload
    // matches against SubView::getName().
    //
    // Unlike AnchorLayout/FlexLayout, CardLayout remembers the last
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

    private:
        std::size_t activeIndex_ = 0;
        View* container_ = nullptr;
    };

    // How one GridLayout row/column track is sized. Fixed and Star mirror
    // WPF/WinForms GridLength's pixel and star-weighted kinds; Auto sizes
    // to the largest desiredSize() (see View::desiredSize()) among the
    // track's own (non-spanning - see GridLayout::arrange()'s comment)
    // children, rather than CSS Grid's full content-measurement algorithm -
    // this toolkit has no content/text measurement pass anywhere, so a
    // real "auto" would need one designed first; desiredSize() being a
    // plain settable-or-overridable value (not something computed from
    // content automatically) is what makes even this much possible.
    enum class GridTrackKind {
        Fixed,
        Star,
        Auto,
    };

    // One row or column definition - value is a pixel size for Fixed,
    // a weight (proportional share of leftover space, like
    // FlexLayoutParams::weight) for Star, and ignored for Auto.
    struct GridTrack {
        GridTrackKind kind = GridTrackKind::Star;
        float value = 1.0f;
    };

    // GridLayout's per-child params - which cell (row/column) a child
    // occupies, optionally spanning multiple rows/columns, and how it's
    // aligned within that cell on each axis independently (a grid cell
    // doesn't have a single "cross axis" the way a flex line does, so
    // this needs both horizontalAlignment and verticalAlignment, reusing
    // CrossAxisAlignment's Start/Center/End/Stretch on each). A child with
    // no GridLayoutParams, or whose row/column falls outside the
    // container's configured track lists, is left exactly where it was -
    // see GridLayout::arrange().
    class GridLayoutParams : public LayoutParams {
    public:
        GridLayoutParams() = default;
        GridLayoutParams(std::size_t row, std::size_t column) : row(row), column(column) {}

        std::size_t row = 0;
        std::size_t column = 0;

        // How many rows/columns (starting at row/column) this child
        // occupies - 1 (the default) means "just its own cell". A
        // spanning child (>1) is ignored when auto-sizing an Auto track -
        // see GridLayout::arrange().
        std::size_t rowSpan = 1;
        std::size_t columnSpan = 1;

        CrossAxisAlignment horizontalAlignment = CrossAxisAlignment::Stretch;
        CrossAxisAlignment verticalAlignment = CrossAxisAlignment::Stretch;

    };

    // Row-and-column ("table") layout - WPF/WinForms Grid/TableLayoutPanel-
    // style. Each axis is configured independently as a list of
    // GridTrack (addRow()/addColumn(), in order - index 0 is the first
    // row/column); a child is placed via GridLayoutParams naming which
    // cell(s) it occupies. rowSpacing()/columnSpacing() sit between
    // consecutive tracks on each axis, same idea as FlexLayout::spacing().
    class GridLayout : public Layout {
    public:
        void addRow(GridTrack track) {
            rows_.push_back(track);
        }

        void addColumn(GridTrack track) {
            columns_.push_back(track);
        }

        void addFixedRow(float pixels) {
            addRow(GridTrack{GridTrackKind::Fixed, pixels});
        }

        void addStarRow(float weight = 1.0f) {
            addRow(GridTrack{GridTrackKind::Star, weight});
        }

        void addAutoRow() {
            addRow(GridTrack{GridTrackKind::Auto, 0.0f});
        }

        void addFixedColumn(float pixels) {
            addColumn(GridTrack{GridTrackKind::Fixed, pixels});
        }

        void addStarColumn(float weight = 1.0f) {
            addColumn(GridTrack{GridTrackKind::Star, weight});
        }

        void addAutoColumn() {
            addColumn(GridTrack{GridTrackKind::Auto, 0.0f});
        }

        const std::vector<GridTrack>& rows() const {
            return rows_;
        }

        const std::vector<GridTrack>& columns() const {
            return columns_;
        }

        float rowSpacing() const {
            return rowSpacing_;
        }

        void setRowSpacing(float spacing) {
            rowSpacing_ = spacing;
        }

        float columnSpacing() const {
            return columnSpacing_;
        }

        void setColumnSpacing(float spacing) {
            columnSpacing_ = spacing;
        }

        void arrange(View& container) override;


    private:
        std::vector<GridTrack> rows_;
        std::vector<GridTrack> columns_;
        float rowSpacing_ = 0.0f;
        float columnSpacing_ = 0.0f;
    };

}
