#include "newui/textfolding.h"
#include "newui/keyboard_constants.h"
#include "newui/uicolormanager.h"

#include <algorithm>

namespace newui {

    // -----------------------------------------------------------------
    // TextFoldingController
    // -----------------------------------------------------------------

    TextFoldingController::TextFoldingController(Control& owner) : TextController(owner) {
        // The base constructor attached its model before this class existed.
        modelAttached(model());
    }

    void TextFoldingController::modelAttached(text::TextModel& model) {
        model.onAfterChar.add(this, &TextFoldingController::handleModelAfterChar);
        model.onAfterRangeChanged.add(this, &TextFoldingController::handleModelAfterRangeChanged);
        folds_.clear();
    }

    void TextFoldingController::setFolds(std::vector<text::TextFold> folds) {
        folds.erase(std::remove_if(folds.begin(), folds.end(), [](const text::TextFold& fold) { return fold.length == 0; }),
            folds.end());
        std::stable_sort(folds.begin(), folds.end(), [](const text::TextFold& a, const text::TextFold& b) {
            return a.start != b.start ? a.start < b.start : a.length > b.length;
        });
        folds_ = std::move(folds);
        if (caret().position().isValid()) {
            expandFoldsAt(caret().position().offset());
        }
        foldsChanged();
    }

    void TextFoldingController::setFoldCollapsed(std::size_t index, bool collapsed) {
        if (index >= folds_.size() || folds_[index].collapsed == collapsed) {
            return;
        }
        folds_[index].collapsed = collapsed;
        if (collapsed && caret().position().isValid()) {
            const std::size_t offset = caret().position().offset();
            if (offset > folds_[index].start && offset < folds_[index].end()) {
                moveCaret(folds_[index].start, false);
            }
        }
        foldsChanged();
    }

    bool TextFoldingController::expandFoldsAt(std::size_t offset) {
        bool expanded = false;
        for (text::TextFold& fold : folds_) {
            if (fold.collapsed && offset > fold.start && offset < fold.end()) {
                fold.collapsed = false;
                expanded = true;
            }
        }
        if (expanded) {
            foldsChanged();
        }
        return expanded;
    }

    void TextFoldingController::foldsChanged() {
        owner().style().markDirty();
        owner().onContentSizeChanged(owner());
    }

    void TextFoldingController::drawFoldPlaceholders(BLContext& ctx, float visibleHeight) const {
        const std::vector<Rect> rects = layoutEngine().placeholderRects(scrollOffsetY(), scrollOffsetY() + visibleHeight);
        if (rects.empty()) {
            return;
        }
        ctx.save();
        ctx.translate(0.0f, -scrollOffsetY());
        ctx.set_stroke_style(UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32());
        ctx.set_stroke_width(1.0);
        for (const Rect& r : rects) {
            ctx.stroke_round_rect(r.left() - 1.5, r.top() + 0.5, r.width() + 3.0, r.height() - 1.0, 2.0);
        }
        ctx.restore();
    }

    std::size_t TextFoldingController::stepOver(std::size_t offset, bool forward) const {
        std::size_t result = offset;
        for (const text::TextFold& fold : folds_) {
            if (!fold.collapsed) {
                continue;
            }
            if (forward && fold.start == offset && fold.end() > result) {
                result = fold.end();
            } else if (!forward && fold.end() == offset && fold.start < result) {
                result = fold.start;
            }
        }
        return result;
    }

