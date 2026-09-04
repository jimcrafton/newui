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
        // before this Splitter's own first layout pass) - clamp to
        // whatever's non-negative rather than producing a negative-sized
        // pane.
        return (std::max)(0.0f, mainAxisSize * 0.5f);
    }
    return (std::min)((std::max)(position, minPaneSize_), maxPos);
}

void Splitter::arrangePanes() {
    const std::vector<SubView*>& children = childViews();
    Rect bounds = getClientBounds();
    float pos = splitPosition_;

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
    if (orientation_ == Orientation::Horizontal) {
        return Rect(bounds.left() + splitPosition_, bounds.top(), dividerThickness_, bounds.size().height);
    }
    return Rect(bounds.left(), bounds.top() + splitPosition_, bounds.size().width, dividerThickness_);
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
    // same as ScrollBar's own thumb drag - splitPosition_ is measured from
    // this Splitter's own client edge, so pt (already this Splitter's own
    // local point) is exactly what setSplitPosition() wants.
    float newPos = (orientation_ == Orientation::Horizontal) ? pt.x : pt.y;
    setSplitPosition(newPos - dividerThickness_ * 0.5f);
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
