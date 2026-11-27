#include "newui/texthistory.h"
#include "newui/win32/winheaders.h"
#include "newui/utils.h"

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
    void type(HistoryTextModel& model, const std::u16string& text) {
        for (char16_t c : text) {
            model.insert(model.length(), std::u16string(1, c));
        }
    }
}

TEST(HistoryTextModel, StartsWithNothingToUndoOrRedo) {
    HistoryTextModel model(u"hello");
    EXPECT_FALSE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
    EXPECT_FALSE(model.undo());
    EXPECT_FALSE(model.redo());
    EXPECT_EQ(model.text(), u"hello");
    EXPECT_TRUE(model.isClean());
}

TEST(HistoryTextModel, UndoAndRedoPutAnInsertBackAndForth) {
    HistoryTextModel model(u"hello world");
    model.insert(5, u", cruel");
    ASSERT_EQ(model.text(), u"hello, cruel world");

    TextRange affected;
    ASSERT_TRUE(model.undo(&affected));
    EXPECT_EQ(model.text(), u"hello world");
    EXPECT_EQ(affected.start(), 5u);
    EXPECT_EQ(affected.length(), 0u);
    EXPECT_FALSE(model.canUndo());
    EXPECT_TRUE(model.canRedo());

    ASSERT_TRUE(model.redo(&affected));
    EXPECT_EQ(model.text(), u"hello, cruel world");
    EXPECT_EQ(affected.start(), 5u);
    EXPECT_EQ(affected.length(), 7u);
    EXPECT_TRUE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
}

TEST(HistoryTextModel, UndoRestoresRemovedAndReplacedText) {
    HistoryTextModel model(u"one two three");
    model.remove(TextRange(3, 4));
    ASSERT_EQ(model.text(), u"one three");
    model.replace(TextRange(4, 5), u"3");
    ASSERT_EQ(model.text(), u"one 3");

    TextRange affected;
    ASSERT_TRUE(model.undo(&affected));
    EXPECT_EQ(model.text(), u"one three");
    EXPECT_EQ(affected.start(), 4u);
    EXPECT_EQ(affected.length(), 5u);   // "three" is back
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"one two three");
    ASSERT_TRUE(model.redo());
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), u"one 3");
}

TEST(HistoryTextModel, TypingIsOneStepPerRunOfTheSameKindOfCharacter) {
    HistoryTextModel model;
    type(model, u"hello world");
    EXPECT_EQ(model.undoStepCount(), 3u);   // "hello", " ", "world"
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"hello ");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"hello");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"");
    ASSERT_TRUE(model.redo());
    ASSERT_TRUE(model.redo());
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), u"hello world");
}

TEST(HistoryTextModel, ALineBreakIsAStepOfItsOwn) {
    HistoryTextModel model;
    type(model, u"ab\ncd");
    EXPECT_EQ(model.undoStepCount(), 3u);
    model.undo();
    EXPECT_EQ(model.text(), u"ab\n");
    model.undo();
    EXPECT_EQ(model.text(), u"ab");
}

TEST(HistoryTextModel, PastedAndReplacedTextAreNotMergedIntoTyping) {
    HistoryTextModel model;
    type(model, u"abc");
    model.insert(3, u"defg");   // a paste
    type(model, u"h");
    EXPECT_EQ(model.undoStepCount(), 3u);
    model.replace(TextRange(0, 1), u"X");
    EXPECT_EQ(model.undoStepCount(), 4u);
}

TEST(HistoryTextModel, BackspaceRunsAreOneStep) {
    HistoryTextModel model(u"hello world");
    for (int i = 0; i < 5; ++i) {
        model.remove(TextRange(10 - static_cast<size_t>(i), 1));   // backspace from the end
    }
    ASSERT_EQ(model.text(), u"hello ");
    EXPECT_EQ(model.undoStepCount(), 1u);
    TextRange affected;
    ASSERT_TRUE(model.undo(&affected));
    EXPECT_EQ(model.text(), u"hello world");
    EXPECT_EQ(affected.start(), 6u);
    EXPECT_EQ(affected.length(), 5u);
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), u"hello ");
}

