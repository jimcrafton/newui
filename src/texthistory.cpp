#include "newui/texthistory.h"

#include <cwctype>
#include <utility>

namespace newui::text {

    namespace {
        bool isLineBreak(wchar_t c) {
            return c == L'\r' || c == L'\n';
        }

        // A typing run continues while the characters stay in one class.
        int charClass(wchar_t c) {
            if (c == L'_' || std::iswalnum(static_cast<wint_t>(c)) != 0) {
                return 1;
            }
            return std::iswspace(static_cast<wint_t>(c)) != 0 ? 2 : 3;
        }
    }

    void HistoryTextModel::setText(const std::wstring& text) {
        if (replaying_) {
            TextModel::setText(text);
            return;
        }
        const PieceTree before = snapshot();
        TextModel::setText(text);
        if (!snapshot().sameAs(before)) {
            clearHistory();
            markClean();
        }
    }

    void HistoryTextModel::clear() {
        if (replaying_) {
            TextModel::clear();
            return;
        }
        const PieceTree before = snapshot();
        TextModel::clear();
        if (!snapshot().sameAs(before)) {
            clearHistory();
            markClean();
        }
    }

    void HistoryTextModel::insert(size_t offset, const std::wstring& text) {
        if (replaying_) {
            TextModel::insert(offset, text);
            return;
        }
        const PieceTree before = snapshot();
        const size_t start = offset < before.length() ? offset : before.length();
        TextModel::insert(offset, text);
        record(before, start, 0, text);
    }

    void HistoryTextModel::remove(const TextRange& range) {
        if (replaying_) {
            TextModel::remove(range);
            return;
        }
        const PieceTree before = snapshot();
        const size_t start = range.start() < before.length() ? range.start() : before.length();
        const size_t end = range.end() < before.length() ? range.end() : before.length();
        TextModel::remove(range);
        record(before, start, end > start ? end - start : 0, std::wstring());
    }

    void HistoryTextModel::replace(const TextRange& range, const std::wstring& replacement) {
        if (replaying_) {
            TextModel::replace(range, replacement);
            return;
        }
        const PieceTree before = snapshot();
        const size_t start = range.start() < before.length() ? range.start() : before.length();
        const size_t end = range.end() < before.length() ? range.end() : before.length();
        TextModel::replace(range, replacement);
        record(before, start, end > start ? end - start : 0, replacement);
    }

    void HistoryTextModel::record(const PieceTree& before, size_t start, size_t removed, const std::wstring& inserted) {
        PieceTree after = snapshot();
        if (after.sameAs(before)) {
            return;   // vetoed, or nothing to do
        }
        // Whatever path this takes, the history has changed by the time it returns.
        struct Notify {
            HistoryTextModel& model;
            ~Notify() { model.notifyHistoryChanged(); }
        } notify{ *this };
        // A new edit ends the redo history - and the clean state, if that was in it.
        redo_.clear();
        if (cleanDepth_ != npos && cleanDepth_ > undo_.size()) {
            cleanDepth_ = npos;
        }

        if (groupDepth_ > 0) {
            group_.steps.push_back({ before, std::move(after), start, removed, inserted.size() });
            return;
        }
        if (mergeable_ && tryMerge(before, after, start, removed, inserted)) {
            return;
        }
        Item item;
        item.steps.push_back({ before, std::move(after), start, removed, inserted.size() });
        item.lastTyped = removed == 0 && inserted.size() == 1 ? inserted[0] : L'\0';
        push(std::move(item));
        // What can be continued: one typed character (not a break), or one removed.
        mergeable_ = (removed == 0 && inserted.size() == 1 && !isLineBreak(inserted[0]))
            || (removed == 1 && inserted.empty());
    }

    bool HistoryTextModel::tryMerge(const PieceTree& /*before*/, const PieceTree& after, size_t start, size_t removed,
            const std::wstring& inserted) {
        if (undo_.empty() || undo_.back().steps.size() != 1) {
            return false;
        }
        Item& item = undo_.back();
        Step& top = item.steps[0];

        if (removed == 0 && inserted.size() == 1 && !isLineBreak(inserted[0])) {
            // Typing on the end of a typed run of the same kind.
            if (top.removed == 0 && top.inserted > 0 && start == top.start + top.inserted
                    && (item.lastTyped == L'\0' || charClass(item.lastTyped) == charClass(inserted[0]))) {
                ++top.inserted;
                top.after = after;
                item.lastTyped = inserted[0];
                return true;
            }
            return false;
        }
        if (removed == 1 && inserted.empty()) {
            // Backspace over the last character just typed.
            if (top.removed == 0 && top.inserted > 0 && start == top.start + top.inserted - 1) {
                --top.inserted;
                item.lastTyped = L'\0';
                if (top.inserted == 0) {
                    undo_.pop_back();   // typed and taken back: nothing left to undo
                    mergeable_ = false;
                } else {
                    top.after = after;
                }
                return true;
            }
            // More Backspace (one to the left of what's gone) or Delete (at the same place).
            if (top.inserted == 0 && top.removed > 0 && (start == top.start || (top.start > 0 && start == top.start - 1))) {
                if (start != top.start) {
                    top.start = start;
                }
                ++top.removed;
                top.after = after;
                return true;
            }
        }
        return false;
    }

