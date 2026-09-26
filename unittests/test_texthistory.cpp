#include "newui/texthistory.h"

#include "newui/clipboardmgr.h"
#include "newui/controls.h"

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

using newui::text::HistoryTextModel;
using newui::text::TextModel;
using newui::text::TextRange;

namespace {
    // Types text one character at a time at the end, the way a keyboard does.
    void type(HistoryTextModel& model, const std::wstring& text) {
        for (wchar_t c : text) {
            model.insert(model.length(), std::wstring(1, c));
        }
    }
}

TEST(HistoryTextModel, StartsWithNothingToUndoOrRedo) {
    HistoryTextModel model(L"hello");
    EXPECT_FALSE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
    EXPECT_FALSE(model.undo());
    EXPECT_FALSE(model.redo());
    EXPECT_EQ(model.text(), L"hello");
    EXPECT_TRUE(model.isClean());
}

TEST(HistoryTextModel, UndoAndRedoPutAnInsertBackAndForth) {
    HistoryTextModel model(L"hello world");
    model.insert(5, L", cruel");
    ASSERT_EQ(model.text(), L"hello, cruel world");

    TextRange affected;
    ASSERT_TRUE(model.undo(&affected));
    EXPECT_EQ(model.text(), L"hello world");
    EXPECT_EQ(affected.start(), 5u);
    EXPECT_EQ(affected.length(), 0u);
    EXPECT_FALSE(model.canUndo());
    EXPECT_TRUE(model.canRedo());

    ASSERT_TRUE(model.redo(&affected));
    EXPECT_EQ(model.text(), L"hello, cruel world");
    EXPECT_EQ(affected.start(), 5u);
    EXPECT_EQ(affected.length(), 7u);
    EXPECT_TRUE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
}

TEST(HistoryTextModel, UndoRestoresRemovedAndReplacedText) {
    HistoryTextModel model(L"one two three");
    model.remove(TextRange(3, 4));
    ASSERT_EQ(model.text(), L"one three");
    model.replace(TextRange(4, 5), L"3");
    ASSERT_EQ(model.text(), L"one 3");

    TextRange affected;
    ASSERT_TRUE(model.undo(&affected));
    EXPECT_EQ(model.text(), L"one three");
    EXPECT_EQ(affected.start(), 4u);
    EXPECT_EQ(affected.length(), 5u);   // "three" is back
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"one two three");
    ASSERT_TRUE(model.redo());
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), L"one 3");
}

TEST(HistoryTextModel, TypingIsOneStepPerRunOfTheSameKindOfCharacter) {
    HistoryTextModel model;
    type(model, L"hello world");
    EXPECT_EQ(model.undoStepCount(), 3u);   // "hello", " ", "world"
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"hello ");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"hello");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"");
    ASSERT_TRUE(model.redo());
    ASSERT_TRUE(model.redo());
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), L"hello world");
}

TEST(HistoryTextModel, ALineBreakIsAStepOfItsOwn) {
    HistoryTextModel model;
    type(model, L"ab\ncd");
    EXPECT_EQ(model.undoStepCount(), 3u);
    model.undo();
    EXPECT_EQ(model.text(), L"ab\n");
    model.undo();
    EXPECT_EQ(model.text(), L"ab");
}

TEST(HistoryTextModel, PastedAndReplacedTextAreNotMergedIntoTyping) {
    HistoryTextModel model;
    type(model, L"abc");
    model.insert(3, L"defg");   // a paste
    type(model, L"h");
    EXPECT_EQ(model.undoStepCount(), 3u);
    model.replace(TextRange(0, 1), L"X");
    EXPECT_EQ(model.undoStepCount(), 4u);
}