    SyncReturn TextFoldingController::handleMouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
        ensureLayoutUpToDate();
        std::size_t foldStart = 0;
        if (layoutEngine().foldAtPoint(toLayoutSpace(pt), foldStart)) {
            for (text::TextFold& fold : folds_) {
                if (fold.collapsed && fold.start == foldStart) {
                    fold.collapsed = false;
                }
            }
            moveCaret(foldStart, false);
            foldsChanged();
            return SyncReturn::Handled;
        }
        return TextController::handleMouseDown(pt, btnMask, keyMask);
    }

    SyncReturn TextFoldingController::handleKeyDown(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) {
        const bool extend = (keyMask & kmShift) != 0;
        const bool hasCaret = caret().position().isValid();
        const std::size_t offset = hasCaret ? caret().position().offset() : 0;
        switch (VKeyCode) {
            case vkLeftArrow:
            case vkRightArrow: {
                const std::size_t target = hasCaret ? stepOver(offset, VKeyCode == vkRightArrow) : offset;
                if (target != offset) {
                    moveCaret(target, extend);
                    return SyncReturn::Handled;
                }
                break;
            }
            case vkBackSpace:
            case vkDelete: {
                // Against a collapsed fold: open it rather than delete text nobody can see.
                if (hasCaret && selection().isEmpty() && stepOver(offset, VKeyCode == vkDelete) != offset) {
                    for (text::TextFold& fold : folds_) {
                        if (fold.collapsed && (VKeyCode == vkDelete ? fold.start : fold.end()) == offset) {
                            fold.collapsed = false;
                        }
                    }
                    foldsChanged();
                    return SyncReturn::Handled;
                }
                break;
            }
            default:
                break;
        }
        const SyncReturn result = TextController::handleKeyDown(keyMask, keyCharVal, repeatCount, VKeyCode);
        if (caret().position().isValid()) {
            expandFoldsAt(caret().position().offset());
        }
        return result;
    }

    SyncReturn TextFoldingController::handleModelAfterChar(text::TextModel& sender, size_t offset, wchar_t ch, text::CharChangeKind kind) {
        if (kind == text::CharChangeKind::Inserted) {
            adjustFolds(offset, 0, 1);
        } else {
            adjustFolds(offset, 1, 0);
        }
        return SyncReturn::Handled;
    }

    SyncReturn TextFoldingController::handleModelAfterRangeChanged(text::TextModel& sender, const text::TextRange& range, const std::wstring& replacement) {
        adjustFolds(range.start(), range.length(), replacement.size());
        return SyncReturn::Handled;
    }

    void TextFoldingController::adjustFolds(std::size_t start, std::size_t removed, std::size_t inserted) {
        const std::size_t editEnd = start + removed;
        for (auto it = folds_.begin(); it != folds_.end();) {
            text::TextFold& fold = *it;
            if (fold.end() <= start) {
                ++it;   // before the edit
            } else if (fold.start >= editEnd) {
                fold.start = fold.start + inserted - removed;   // after it
                ++it;
            } else if (start <= fold.start && editEnd >= fold.end()) {
                it = folds_.erase(it);   // all of it replaced
            } else if (start >= fold.start && editEnd <= fold.end()) {
                fold.length = fold.length + inserted - removed;   // inside it
                fold.collapsed = false;
                ++it;
            } else {
                it = folds_.erase(it);   // across an edge
            }
        }
    }

    // -----------------------------------------------------------------
    // TextFoldingControl
    // -----------------------------------------------------------------

    TextFoldingControl::TextFoldingControl() {
        setController(std::make_unique<TextFoldingController>(*this));
    }

    const std::vector<text::TextFold>& TextFoldingControl::folds() const {
        static const std::vector<text::TextFold> none;
        const TextFoldingController* folding = foldingController();
        return folding != nullptr ? folding->folds() : none;
    }

    void TextFoldingControl::setFolds(std::vector<text::TextFold> folds) {
        if (TextFoldingController* folding = foldingController()) {
            folding->setFolds(std::move(folds));
        }
    }

    void TextFoldingControl::setFoldCollapsed(std::size_t index, bool collapsed) {
        if (TextFoldingController* folding = foldingController()) {
            folding->setFoldCollapsed(index, collapsed);
        }
    }

    void TextFoldingControl::paint(BLContext& ctx) {
        TextControl::paint(ctx);
        const TextFoldingController* folding = foldingController();
        const Rect clientBounds = getClientBounds();
        if (folding == nullptr || clientBounds.width() <= 0.0f || clientBounds.height() <= 0.0f) {
            return;
        }
        ctx.save();
        ctx.translate(clientBounds.left(), clientBounds.top());
        folding->drawFoldPlaceholders(ctx, clientBounds.height());
        ctx.restore();
    }

}