    void HistoryTextModel::push(Item item) {
        undo_.push_back(std::move(item));
        if (undo_.size() > maxSteps_) {
            undo_.erase(undo_.begin());
            if (cleanDepth_ != npos) {
                cleanDepth_ = cleanDepth_ == 0 ? npos : cleanDepth_ - 1;
            }
        }
    }

    bool HistoryTextModel::apply(const Step& step, bool undoing) {
        const PieceTree current = snapshot();
        replaying_ = true;
        if (undoing) {
            TextModel::replace(TextRange(step.start, step.inserted), step.before.substring(step.start, step.removed));
        } else {
            TextModel::replace(TextRange(step.start, step.removed), step.after.substring(step.start, step.inserted));
        }
        replaying_ = false;
        return !snapshot().sameAs(current);   // unchanged: a listener vetoed it
    }

    bool HistoryTextModel::undo(TextRange* affected) {
        if (!canUndo()) {
            return false;
        }
        Item item = std::move(undo_.back());
        undo_.pop_back();
        for (size_t i = item.steps.size(); i-- > 0;) {
            if (!apply(item.steps[i], true)) {
                if (i + 1 == item.steps.size()) {
                    undo_.push_back(std::move(item));   // nothing happened
                } else {
                    clearHistory();                      // half undone: the history no longer fits the text
                }
                return false;
            }
        }
        if (affected != nullptr) {
            *affected = TextRange(item.steps.front().start, item.steps.front().removed);
        }
        redo_.push_back(std::move(item));
        mergeable_ = false;
        notifyHistoryChanged();
        return true;
    }

    bool HistoryTextModel::redo(TextRange* affected) {
        if (!canRedo()) {
            return false;
        }
        Item item = std::move(redo_.back());
        redo_.pop_back();
        for (size_t i = 0; i < item.steps.size(); ++i) {
            if (!apply(item.steps[i], false)) {
                if (i == 0) {
                    redo_.push_back(std::move(item));
                } else {
                    clearHistory();
                }
                return false;
            }
        }
        if (affected != nullptr) {
            *affected = TextRange(item.steps.back().start, item.steps.back().inserted);
        }
        undo_.push_back(std::move(item));
        mergeable_ = false;
        notifyHistoryChanged();
        return true;
    }

    void HistoryTextModel::beginGroup() {
        if (groupDepth_ == 0) {
            group_ = Item();
            mergeable_ = false;
        }
        if (++groupDepth_ == 1) {
            notifyHistoryChanged();   // nothing can be undone until the group ends
        }
    }

    void HistoryTextModel::endGroup() {
        if (groupDepth_ == 0) {
            return;
        }
        if (--groupDepth_ == 0) {
            if (!group_.steps.empty()) {
                push(std::move(group_));
            }
            group_ = Item();
            mergeable_ = false;
            notifyHistoryChanged();
        }
    }

    void HistoryTextModel::clearHistory() {
        undo_.clear();
        redo_.clear();
        group_ = Item();
        groupDepth_ = 0;
        mergeable_ = false;
        cleanDepth_ = npos;
        notifyHistoryChanged();
    }

    void HistoryTextModel::markClean() {
        cleanDepth_ = undo_.size();
        mergeable_ = false;
    }

    bool HistoryTextModel::isClean() const {
        return groupDepth_ == 0 && cleanDepth_ == undo_.size();
    }

    void HistoryTextModel::setMaxUndoSteps(std::size_t steps) {
        maxSteps_ = steps > 0 ? steps : 1;
        while (undo_.size() > maxSteps_) {
            undo_.erase(undo_.begin());
            if (cleanDepth_ != npos) {
                cleanDepth_ = cleanDepth_ == 0 ? npos : cleanDepth_ - 1;
            }
        }
    }

}