TEST(HistoryTextModel, BackspaceRunsAreOneStep) {
    HistoryTextModel model(L"hello world");
    for (int i = 0; i < 5; ++i) {
        model.remove(TextRange(10 - static_cast<size_t>(i), 1));   // backspace from the end
    }
    ASSERT_EQ(model.text(), L"hello ");
    EXPECT_EQ(model.undoStepCount(), 1u);
    TextRange affected;
    ASSERT_TRUE(model.undo(&affected));
    EXPECT_EQ(model.text(), L"hello world");
    EXPECT_EQ(affected.start(), 6u);
    EXPECT_EQ(affected.length(), 5u);
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), L"hello ");
}

TEST(HistoryTextModel, DeleteRunsAreOneStep) {
    HistoryTextModel model(L"hello world");
    for (int i = 0; i < 5; ++i) {
        model.remove(TextRange(6, 1));   // forward Delete at the same place
    }
    ASSERT_EQ(model.text(), L"hello ");
    EXPECT_EQ(model.undoStepCount(), 1u);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"hello world");
}

TEST(HistoryTextModel, BackspaceOverJustTypedTextShrinksTheRunAndAnEmptiedRunLeavesNothing) {
    HistoryTextModel model(L"x");
    type(model, L"abc");   // "xabc"
    model.remove(TextRange(3, 1));
    model.remove(TextRange(2, 1));
    ASSERT_EQ(model.text(), L"xa");
    EXPECT_EQ(model.undoStepCount(), 1u);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"x");

    HistoryTextModel other(L"x");
    type(other, L"a");
    other.remove(TextRange(1, 1));
    EXPECT_EQ(other.text(), L"x");
    EXPECT_EQ(other.undoStepCount(), 0u);
    EXPECT_FALSE(other.canUndo());
}

TEST(HistoryTextModel, BreakCoalescingStartsANewStep) {
    HistoryTextModel model;
    type(model, L"ab");
    model.breakCoalescing();
    type(model, L"cd");
    EXPECT_EQ(model.undoStepCount(), 2u);
    model.undo();
    EXPECT_EQ(model.text(), L"ab");
}

TEST(HistoryTextModel, TypingNotAtTheEndOfTheLastRunIsANewStep) {
    HistoryTextModel model;
    type(model, L"abc");
    model.insert(0, L"X");   // somewhere else
    EXPECT_EQ(model.undoStepCount(), 2u);
}

TEST(HistoryTextModel, AGroupIsOneStepHoweverManyEditsItHolds) {
    HistoryTextModel model(L"a b a");
    model.beginGroup();
    model.replace(TextRange(0, 1), L"X");
    model.replace(TextRange(4, 1), L"X");
    model.beginGroup();   // nested: only the outermost counts
    model.insert(5, L"!");
    model.endGroup();
    EXPECT_FALSE(model.canUndo());   // not until it ends
    model.endGroup();
    ASSERT_EQ(model.text(), L"X b X!");
    EXPECT_EQ(model.undoStepCount(), 1u);

    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"a b a");
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), L"X b X!");
}

TEST(HistoryTextModel, AGroupOfEditsThatDependOnEachOtherUndoesInReverseAndRedoesInOrder) {
    HistoryTextModel model(L"0123456789");
    model.beginGroup();
    model.insert(2, L"abc");        // 01abc23456789
    model.remove(TextRange(0, 4));  // c23456789   - reaches into what was just inserted
    model.insert(3, L"XYZ");        // c23XYZ456789
    model.endGroup();
    ASSERT_EQ(model.text(), L"c23XYZ456789");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"0123456789");
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), L"c23XYZ456789");
}

TEST(HistoryTextModel, AnEmptyGroupAddsNothing) {
    HistoryTextModel model(L"a");
    model.beginGroup();
    model.endGroup();
    EXPECT_FALSE(model.canUndo());
}

TEST(HistoryTextModel, ANewEditEndsTheRedoHistory) {
    HistoryTextModel model(L"abc");
    model.insert(3, L"1");
    model.breakCoalescing();
    model.insert(4, L"2");
    model.undo();
    ASSERT_TRUE(model.canRedo());
    model.insert(0, L"Z");
    EXPECT_FALSE(model.canRedo());
    EXPECT_EQ(model.text(), L"Zabc1");
}

