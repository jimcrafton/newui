#pragma once

#include <newui/subview.h>
#include <newui/cursor.h>
#include <newui/layout.h>

namespace newui {

// A draggable divider between exactly two child panes - reuses the same
// Orientation enum FlexLayout does, and with the same meaning: Horizontal
// arranges its two children left/right (a vertical divider bar, dragged
// horizontally); Vertical stacks them top/bottom (a horizontal divider
// bar, dragged vertically).
//
// Self-contained, like ScrollBar (this file's own closest precedent) -
// owns its drag state and arranges its own children directly, rather than
// being decomposed into a separate Layout + a separate divider SubView.
// The two managed panes are just this Splitter's ordinary childViews()[0]/
// [1] - addChild() them normally; nothing else needs registering. A
// Splitter with fewer than 2 children simply doesn't arrange anything yet,
// same "no-op until configured" shape FlexLayout/AnchorLayout already have
// for an empty container.
//
// A plain SubView, not a Control: Control's base constructor
// unconditionally subscribes its own onMouseDown/onMouseUp click-tracking
// (Control::handleTrackingMouseDown() always returns Handled for a
// mouseDown anywhere in the control's bounds, not just a specific
// sub-region) - real, discovered behavior that would make every mouseDown
// on this Splitter report Handled regardless of whether it actually hit
// the divider, since Control's own listener runs first. A Splitter has no
// use for Control's click/enabled-state machinery in the first place (it
// only ever cares about one narrow region, the divider), so avoiding
// Control entirely sidesteps the mismatch rather than working around it.
// Which pane keeps a fixed size across an ordinary container resize (a
// window resize, or a resize cascading down from some other Splitter
// higher up the tree being dragged) - the other pane always absorbs the
// difference. Independent of which side First/Second land on visually
// (First is always the near/left-or-top pane, same as before) - this is
// purely about resize behavior, matching the standard docking-IDE
// convention: a fixed-size dock stays fixed, the flexible content area
// fills whatever's left. Dragging the divider itself always works
// identically either way - only setBounds()-driven resizes differ.
enum class SplitterFixedPane { First, Second };

class Splitter : public SubView {
public:
    explicit Splitter(Orientation orientation = Orientation::Horizontal);
    ~Splitter() override {}

    Orientation orientation() const { return orientation_; }
    void setOrientation(Orientation orientation);

    // First (the default - matches every pre-existing caller/test
    // unchanged): distance from this Splitter's own left (Horizontal) or
    // top (Vertical) client edge to the divider's near edge, i.e. pane[0]'s
    // own size. Second: the same value instead measures pane[1]'s own size
    // from the *far* edge - see fixedPane()'s own comment. Either way,
    // clamped on every set/drag to keep both panes at least minPaneSize()
    // along the split axis.
    float splitPosition() const { return splitPosition_; }
    void setSplitPosition(float position);

    float dividerThickness() const { return dividerThickness_; }
    void setDividerThickness(float thickness);

    float minPaneSize() const { return minPaneSize_; }
    void setMinPaneSize(float size);

    SplitterFixedPane fixedPane() const { return fixedPane_; }
    void setFixedPane(SplitterFixedPane pane);

    void setBounds(const Rect& bounds) override;
    void addChild(SubView* child) override;
    void removeChild(SubView* child) override;

    void paint(BLContext& ctx) override;

private:
    void arrangePanes();
    // pane[0]'s own size given bounds and the current fixedPane() mode -
    // shared by arrangePanes()/dividerRect() so they can never disagree.
    float firstPaneSize(const Rect& bounds) const;
    float clampSplitPosition(float position) const;
    Rect dividerRect() const;
    bool isPointInDivider(const Point& localPt) const;
    void updateCursor();

    SyncReturn handleMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
    SyncReturn handleMouseMove(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
    SyncReturn handleMouseUp(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);

    Orientation orientation_;
    float splitPosition_ = 200.0f;
    float dividerThickness_ = 6.0f;
    float minPaneSize_ = 40.0f;
    SplitterFixedPane fixedPane_ = SplitterFixedPane::First;
    bool dragging_ = false;
};

}
