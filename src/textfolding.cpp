#include "newui/textfolding.h"
#include "newui/keyboard_constants.h"
#include "newui/uicolormanager.h"

#include <algorithm>
#include <cmath>
#include <string>

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
        countTextLines();
    }

    void TextFoldingController::countTextLines() {
        const std::wstring& text = model().text();
        textLineCount_ = 1 + static_cast<std::size_t>(std::count(text.begin(), text.end(), L'\n'));
    }

    namespace {
        constexpr float kGutterPad = 4.0f;       // before the numbers
        constexpr float kNumbersGap = 6.0f;      // numbers to markers
        constexpr float kMarkerSize = 9.0f;
        constexpr float kMarkerGap = 5.0f;       // markers to text

        std::size_t digitCount(std::size_t value) {
            std::size_t digits = 1;
            while (value >= 10) {
                value /= 10;
                ++digits;
            }
            return digits;
        }
    }

    void TextFoldingController::setGutterVisible(bool visible) {
        if (gutterVisible_ != visible) {
            gutterVisible_ = visible;
            foldsChanged();
        }
    }

    TextFoldingController::GutterColumns TextFoldingController::gutterColumns() const {
        const std::size_t digits = digitCount(textLineCount_) < 2 ? 2 : digitCount(textLineCount_);
        GutterColumns columns;
        columns.numbersRight = owner().getClientBounds().left() + kGutterPad
            + static_cast<float>(digits) * font().measureText("0").width;
        columns.markerLeft = columns.numbersRight + kNumbersGap;
        return columns;
    }

    float TextFoldingController::gutterWidth() const {
        if (!gutterVisible_) {
            return 0.0f;
        }
        return gutterColumns().markerLeft + kMarkerSize + kMarkerGap - owner().getClientBounds().left();
    }

    Rect TextFoldingController::textArea() const {
        const Rect client = owner().getClientBounds();
        const float gutter = gutterWidth();
        const float width = client.width() > gutter ? client.width() - gutter : 0.0f;
        return Rect(client.left() + gutter, client.top(), width, client.height());
    }

    std::size_t TextFoldingController::markerFold(std::size_t line) const {
        const text::TextLayoutEngine& engine = layoutEngine();
        if (line >= engine.lineCount()) {
            return kNoFold;
        }
        const std::size_t lineStart = engine.lineStart(line);
        const std::size_t lineEnd = lineStart + engine.lineLength(line);
        auto it = std::lower_bound(folds_.begin(), folds_.end(), lineStart,
            [](const text::TextFold& fold, std::size_t offset) { return fold.start < offset; });
        for (; it != folds_.end() && it->start <= lineEnd; ++it) {
            // Collapsed: its placeholder is on this line. Expanded: it runs on past it.
            if (it->collapsed || it->end() > lineEnd) {
                return static_cast<std::size_t>(it - folds_.begin());
            }
        }
        return kNoFold;
    }

    void TextFoldingController::drawGutter(BLContext& ctx) const {
        const Rect client = owner().getClientBounds();
        BLFont* blFont = font().blFont();
        if (!gutterVisible_ || blFont == nullptr || client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }
        const text::TextLayoutEngine& engine = layoutEngine();
        const GutterColumns columns = gutterColumns();
        const float descent = font().measureText("0").descent;
        const BLRgba32 color = UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32();

        ctx.save();
        ctx.clip_to_rect(BLRect(client.left(), client.top(), gutterWidth(), client.height()));
        ctx.set_fill_style(color);
        ctx.set_stroke_style(color);
        ctx.set_stroke_width(1.0);
        for (std::size_t line = engine.lineAtY(scrollOffsetY()); line < engine.lineCount(); ++line) {
            const float top = client.top() + engine.lineTop(line) - scrollOffsetY();
            if (top >= client.bottom()) {
                break;
            }
            const float baseline = top + engine.lineBaseline(line);
            const std::string number = std::to_string(engine.lineNumber(line) + 1);
            ctx.fill_utf8_text(BLPoint(columns.numbersRight - font().measureText(number).width, baseline),
                *blFont, number.c_str(), number.size());

            const std::size_t fold = markerFold(line);
            if (fold == kNoFold) {
                continue;
            }
            // A box centred on the first row, with a minus - and a plus's upright when collapsed.
            const double x = std::floor(columns.markerLeft) + 0.5;
            const double y = std::floor((top + baseline + descent) * 0.5f - kMarkerSize * 0.5f) + 0.5;
            const double size = kMarkerSize - 1.0;
            ctx.stroke_rect(x, y, size, size);
            ctx.stroke_line(x + 2.0, y + size * 0.5, x + size - 2.0, y + size * 0.5);
            if (folds_[fold].collapsed) {
                ctx.stroke_line(x + size * 0.5, y + 2.0, x + size * 0.5, y + size - 2.0);
            }
        }
        ctx.restore();
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
        const text::TextLayoutEngine& engine = layoutEngine();
        if (gutterVisible_ && pt.x < textArea().left()) {
            if (engine.lineCount() == 0) {
                return SyncReturn::Handled;
            }
            const std::size_t line = engine.lineAtY(toLayoutSpace(pt).y);
            const std::size_t fold = markerFold(line);
            if (fold != kNoFold && pt.x >= gutterColumns().markerLeft - 2.0f) {
                setFoldCollapsed(fold, !folds_[fold].collapsed);
                return SyncReturn::Handled;
            }
            // A line number: select its line, break included.
            const std::size_t end = line + 1 < engine.lineCount() ? engine.lineStart(line + 1) : model().length();
            moveCaret(engine.lineStart(line), false);
            moveCaret(end, true);
            return SyncReturn::Handled;
        }
        std::size_t foldStart = 0;
        if (engine.foldAtPoint(toLayoutSpace(pt), foldStart)) {
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
        countTextLines();
        return SyncReturn::Handled;
    }

    SyncReturn TextFoldingController::handleModelAfterRangeChanged(text::TextModel& sender, const text::TextRange& range, const std::wstring& replacement) {
        adjustFolds(range.start(), range.length(), replacement.size());
        countTextLines();
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

    bool TextFoldingControl::gutterVisible() const {
        const TextFoldingController* folding = foldingController();
        return folding != nullptr && folding->gutterVisible();
    }

    void TextFoldingControl::setGutterVisible(bool visible) {
        if (TextFoldingController* folding = foldingController()) {
            folding->setGutterVisible(visible);
        }
    }

    void TextFoldingControl::paint(BLContext& ctx) {
        TextControl::paint(ctx);
        const TextFoldingController* folding = foldingController();
        if (folding == nullptr) {
            return;
        }
        const Rect area = folding->textArea();
        if (area.width() > 0.0f && area.height() > 0.0f) {
            ctx.save();
            ctx.translate(area.left(), area.top());
            folding->drawFoldPlaceholders(ctx, area.height());
            ctx.restore();
        }
        folding->drawGutter(ctx);
    }

}