TEST(HistoryTextModel, TypingAfterAnUndoIsANewStepNotAMergeIntoTheOldOne) {
    HistoryTextModel model;
    type(model, L"abc");
    model.undo();
    type(model, L"xyz");
    EXPECT_EQ(model.text(), L"xyz");
    EXPECT_EQ(model.undoStepCount(), 1u);
    model.undo();
    EXPECT_EQ(model.text(), L"");
    EXPECT_FALSE(model.canUndo());
    model.redo();
    EXPECT_EQ(model.text(), L"xyz");
}

TEST(HistoryTextModel, SetTextAndClearForgetTheHistory) {
    HistoryTextModel model(L"abc");
    model.insert(3, L"d");
    ASSERT_TRUE(model.canUndo());
    model.setText(L"loaded");
    EXPECT_FALSE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
    EXPECT_TRUE(model.isClean());

    model.insert(6, L"!");
    model.undo();
    ASSERT_TRUE(model.canRedo());
    model.clear();
    EXPECT_FALSE(model.canRedo());
    EXPECT_FALSE(model.canUndo());
    EXPECT_EQ(model.text(), L"");
}

TEST(HistoryTextModel, CleanStateFollowsSaveEditUndoRedo) {
    HistoryTextModel model(L"abc");
    EXPECT_TRUE(model.isClean());
    model.insert(3, L"d");
    EXPECT_FALSE(model.isClean());
    model.markClean();
    EXPECT_TRUE(model.isClean());

    model.insert(4, L"e");   // not merged into the saved step
    EXPECT_EQ(model.undoStepCount(), 2u);
    EXPECT_FALSE(model.isClean());
    model.undo();
    EXPECT_TRUE(model.isClean());
    model.undo();
    EXPECT_FALSE(model.isClean());
    model.redo();
    EXPECT_TRUE(model.isClean());

    // Edit in place of the redo history that held the clean state: it can't come back.
    model.undo();
    model.undo();
    model.markClean();   // clean at the very start now
    model.redo();
    EXPECT_FALSE(model.isClean());
    model.undo();
    EXPECT_TRUE(model.isClean());
    model.insert(0, L"Z");
    EXPECT_FALSE(model.isClean());
    model.undo();
    EXPECT_TRUE(model.isClean());

    HistoryTextModel lost(L"abc");
    lost.insert(3, L"d");
    lost.markClean();
    lost.undo();
    lost.insert(0, L"Q");   // the saved state was in the redo history
    lost.undo();
    EXPECT_FALSE(lost.isClean());
}

TEST(HistoryTextModel, UndoAndRedoFireTheOrdinaryEditEvents) {
    HistoryTextModel model(L"hello world");
    model.replace(TextRange(6, 5), L"there");

    std::vector<std::wstring> seen;
    int changed = 0;
    model.onAfterRangeChanged.add([&](TextModel&, const TextRange& range, const std::wstring& replacement) {
        seen.push_back(std::to_wstring(range.start()) + L"+" + std::to_wstring(range.length()) + L"->" + replacement);
        return newui::SyncReturn::Ignored;
    });
    model.onChanged.add([&](newui::Model&) { ++changed; return newui::SyncReturn::Ignored; });

    model.undo();
    model.redo();
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], L"6+5->world");
    EXPECT_EQ(seen[1], L"6+5->there");
    EXPECT_EQ(changed, 2);
}

TEST(HistoryTextModel, AVetoedEditLeavesNoStep) {
    HistoryTextModel model(L"abc");
    model.onBeforeChar.add([](TextModel&, size_t, wchar_t, newui::text::CharChangeKind, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
    });
    model.insert(3, L"d");
    EXPECT_EQ(model.text(), L"abc");
    EXPECT_FALSE(model.canUndo());
}