TEST(HistoryTextModel, DeleteRunsAreOneStep) {
    HistoryTextModel model(u"hello world");
    for (int i = 0; i < 5; ++i) {
        model.remove(TextRange(6, 1));   // forward Delete at the same place
    }
    ASSERT_EQ(model.text(), u"hello ");
    EXPECT_EQ(model.undoStepCount(), 1u);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"hello world");
}

TEST(HistoryTextModel, BackspaceOverJustTypedTextShrinksTheRunAndAnEmptiedRunLeavesNothing) {
    HistoryTextModel model(u"x");
    type(model, u"abc");   // "xabc"
    model.remove(TextRange(3, 1));
    model.remove(TextRange(2, 1));
    ASSERT_EQ(model.text(), u"xa");
    EXPECT_EQ(model.undoStepCount(), 1u);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"x");

    HistoryTextModel other(u"x");
    type(other, u"a");
    other.remove(TextRange(1, 1));
    EXPECT_EQ(other.text(), u"x");
    EXPECT_EQ(other.undoStepCount(), 0u);
    EXPECT_FALSE(other.canUndo());
}

TEST(HistoryTextModel, BreakCoalescingStartsANewStep) {
    HistoryTextModel model;
    type(model, u"ab");
    model.breakCoalescing();
    type(model, u"cd");
    EXPECT_EQ(model.undoStepCount(), 2u);
    model.undo();
    EXPECT_EQ(model.text(), u"ab");
}

TEST(HistoryTextModel, TypingNotAtTheEndOfTheLastRunIsANewStep) {
    HistoryTextModel model;
    type(model, u"abc");
    model.insert(0, u"X");   // somewhere else
    EXPECT_EQ(model.undoStepCount(), 2u);
}

TEST(HistoryTextModel, AGroupIsOneStepHoweverManyEditsItHolds) {
    HistoryTextModel model(u"a b a");
    model.beginGroup();
    model.replace(TextRange(0, 1), u"X");
    model.replace(TextRange(4, 1), u"X");
    model.beginGroup();   // nested: only the outermost counts
    model.insert(5, u"!");
    model.endGroup();
    EXPECT_FALSE(model.canUndo());   // not until it ends
    model.endGroup();
    ASSERT_EQ(model.text(), u"X b X!");
    EXPECT_EQ(model.undoStepCount(), 1u);

    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"a b a");
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), u"X b X!");
}

TEST(HistoryTextModel, AGroupOfEditsThatDependOnEachOtherUndoesInReverseAndRedoesInOrder) {
    HistoryTextModel model(u"0123456789");
    model.beginGroup();
    model.insert(2, u"abc");        // 01abc23456789
    model.remove(TextRange(0, 4));  // c23456789   - reaches into what was just inserted
    model.insert(3, u"XYZ");        // c23XYZ456789
    model.endGroup();
    ASSERT_EQ(model.text(), u"c23XYZ456789");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"0123456789");
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), u"c23XYZ456789");
}

TEST(HistoryTextModel, AnEmptyGroupAddsNothing) {
    HistoryTextModel model(u"a");
    model.beginGroup();
    model.endGroup();
    EXPECT_FALSE(model.canUndo());
}

TEST(HistoryTextModel, ANewEditEndsTheRedoHistory) {
    HistoryTextModel model(u"abc");
    model.insert(3, u"1");
    model.breakCoalescing();
    model.insert(4, u"2");
    model.undo();
    ASSERT_TRUE(model.canRedo());
    model.insert(0, u"Z");
    EXPECT_FALSE(model.canRedo());
    EXPECT_EQ(model.text(), u"Zabc1");
}

TEST(HistoryTextModel, TypingAfterAnUndoIsANewStepNotAMergeIntoTheOldOne) {
    HistoryTextModel model;
    type(model, u"abc");
    model.undo();
    type(model, u"xyz");
    EXPECT_EQ(model.text(), u"xyz");
    EXPECT_EQ(model.undoStepCount(), 1u);
    model.undo();
    EXPECT_EQ(model.text(), u"");
    EXPECT_FALSE(model.canUndo());
    model.redo();
    EXPECT_EQ(model.text(), u"xyz");
}

