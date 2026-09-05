#include "newui/splitter.h"
#include "newui/uicolormanager.h"

#include <algorithm>

namespace newui {

Splitter::Splitter(Orientation orientation) : orientation_(orientation) {
    setVisible(true);

    style().setBackgroundColor(UIColorManager::colorFor(UIColorRole::ControlBackground));

    updateCursor();

    onMouseDown.add(this, &Splitter::handleMouseDown);
    onMouseMove.add(this, &Splitter::handleMouseMove);
    onMouseUp.add(this, &Splitter::handleMouseUp);
}

void Splitter::setOrientation(Orientation orientation) {
    if (orientation_ == orientation) {
        return;
    }
    orientation_ = orientation;
    updateCursor();
    arrangePanes();
    redraw();
}

void Splitter::setFixedPane(SplitterFixedPane pane) {
    if (fixedPane_ == pane) {
        return;
    }
    fixedPane_ = pane;
    arrangePanes();
    redraw();
}

void Splitter::setSplitPosition(float position) {
    splitPosition_ = clampSplitPosition(position);
    arrangePanes();
    redraw();
}

void Splitter::setDividerThickness(float thickness) {
    dividerThickness_ = (thickness > 0.0f) ? thickness : 0.0f;
    splitPosition_ = clampSplitPosition(splitPosition_);
    arrangePanes();
    redraw();
}

void Splitter::setMinPaneSize(float size) {
    minPaneSize_ = (size >= 0.0f) ? size : 0.0f;
    splitPosition_ = clampSplitPosition(splitPosition_);
    arrangePanes();
}

void Splitter::setBounds(const Rect& bounds) {
    SubView::setBounds(bounds);
    splitPosition_ = clampSplitPosition(splitPosition_);
    arrangePanes();
}

void Splitter::addChild(SubView* child) {
    SubView::addChild(child);
    arrangePanes();
}

void Splitter::removeChild(SubView* child) {
    SubView::removeChild(child);
    arrangePanes();
}

float Splitter::clampSplitPosition(float position) const {
    Rect bounds = getClientBounds();
    float mainAxisSize = (orientation_ == Orientation::Horizontal) ? bounds.size().width : bounds.size().height;
    float maxPos = mainAxisSize - dividerThickness_ - minPaneSize_;
    if (maxPos < minPaneSize_) {
        // Not enough room for two real panes plus the divider yet (e.g.
        // before this Splitter's own first real layout pass, when bounds
        // are still the default {0,0,0,0} - real, hit case: ViewBuilder's
        // configure<Splitter>(fn) calling setSplitPosition() right after
        // construction, before attachment). Every setter routes through
        // here and stores the result back into splitPosition_ - previously
        // this branch returned a value computed from today's degenerate
        // bounds (0, or half of them), permanently discarding whatever the
        // caller actually requested. That's unrecoverable: once real
        // bounds do arrive, setBounds() below re-clamps via this same
        // function, but only ever shrinks/floors the stored value - it
        // never grows a too-small one back up, so the real, intended split
        // position (e.g. 560.0f) was already lost for good. Returning
        // position unchanged here instead defers the real clamp until
        // there's an actual range to clamp into - arrangePanes() is a
        // documented no-op with fewer than 2 children (the only way this
        // branch is reached with children already attached is another
        // Splitter's own premature arrangePanes() during the same
        // construction dance, itself equally transient and superseded once
        // real bounds cascade down for real).
        return position;
    }
    return (std::min)((std::max)(position, minPaneSize_), maxPos);
}

float Splitter::firstPaneSize(const Rect& bounds) const {
    if (fixedPane_ == SplitterFixedPane::First) {
        return splitPosition_;
    }
    // Second: splitPosition_ instead means pane[1]'s own fixed size -
    // pane[0] gets whatever's left, recomputed fresh from bounds (the
    // *current* container size) every call rather than stored, so it
    // naturally grows/shrinks with any resize - a window resize, or one
    // cascading down from some other Splitter higher up the tree being
    // dragged - with no separate resize hook needed.
    float mainAxisSize = (orientation_ == Orientation::Horizontal) ? bounds.size().width : bounds.size().height;
    return (std::max)(0.0f, mainAxisSize - splitPosition_ - dividerThickness_);
}

void Splitter::arrangePanes() {
    const std::vector<SubView*>& children = childViews();
    Rect bounds = getClientBounds();
    float pos = firstPaneSize(bounds);

    if (orientation_ == Orientation::Horizontal) {
        if (children.size() >= 1) {
            children[0]->setBounds(Rect(bounds.left(), bounds.top(), pos, bounds.size().height));
        }
        if (children.size() >= 2) {
            float secondX = bounds.left() + pos + dividerThickness_;
            float secondW = (std::max)(0.0f, bounds.size().width - pos - dividerThickness_);
            children[1]->setBounds(Rect(secondX, bounds.top(), secondW, bounds.size().height));
        }
    } else {
        if (children.size() >= 1) {
            children[0]->setBounds(Rect(bounds.left(), bounds.top(), bounds.size().width, pos));
        }
        if (children.size() >= 2) {
            float secondY = bounds.top() + pos + dividerThickness_;
            float secondH = (std::max)(0.0f, bounds.size().height - pos - dividerThickness_);
            children[1]->setBounds(Rect(bounds.left(), secondY, bounds.size().width, secondH));
        }
    }
}

Rect Splitter::dividerRect() const {
    Rect bounds = getClientBounds();
    // The divider's near edge always sits right after pane[0], regardless
    // of which pane fixedPane() says stays a fixed size - same helper
    // arrangePanes() uses, so this can never disagree with where the panes
    // themselves actually got placed.
    float pos = firstPaneSize(bounds);
    if (orientation_ == Orientation::Horizontal) {
        return Rect(bounds.left() + pos, bounds.top(), dividerThickness_, bounds.size().height);
    }
    return Rect(bounds.left(), bounds.top() + pos, bounds.size().width, dividerThickness_);
}

bool Splitter::isPointInDivider(const Point& localPt) const {
    Rect divider = dividerRect();
    return localPt.x >= divider.left() && localPt.x < divider.right()
        && localPt.y >= divider.top() && localPt.y < divider.bottom();
}

void Splitter::updateCursor() {
    setCursor(Cursor(orientation_ == Orientation::Horizontal ? CursorKind::SizeWE : CursorKind::SizeNS));
}

void Splitter::paint(BLContext& ctx) {
    Rect divider = dividerRect();
    if (divider.size().width <= 0.0f || divider.size().height <= 0.0f) {
        return;
    }

    ctx.save();
    ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32());
    ctx.fill_rect(BLRect(divider));
    ctx.restore();
}