TEST(HistoryTextModel, AVetoedUndoKeepsTheStep) {
    HistoryTextModel model(L"abc");
    model.insert(3, L"defg");
    bool allow = false;
    model.onBeforeRangeChanged.add([&](TextModel&, const TextRange&, const std::wstring&, bool& canChange) {
        canChange = allow;
        return newui::SyncReturn::Handled;
    });
    EXPECT_FALSE(model.undo());
    EXPECT_EQ(model.text(), L"abcdefg");
    EXPECT_TRUE(model.canUndo());
    allow = true;
    EXPECT_TRUE(model.undo());
    EXPECT_EQ(model.text(), L"abc");
}

TEST(HistoryTextModel, TheOldestStepsAreDroppedBeyondTheLimit) {
    HistoryTextModel model;
    model.setMaxUndoSteps(3);
    for (int i = 0; i < 6; ++i) {
        model.insert(model.length(), L"ab");   // pasted-size, so no merging
    }
    EXPECT_EQ(model.undoStepCount(), 3u);
    while (model.undo()) {
    }
    EXPECT_EQ(model.text(), L"ababab");   // the three oldest can't be undone
}

TEST(HistoryTextModel, TheModelSnapshotIsUnaffectedByLaterEdits) {
    HistoryTextModel model(L"snap");
    const newui::text::PieceTree snapshot = model.snapshot();
    model.insert(4, L"shot");
    model.undo();
    model.redo();
    EXPECT_EQ(snapshot.str(), L"snap");
    EXPECT_EQ(model.text(), L"snapshot");
}

TEST(HistoryTextModel, RandomEditsUndoAllTheWayBackAndRedoAllTheWayForward) {
    static const wchar_t alphabet[] = L"ab \n\r_.";
    for (unsigned seed = 1; seed <= 20; ++seed) {
        std::mt19937 rng(seed);
        auto randomText = [&](size_t n) {
            std::wstring s;
            for (size_t i = 0; i < n; ++i) {
                s += alphabet[rng() % (sizeof(alphabet) / sizeof(wchar_t) - 1)];
            }
            return s;
        };
        const std::wstring initial = randomText(rng() % 60);
        HistoryTextModel model(initial);
        std::wstring reference = initial;
        for (int step = 0; step < 300; ++step) {
            const size_t at = reference.empty() ? 0 : rng() % (reference.size() + 1);
            switch (rng() % 8) {
            case 0: case 1: case 2: {   // typing at the end or at a place
                const size_t where = rng() % 2 ? reference.size() : at;
                const std::wstring c = randomText(1);
                model.insert(where, c);
                reference.insert(where, c);
                break;
            }
            case 3: {   // backspace / delete
                if (!reference.empty()) {
                    const size_t where = at < reference.size() ? at : reference.size() - 1;
                    model.remove(TextRange(where, 1));
                    reference.erase(where, 1);
                }
                break;
            }
            case 4: {   // paste
                const std::wstring s = randomText(1 + rng() % 20);
                model.insert(at, s);
                reference.insert(at, s);
                break;
            }
            case 5: {   // replace
                const size_t n = rng() % 6;
                const std::wstring s = randomText(rng() % 6);
                model.replace(TextRange(at, n), s);
                reference.replace(at, n < reference.size() - at ? n : reference.size() - at, s);
                break;
            }
            case 6:
                model.breakCoalescing();
                break;
            default: {   // a group of edits
                model.beginGroup();
                for (int i = 0; i < 3; ++i) {
                    const size_t where = reference.empty() ? 0 : rng() % (reference.size() + 1);
                    const std::wstring s = randomText(1 + rng() % 3);
                    model.insert(where, s);
                    reference.insert(where, s);
                }
                model.endGroup();
                break;
            }
            }
            ASSERT_EQ(model.text(), reference) << "seed " << seed << " step " << step;
        }

        // A walk up and down the history: each undo is undone by a redo, exactly.
        std::vector<std::wstring> states;   // states[i]: the text with i steps undone
        states.push_back(reference);
        while (model.undo()) {
            states.push_back(model.text());
        }
        ASSERT_EQ(model.text(), initial) << "seed " << seed;
        for (size_t i = states.size() - 1; i-- > 0;) {
            ASSERT_TRUE(model.redo());
            ASSERT_EQ(model.text(), states[i]) << "seed " << seed << " redo to " << i;
        }
        ASSERT_EQ(model.text(), reference);
        EXPECT_FALSE(model.canRedo());
    }
}