TEST(HistoryTextModel, SetTextAndClearForgetTheHistory) {
    HistoryTextModel model(u"abc");
    model.insert(3, u"d");
    ASSERT_TRUE(model.canUndo());
    model.setText(u"loaded");
    EXPECT_FALSE(model.canUndo());
    EXPECT_FALSE(model.canRedo());
    EXPECT_TRUE(model.isClean());

    model.insert(6, u"!");
    model.undo();
    ASSERT_TRUE(model.canRedo());
    model.clear();
    EXPECT_FALSE(model.canRedo());
    EXPECT_FALSE(model.canUndo());
    EXPECT_EQ(model.text(), u"");
}

TEST(HistoryTextModel, CleanStateFollowsSaveEditUndoRedo) {
    HistoryTextModel model(u"abc");
    EXPECT_TRUE(model.isClean());
    model.insert(3, u"d");
    EXPECT_FALSE(model.isClean());
    model.markClean();
    EXPECT_TRUE(model.isClean());

    model.insert(4, u"e");   // not merged into the saved step
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
    model.insert(0, u"Z");
    EXPECT_FALSE(model.isClean());
    model.undo();
    EXPECT_TRUE(model.isClean());

    HistoryTextModel lost(u"abc");
    lost.insert(3, u"d");
    lost.markClean();
    lost.undo();
    lost.insert(0, u"Q");   // the saved state was in the redo history
    lost.undo();
    EXPECT_FALSE(lost.isClean());
}

TEST(HistoryTextModel, UndoAndRedoFireTheOrdinaryEditEvents) {
    HistoryTextModel model(u"hello world");
    model.replace(TextRange(6, 5), u"there");

    std::vector<std::u16string> seen;
    int changed = 0;
    model.onAfterRangeChanged.add([&](TextModel&, const TextRange& range, const std::u16string& replacement) {
        seen.push_back(newui::utf8ToUtf16(std::to_string(range.start())) + u"+" + newui::utf8ToUtf16(std::to_string(range.length())) + u"->" + replacement);
        return newui::SyncReturn::Ignored;
    });
    model.onChanged.add([&](newui::Model&) { ++changed; return newui::SyncReturn::Ignored; });

    model.undo();
    model.redo();
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], u"6+5->world");
    EXPECT_EQ(seen[1], u"6+5->there");
    EXPECT_EQ(changed, 2);
}

TEST(HistoryTextModel, AVetoedEditLeavesNoStep) {
    HistoryTextModel model(u"abc");
    model.onBeforeChar.add([](TextModel&, size_t, char16_t, newui::text::CharChangeKind, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
    });
    model.insert(3, u"d");
    EXPECT_EQ(model.text(), u"abc");
    EXPECT_FALSE(model.canUndo());
}

TEST(HistoryTextModel, AVetoedUndoKeepsTheStep) {
    HistoryTextModel model(u"abc");
    model.insert(3, u"defg");
    bool allow = false;
    model.onBeforeRangeChanged.add([&](TextModel&, const TextRange&, const std::u16string&, bool& canChange) {
        canChange = allow;
        return newui::SyncReturn::Handled;
    });
    EXPECT_FALSE(model.undo());
    EXPECT_EQ(model.text(), u"abcdefg");
    EXPECT_TRUE(model.canUndo());
    allow = true;
    EXPECT_TRUE(model.undo());
    EXPECT_EQ(model.text(), u"abc");
}

TEST(HistoryTextModel, TheOldestStepsAreDroppedBeyondTheLimit) {
    HistoryTextModel model;
    model.setMaxUndoSteps(3);
    for (int i = 0; i < 6; ++i) {
        model.insert(model.length(), u"ab");   // pasted-size, so no merging
    }
    EXPECT_EQ(model.undoStepCount(), 3u);
    while (model.undo()) {
    }
    EXPECT_EQ(model.text(), u"ababab");   // the three oldest can't be undone
}