SyncReturn Splitter::handleMouseDown(View& /*sender*/, const Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
    if (!isPointInDivider(pt)) {
        return SyncReturn::Ignored;
    }
    dragging_ = true;
    return SyncReturn::Handled;
}

SyncReturn Splitter::handleMouseMove(View& /*sender*/, const Point& pt,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
    if (!dragging_) {
        return SyncReturn::Ignored;
    }
    // Mouse capture (RootView::mouseDown()'s capturedSubView_ handling)
    // keeps this drag routed here regardless of where the cursor strays,
    // same as ScrollBar's own thumb drag - pt is already this Splitter's
    // own local point. pane0Size is what splitPosition_ would mean under
    // fixedPane() == First (pane[0]'s own size, measured from the near
    // edge) - for Second, that same cursor position instead means pane[1]
    // shrinks/grows the complementary amount, so it's converted via the
    // current bounds before being handed to setSplitPosition() (which
    // always expects "whatever splitPosition_ means right now").
    Rect bounds = getClientBounds();
    float cursorMain = (orientation_ == Orientation::Horizontal) ? pt.x : pt.y;
    float pane0Size = cursorMain - dividerThickness_ * 0.5f;
    if (fixedPane_ == SplitterFixedPane::First) {
        setSplitPosition(pane0Size);
    } else {
        float mainAxisSize = (orientation_ == Orientation::Horizontal) ? bounds.size().width : bounds.size().height;
        setSplitPosition(mainAxisSize - pane0Size - dividerThickness_);
    }
    return SyncReturn::Handled;
}

SyncReturn Splitter::handleMouseUp(View& /*sender*/, const Point& /*pt*/,
        std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
    if (!dragging_) {
        return SyncReturn::Ignored;
    }
    dragging_ = false;
    return SyncReturn::Handled;
}

}