TEST(TextControlUndo, UndoAndRedoGoThroughTheModelAndPlaceTheCaretAtTheEndOfTheRestoredText) {
    newui::TextControl control;
    control.setModel(std::make_unique<HistoryTextModel>());
    control.setText(L"hello world");
    EXPECT_FALSE(control.canUndo());

    control.model().remove(TextRange(5, 6));   // " world"
    ASSERT_EQ(control.text(), L"hello");
    EXPECT_TRUE(control.canUndo());
    control.caret().setPosition(newui::text::TextPosition(0));

    ASSERT_TRUE(control.undo());
    EXPECT_EQ(control.text(), L"hello world");
    EXPECT_EQ(control.caret().position().offset(), 11u);   // after what came back
    EXPECT_TRUE(control.canRedo());

    ASSERT_TRUE(control.redo());
    EXPECT_EQ(control.text(), L"hello");
    EXPECT_EQ(control.caret().position().offset(), 5u);
    EXPECT_FALSE(control.undo() && control.undo());   // only one step to undo
}

TEST(TextControlUndo, APlainTextModelHasNoHistory) {
    newui::TextControl control;
    control.setText(L"abc");
    control.model().insert(3, L"d");
    EXPECT_FALSE(control.canUndo());
    EXPECT_FALSE(control.undo());
    EXPECT_FALSE(control.redo());
    EXPECT_EQ(control.text(), L"abcd");
}

namespace {
    std::wstring clipboardText() {
        std::wstring out;
        newui::ClipboardManager::getText(out);
        return out;
    }
}

TEST(TextControlClipboard, CopyPutsTheSelectionOnTheClipboardAndLeavesTheText) {
    newui::TextControl control;
    control.setText(L"hello world");
    EXPECT_FALSE(control.copy());   // nothing selected
    control.selection().setRange(TextRange(6, 5));
    ASSERT_TRUE(control.copy());
    EXPECT_EQ(clipboardText(), L"world");
    EXPECT_EQ(control.text(), L"hello world");
}

TEST(TextControlClipboard, CutRemovesTheSelectionAndPlacesTheCaretWhereItWas) {
    newui::TextControl control;
    control.setModel(std::make_unique<HistoryTextModel>());
    control.setText(L"hello world");
    control.selection().setRange(TextRange(0, 6));
    ASSERT_TRUE(control.cut());
    EXPECT_EQ(clipboardText(), L"hello ");
    EXPECT_EQ(control.text(), L"world");
    EXPECT_EQ(control.caret().position().offset(), 0u);
    EXPECT_TRUE(control.selection().isEmpty());
    ASSERT_TRUE(control.undo());   // one undo step
    EXPECT_EQ(control.text(), L"hello world");
}

TEST(TextControlClipboard, PasteReplacesTheSelectionOrInsertsAtTheCaretAndIsOneUndoStep) {
    newui::TextControl control;
    control.setModel(std::make_unique<HistoryTextModel>());
    ASSERT_TRUE(newui::ClipboardManager::setText(L"XYZ"));
    control.setText(L"abcdef");
    control.selection().setRange(TextRange(1, 2));
    ASSERT_TRUE(control.paste());
    EXPECT_EQ(control.text(), L"aXYZdef");
    EXPECT_EQ(control.caret().position().offset(), 4u);

    control.caret().setPosition(newui::text::TextPosition(7));
    ASSERT_TRUE(control.paste());
    EXPECT_EQ(control.text(), L"aXYZdefXYZ");

    ASSERT_TRUE(control.undo());
    EXPECT_EQ(control.text(), L"aXYZdef");
    ASSERT_TRUE(control.undo());
    EXPECT_EQ(control.text(), L"abcdef");
}