TEST(HistoryTextModel, TheModelSnapshotIsUnaffectedByLaterEdits) {
    HistoryTextModel model(u"snap");
    const newui::text::PieceTree snapshot = model.snapshot();
    model.insert(4, u"shot");
    model.undo();
    model.redo();
    EXPECT_EQ(snapshot.str(), u"snap");
    EXPECT_EQ(model.text(), u"snapshot");
}

TEST(HistoryTextModel, RandomEditsUndoAllTheWayBackAndRedoAllTheWayForward) {
    static const char16_t alphabet[] = u"ab \n\r_.";
    for (unsigned seed = 1; seed <= 20; ++seed) {
        std::mt19937 rng(seed);
        auto randomText = [&](size_t n) {
            std::u16string s;
            for (size_t i = 0; i < n; ++i) {
                s += alphabet[rng() % (sizeof(alphabet) / sizeof(char16_t) - 1)];
            }
            return s;
        };
        const std::u16string initial = randomText(rng() % 60);
        HistoryTextModel model(initial);
        std::u16string reference = initial;
        for (int step = 0; step < 300; ++step) {
            const size_t at = reference.empty() ? 0 : rng() % (reference.size() + 1);
            switch (rng() % 8) {
            case 0: case 1: case 2: {   // typing at the end or at a place
                const size_t where = rng() % 2 ? reference.size() : at;
                const std::u16string c = randomText(1);
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
                const std::u16string s = randomText(1 + rng() % 20);
                model.insert(at, s);
                reference.insert(at, s);
                break;
            }
            case 5: {   // replace
                const size_t n = rng() % 6;
                const std::u16string s = randomText(rng() % 6);
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
                    const std::u16string s = randomText(1 + rng() % 3);
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
        std::vector<std::u16string> states;   // states[i]: the text with i steps undone
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
    control.setText(u"hello world");
    EXPECT_FALSE(control.canUndo());

    control.model().remove(TextRange(5, 6));   // " world"
    ASSERT_EQ(control.text(), u"hello");
    EXPECT_TRUE(control.canUndo());
    control.caret().setPosition(newui::text::TextPosition(0));

    ASSERT_TRUE(control.undo());
    EXPECT_EQ(control.text(), u"hello world");
    EXPECT_EQ(control.caret().position().offset(), 11u);   // after what came back
    EXPECT_TRUE(control.canRedo());

    ASSERT_TRUE(control.redo());
    EXPECT_EQ(control.text(), u"hello");
    EXPECT_EQ(control.caret().position().offset(), 5u);
    EXPECT_FALSE(control.undo() && control.undo());   // only one step to undo
}

TEST(TextControlUndo, APlainTextModelHasNoHistory) {
    newui::TextControl control;
    control.setText(u"abc");
    control.model().insert(3, u"d");
    EXPECT_FALSE(control.canUndo());
    EXPECT_FALSE(control.undo());
    EXPECT_FALSE(control.redo());
    EXPECT_EQ(control.text(), u"abcd");
}

namespace {
    std::u16string clipboardText() {
        std::u16string out;
        newui::ClipboardManager::getText(out);
        return out;
    }
}

TEST(TextControlClipboard, CopyPutsTheSelectionOnTheClipboardAndLeavesTheText) {
    newui::TextControl control;
    control.setText(u"hello world");
    EXPECT_FALSE(control.copy());   // nothing selected
    control.selection().setRange(TextRange(6, 5));
    ASSERT_TRUE(control.copy());
    EXPECT_EQ(clipboardText(), u"world");
    EXPECT_EQ(control.text(), u"hello world");
}

TEST(TextControlClipboard, CutRemovesTheSelectionAndPlacesTheCaretWhereItWas) {
    newui::TextControl control;
    control.setModel(std::make_unique<HistoryTextModel>());
    control.setText(u"hello world");
    control.selection().setRange(TextRange(0, 6));
    ASSERT_TRUE(control.cut());
    EXPECT_EQ(clipboardText(), u"hello ");
    EXPECT_EQ(control.text(), u"world");
    EXPECT_EQ(control.caret().position().offset(), 0u);
    EXPECT_TRUE(control.selection().isEmpty());
    ASSERT_TRUE(control.undo());   // one undo step
    EXPECT_EQ(control.text(), u"hello world");
}

TEST(TextControlClipboard, PasteReplacesTheSelectionOrInsertsAtTheCaretAndIsOneUndoStep) {
    newui::TextControl control;
    control.setModel(std::make_unique<HistoryTextModel>());
    ASSERT_TRUE(newui::ClipboardManager::setText(u"XYZ"));
    control.setText(u"abcdef");
    control.selection().setRange(TextRange(1, 2));
    ASSERT_TRUE(control.paste());
    EXPECT_EQ(control.text(), u"aXYZdef");
    EXPECT_EQ(control.caret().position().offset(), 4u);

    control.caret().setPosition(newui::text::TextPosition(7));
    ASSERT_TRUE(control.paste());
    EXPECT_EQ(control.text(), u"aXYZdefXYZ");

    ASSERT_TRUE(control.undo());
    EXPECT_EQ(control.text(), u"aXYZdef");
    ASSERT_TRUE(control.undo());
    EXPECT_EQ(control.text(), u"abcdef");
}

TEST(TextControlClipboard, PasteIntoASingleLineFieldTurnsLineBreaksIntoSpaces) {
    newui::TextField field;
    ASSERT_TRUE(newui::ClipboardManager::setText(u"a\r\nb\nc\rd"));
    ASSERT_TRUE(field.paste());
    EXPECT_EQ(field.text(), u"a b c d");
}

TEST(TextControlClipboard, PasteWithNoTextOnTheClipboardDoesNothing) {
    newui::TextControl control;
    control.setText(u"abc");
    ASSERT_TRUE(newui::ClipboardManager::setText(u""));
    EXPECT_FALSE(control.paste());
    EXPECT_EQ(control.text(), u"abc");
}

TEST(TextControlClipboard, SecureEntryNeverCopiesAndReadOnlyNeverChanges) {
    newui::TextControl secret;
    secret.setText(u"hunter2");
    secret.selection().setRange(TextRange(0, 7));
    ASSERT_TRUE(newui::ClipboardManager::setText(u"untouched"));
    secret.inputTraits().setSecureTextEntry(true);
    EXPECT_FALSE(secret.copy());
    EXPECT_FALSE(secret.cut());
    EXPECT_EQ(clipboardText(), u"untouched");
    EXPECT_EQ(secret.text(), u"hunter2");

    newui::TextControl locked;
    locked.setText(u"fixed");
    locked.selection().setRange(TextRange(0, 5));
    locked.inputTraits().setReadOnly(true);
    EXPECT_TRUE(locked.copy());          // reading is fine
    EXPECT_FALSE(locked.cut());
    EXPECT_FALSE(locked.paste());
    EXPECT_EQ(locked.text(), u"fixed");
}

TEST(HistoryTextModel, OnHistoryChangedFiresAfterTheHistoryIsUpdatedNotBefore) {
    HistoryTextModel model(u"abc");
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

    model.insert(3, u"d");
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

    model.setText(u"loaded");   // the history is forgotten
    ASSERT_GE(seen.size(), 4u);
    EXPECT_FALSE(seen.back().canUndo);
    EXPECT_FALSE(seen.back().canRedo);

    // A group: nothing to undo until it ends.
    seen.clear();
    model.beginGroup();
    ASSERT_EQ(seen.size(), 1u);
    model.insert(0, u"x");
    model.endGroup();
    EXPECT_TRUE(seen.back().canUndo);

    // A vetoed edit changes nothing.
    seen.clear();
    model.onBeforeChar.add([](TextModel&, size_t, char16_t, newui::text::CharChangeKind, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
    });
    model.insert(0, u"y");
    EXPECT_TRUE(seen.empty());
}
