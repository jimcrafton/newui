#include "layertree.h"

#include <newui/color.h>
#include <newui/fontmanager.h>
#include <newui/keyboard_constants.h>
#include <newui/mouse_constants.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <cmath>

namespace sketch {

    using namespace newui;

    namespace {
        constexpr float kEyeX = 4.0f, kLockX = 22.0f, kIconSize = 14.0f, kContentX = 42.0f, kIndent = 14.0f, kTargetSize = 10.0f;
    }

    LayerTree::LayerTree() {
        setName("LayerTree");
        setVisible(true);
        onMouseDown.add(this, &LayerTree::mouseDown);
        onMouseMove.add(this, &LayerTree::mouseMove);
        onMouseUp.add(this, &LayerTree::mouseUp);
    }

    std::size_t LayerTree::visibleRows() const {
        return std::size_t(std::max(1.0f, std::floor(getClientBounds().height() / kRowHeight)));
    }

    void LayerTree::setRows(std::vector<LayerRow> rows) {
        rows_ = std::move(rows);
        // Keep the active layer in sight.
        std::size_t fit = visibleRows();
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].active) {
                if (i < scroll_) {
                    scroll_ = i;
                } else if (i >= scroll_ + fit) {
                    scroll_ = i + 1 - fit;
                }
            }
        }
        if (rows_.size() <= fit || scroll_ + fit > rows_.size()) {
            scroll_ = rows_.size() > fit ? rows_.size() - fit : 0;
        }
        pressed_ = rows_.size();
        dragging_ = false;
        style().markDirty();
    }

    Rect LayerTree::rowRect(std::size_t index) const {
        Rect client = getClientBounds();
        return Rect(client.left(), client.top() + float(index - std::min(index, scroll_)) * kRowHeight, client.width(), kRowHeight);
    }

    Rect LayerTree::eyeRect(std::size_t index) const {
        Rect r = rowRect(index);
        return Rect(r.left() + kEyeX, r.top() + (kRowHeight - kIconSize) * 0.5f, kIconSize, kIconSize);
    }

    Rect LayerTree::lockRect(std::size_t index) const {
        Rect r = rowRect(index);
        return Rect(r.left() + kLockX, r.top() + (kRowHeight - kIconSize) * 0.5f, kIconSize, kIconSize);
    }

    Rect LayerTree::triangleRect(std::size_t index) const {
        Rect r = rowRect(index);
        float x = r.left() + kContentX + float(rows_[index].depth) * kIndent;
        return Rect(x, r.top(), 12.0f, kRowHeight);
    }

    Rect LayerTree::nameRect(std::size_t index) const {
        Rect r = rowRect(index);
        float x = r.left() + kContentX + 12.0f + float(rows_[index].depth) * kIndent;
        return Rect(x, r.top(), std::max(0.0f, r.left() + r.width() - x - kTargetSize - 8.0f), kRowHeight);
    }

    Rect LayerTree::targetRect(std::size_t index) const {
        Rect r = rowRect(index);
        return Rect(r.left() + r.width() - kTargetSize - 4.0f, r.top() + (kRowHeight - kTargetSize) * 0.5f, kTargetSize, kTargetSize);
    }

    std::size_t LayerTree::rowAt(const Point& pt) const {
        Rect client = getClientBounds();
        float y = pt.y - client.top();
        if (y < 0.0f || pt.x < client.left() || pt.x > client.left() + client.width()) {
            return rows_.size();
        }
        std::size_t index = scroll_ + std::size_t(y / kRowHeight);
        return index < rows_.size() ? index : rows_.size();
    }

    LayerTree::Drop LayerTree::zoneAt(const Point& pt, std::size_t row) const {
        float inside = pt.y - rowRect(row).top();
        if (inside < kRowHeight * 0.25f) {
            return Drop::Above;
        }
        if (inside > kRowHeight * 0.75f) {
            return Drop::Below;
        }
        return Drop::Inside;
    }

    namespace {
        bool hit(const Rect& r, const Point& p) {
            return p.x >= r.left() && p.x < r.left() + r.width() && p.y >= r.top() && p.y < r.top() + r.height();
        }
    }

    SyncReturn LayerTree::mouseDown(View&, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if ((buttons & mbmLeftButton) == 0) {
            return SyncReturn::Ignored;
        }
        std::size_t row = rowAt(pt);
        if (row >= rows_.size()) {
            return SyncReturn::Ignored;
        }
        const std::size_t id = rows_[row].id;
        if (hit(eyeRect(row), pt)) {
            if (actions_.toggleVisible) { actions_.toggleVisible(id); }
        } else if (hit(lockRect(row), pt)) {
            if (actions_.toggleLock) { actions_.toggleLock(id); }
        } else if (rows_[row].hasChildren && hit(triangleRect(row), pt)) {
            if (actions_.toggleOpen) { actions_.toggleOpen(id); }
        } else if (hit(targetRect(row), pt)) {
            if (actions_.moveSelectionHere) { actions_.moveSelectionHere(id); }
        } else {
            pressed_ = row;
            ctrl_ = (keys & kmCtrl) != 0;
            dragging_ = false;
        }
        return SyncReturn::Handled;
    }

    SyncReturn LayerTree::mouseMove(View&, const Point& pt, std::uint32_t, std::uint32_t) {
        if (pressed_ >= rows_.size()) {
            return SyncReturn::Ignored;
        }
        std::size_t row = rowAt(pt);
        dragging_ = row < rows_.size() && row != pressed_;
        dropRow_ = row;
        if (dragging_) {
            dropZone_ = zoneAt(pt, row);
        }
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn LayerTree::mouseUp(View&, const Point&, std::uint32_t, std::uint32_t) {
        if (pressed_ >= rows_.size()) {
            return SyncReturn::Ignored;
        }
        std::size_t source = rows_[pressed_].id;
        bool wasDragging = dragging_ && dropRow_ < rows_.size();
        std::size_t target = wasDragging ? rows_[dropRow_].id : 0;
        Drop zone = dropZone_;
        bool ctrl = ctrl_;
        pressed_ = rows_.size();
        dragging_ = false;
        style().markDirty();
        if (wasDragging) {
            if (actions_.drop) { actions_.drop(source, target, zone); }
        } else if (actions_.activate) {
            actions_.activate(source, ctrl);
        }
        return SyncReturn::Handled;
    }

    void LayerTree::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Status);
        BLFont* blFont = font.blFont();
        const std::uint32_t text = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32().value;
        const std::uint32_t highlight = UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32().value;
        auto withAlpha = [](std::uint32_t color, std::uint32_t alpha) { return BLRgba32((color & 0x00FFFFFFu) | (alpha << 24)); };

        ctx.save();
        ctx.clip_to_rect(BLRect(client.left(), client.top(), client.width(), client.height()));
        std::size_t last = std::min(rows_.size(), scroll_ + visibleRows());
        for (std::size_t i = scroll_; i < last; ++i) {
            const LayerRow& row = rows_[i];
            Rect r = rowRect(i);
            if (row.active) {
                ctx.set_fill_style(withAlpha(highlight, 0x70));
                ctx.fill_rect(BLRect(r.left(), r.top(), r.width(), r.height()));
            }
            const bool dim = !row.shown || !row.editable;
            // eye
            Rect eye = eyeRect(i);
            double ex = eye.left() + eye.width() * 0.5, ey = eye.top() + eye.height() * 0.5;
            ctx.set_stroke_width(1.2);
            ctx.set_stroke_style(withAlpha(text, row.visible ? 0xFF : 0x40));
            ctx.stroke_ellipse(BLEllipse(ex, ey, 6.0, 3.6));
            if (row.visible) {
                ctx.set_fill_style(withAlpha(text, 0xFF));
                ctx.fill_circle(BLCircle(ex, ey, 1.8));
            }
            // lock
            Rect lock = lockRect(i);
            double lx = lock.left() + lock.width() * 0.5, ly = lock.top() + lock.height() * 0.5;
            ctx.set_stroke_style(withAlpha(text, row.locked ? 0xFF : 0x40));
            ctx.stroke_round_rect(BLRoundRect(lx - 4.0, ly - 1.0, 8.0, 6.0, 1.0));
            BLPath shackle;
            shackle.move_to(lx - 2.5, ly - 1.0);
            shackle.line_to(lx - 2.5, ly - 3.0);
            shackle.cubic_to(lx - 2.5, ly - 5.5, lx + 2.5, ly - 5.5, lx + 2.5, ly - 3.0);
            if (row.locked) {
                shackle.line_to(lx + 2.5, ly - 1.0);
                ctx.set_fill_style(withAlpha(text, 0xFF));
                ctx.fill_rect(BLRect(lx - 4.0, ly - 1.0, 8.0, 6.0));
            }
            ctx.stroke_path(shackle);
            // open / close triangle
            if (row.hasChildren) {
                Rect t = triangleRect(i);
                double tx = t.left() + 6.0, ty = t.top() + kRowHeight * 0.5;
                BLPath tri;
                if (row.expanded) {
                    tri.move_to(tx - 3.5, ty - 2.0);
                    tri.line_to(tx + 3.5, ty - 2.0);
                    tri.line_to(tx, ty + 3.0);
                } else {
                    tri.move_to(tx - 2.0, ty - 3.5);
                    tri.line_to(tx - 2.0, ty + 3.5);
                    tri.line_to(tx + 3.0, ty);
                }
                tri.close();
                ctx.set_fill_style(withAlpha(text, 0xC0));
                ctx.fill_path(tri);
            }
            // name
            Rect nameBox = nameRect(i);
            if (blFont != nullptr && blFont->is_valid()) {
                ctx.save();
                ctx.clip_to_rect(BLRect(nameBox.left(), nameBox.top(), nameBox.width(), nameBox.height()));
                ctx.set_fill_style(withAlpha(text, dim ? 0x70 : 0xFF));
                ctx.fill_utf8_text(BLPoint(nameBox.left() + 2.0, r.top() + kRowHeight * 0.5 + blFont->metrics().ascent * 0.5 - 1.0), *blFont, row.name.c_str(), row.name.size());
                ctx.restore();
            }
            // selection target
            Rect target = targetRect(i);
            ctx.set_stroke_style(withAlpha(text, 0x90));
            ctx.set_stroke_width(1.0);
            ctx.stroke_rect(BLRect(target.left() + 0.5, target.top() + 0.5, target.width() - 1.0, target.height() - 1.0));
            if (row.holdsSelection) {
                ctx.set_fill_style(withAlpha(highlight, 0xFF));
                ctx.fill_rect(BLRect(target.left() + 2.0, target.top() + 2.0, target.width() - 4.0, target.height() - 4.0));
            }
        }
        // where a dragged row would land
        if (dragging_ && dropRow_ >= scroll_ && dropRow_ < last) {
            Rect r = rowRect(dropRow_);
            ctx.set_stroke_style(withAlpha(highlight, 0xFF));
            ctx.set_stroke_width(2.0);
            if (dropZone_ == Drop::Inside) {
                ctx.stroke_rect(BLRect(r.left() + 1.0, r.top() + 1.0, r.width() - 2.0, r.height() - 2.0));
            } else {
                double y = dropZone_ == Drop::Above ? r.top() + 1.0 : r.top() + r.height() - 1.0;
                ctx.stroke_line(BLLine(r.left() + 2.0, y, r.left() + r.width() - 2.0, y));
            }
        }
        ctx.restore();
    }

}