TEST(TextControlClipboard, PasteIntoASingleLineFieldTurnsLineBreaksIntoSpaces) {
    newui::TextField field;
    ASSERT_TRUE(newui::ClipboardManager::setText(L"a\r\nb\nc\rd"));
    ASSERT_TRUE(field.paste());
    EXPECT_EQ(field.text(), L"a b c d");
}

TEST(TextControlClipboard, PasteWithNoTextOnTheClipboardDoesNothing) {
    newui::TextControl control;
    control.setText(L"abc");
    ASSERT_TRUE(newui::ClipboardManager::setText(L""));
    EXPECT_FALSE(control.paste());
    EXPECT_EQ(control.text(), L"abc");
}

TEST(TextControlClipboard, SecureEntryNeverCopiesAndReadOnlyNeverChanges) {
    newui::TextControl secret;
    secret.setText(L"hunter2");
    secret.selection().setRange(TextRange(0, 7));
    ASSERT_TRUE(newui::ClipboardManager::setText(L"untouched"));
    secret.inputTraits().setSecureTextEntry(true);
    EXPECT_FALSE(secret.copy());
    EXPECT_FALSE(secret.cut());
    EXPECT_EQ(clipboardText(), L"untouched");
    EXPECT_EQ(secret.text(), L"hunter2");

    newui::TextControl locked;
    locked.setText(L"fixed");
    locked.selection().setRange(TextRange(0, 5));
    locked.inputTraits().setReadOnly(true);
    EXPECT_TRUE(locked.copy());          // reading is fine
    EXPECT_FALSE(locked.cut());
    EXPECT_FALSE(locked.paste());
    EXPECT_EQ(locked.text(), L"fixed");
}

TEST(HistoryTextModel, OnHistoryChangedFiresAfterTheHistoryIsUpdatedNotBefore) {
    HistoryTextModel model(L"abc");
    struct Seen { bool canUndo; bool canRedo; };
    std::vector<Seen> seen;
    model.onHistoryChanged.add([&](TextModel& sender) {
        seen.push_back({ sender.canUndo(), sender.canRedo() });
        return newui::SyncReturn::Ignored;
    });
    // onChanged is the one that comes first, and sees the answers before the step is recorded.
    bool undoableInOnChanged = true;
    model.onChanged.add([&](newui::Model&) {
        undoableInOnChanged = model.canUndo();
        return newui::SyncReturn::Ignored;
    });

    model.insert(3, L"d");
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_TRUE(seen[0].canUndo);
    EXPECT_FALSE(seen[0].canRedo);
    EXPECT_FALSE(undoableInOnChanged) << "why onHistoryChanged exists";

    model.undo();
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_FALSE(seen[1].canUndo);
    EXPECT_TRUE(seen[1].canRedo);

    model.redo();
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_TRUE(seen[2].canUndo);

    model.setText(L"loaded");   // the history is forgotten
    ASSERT_GE(seen.size(), 4u);
    EXPECT_FALSE(seen.back().canUndo);
    EXPECT_FALSE(seen.back().canRedo);

    // A group: nothing to undo until it ends.
    seen.clear();
    model.beginGroup();
    ASSERT_EQ(seen.size(), 1u);
    model.insert(0, L"x");
    model.endGroup();
    EXPECT_TRUE(seen.back().canUndo);

    // A vetoed edit changes nothing.
    seen.clear();
    model.onBeforeChar.add([](TextModel&, size_t, wchar_t, newui::text::CharChangeKind, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
    });
    model.insert(0, L"y");
    EXPECT_TRUE(seen.empty());
}
