#include "newui/action.h"
#include "newui/actioncontext.h"

#include <gtest/gtest.h>

namespace {

struct Contexts {
    newui::ActionContext global = newui::ActionContext::global();
    newui::ActionContext canvas{"canvas"};
    newui::ActionContext trace{"trace"};
    newui::ActionContext textInput{"textInput"};
    newui::ActionContextTree tree;

    Contexts() {
        tree.setParent(canvas, global);
        tree.setParent(trace, canvas);
    }
};

constexpr std::uint32_t kKey = static_cast<std::uint32_t>(newui::vkF1);

}

TEST(ActionContext, EqualityIsByName) {
    EXPECT_EQ(newui::ActionContext("canvas"), newui::ActionContext("canvas"));
    EXPECT_NE(newui::ActionContext("canvas"), newui::ActionContext("graph"));
    EXPECT_EQ(newui::ActionContext(), newui::ActionContext::global());
}

TEST(ActionContextTree, DepthAndInheritance) {
    Contexts c;
    EXPECT_EQ(c.tree.depth(c.global), 0);
    EXPECT_EQ(c.tree.depth(c.canvas), 1);
    EXPECT_EQ(c.tree.depth(c.trace), 2);
    EXPECT_EQ(c.tree.depth(c.textInput), 1);

    EXPECT_TRUE(c.tree.inherits(c.trace, c.canvas));
    EXPECT_TRUE(c.tree.inherits(c.trace, c.global));
    EXPECT_TRUE(c.tree.inherits(c.trace, c.trace));
    EXPECT_FALSE(c.tree.inherits(c.canvas, c.trace));
    EXPECT_FALSE(c.tree.inherits(c.textInput, c.canvas));
}

TEST(ActionContextTree, OverlapIsAncestryEitherWay) {
    Contexts c;
    EXPECT_TRUE(c.tree.overlaps(c.trace, c.canvas));
    EXPECT_TRUE(c.tree.overlaps(c.canvas, c.trace));
    EXPECT_TRUE(c.tree.overlaps(c.canvas, c.global));
    EXPECT_FALSE(c.tree.overlaps(c.canvas, c.textInput));
}

TEST(ActionContextTree, ParentCycleTerminates) {
    newui::ActionContext a("a");
    newui::ActionContext b("b");
    newui::ActionContextTree tree;
    tree.setParent(a, b);
    tree.setParent(b, a);
    EXPECT_FALSE(tree.inherits(a, newui::ActionContext("elsewhere")));
    EXPECT_LE(tree.depth(a), 64);
}

TEST(ActionBindings, HotkeyIsTheGlobalBindingAndExtrasFollow) {
    Contexts c;
    newui::Action action("x");
    EXPECT_TRUE(action.bindings().empty());

    action.setHotkey(newui::vkF1, newui::kmCtrl);
    action.addBinding({c.canvas, newui::vkF2, newui::kmUndefined});
    action.addBinding({c.canvas, newui::vkF2, newui::kmUndefined});
    action.addBinding({c.canvas, newui::vkUndefined, newui::kmUndefined});

    auto bindings = action.bindings();
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(bindings[0].context, c.global);
    EXPECT_EQ(bindings[0].key, newui::vkF1);
    EXPECT_EQ(bindings[1].context, c.canvas);

    action.clearBindings();
    EXPECT_EQ(action.bindings().size(), 1u);
}

TEST(ActionMatcher, PlainHotkeyMatchesWithNoActiveContexts) {
    Contexts c;
    newui::Action action("x");
    action.setHotkey(newui::vkF1);
    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmUndefined, c.tree, {}), &action);
    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmCtrl, c.tree, {}), nullptr);
}

TEST(ActionMatcher, ContextBindingNeedsItsContextActive) {
    Contexts c;
    newui::Action action("x");
    action.addBinding({c.canvas, newui::vkF1, newui::kmUndefined});

    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmUndefined, c.tree, {}), nullptr);
    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmUndefined, c.tree, {c.textInput}), nullptr);
    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmUndefined, c.tree, {c.canvas}), &action);
}

TEST(ActionMatcher, ChildContextCountsAsItsAncestors) {
    Contexts c;
    newui::Action action("x");
    action.addBinding({c.canvas, newui::vkF1, newui::kmUndefined});
    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmUndefined, c.tree, {c.trace}), &action);
}

TEST(ActionMatcher, DeepestActiveContextWinsRegardlessOfOrder) {
    Contexts c;
    newui::Action global("global");
    newui::Action canvas("canvas");
    newui::Action trace("trace");
    global.setHotkey(newui::vkF1);
    canvas.addBinding({c.canvas, newui::vkF1, newui::kmUndefined});
    trace.addBinding({c.trace, newui::vkF1, newui::kmUndefined});

    EXPECT_EQ(newui::ActionMatcher::find({&trace, &canvas, &global}, kKey, newui::kmUndefined, c.tree, {c.trace}), &trace);
    EXPECT_EQ(newui::ActionMatcher::find({&global, &canvas, &trace}, kKey, newui::kmUndefined, c.tree, {c.canvas}), &canvas);
    EXPECT_EQ(newui::ActionMatcher::find({&global, &canvas, &trace}, kKey, newui::kmUndefined, c.tree, {c.textInput}), &global);
}

TEST(ActionMatcher, TextInputContextShadowsCanvasKeyOnlyWhereBound) {
    Contexts c;
    newui::Action documentUndo("documentUndo");
    newui::Action textUndo("textUndo");
    documentUndo.addBinding({c.canvas, newui::vkF1, newui::kmCtrl});
    textUndo.addBinding({c.textInput, newui::vkF1, newui::kmCtrl});

    EXPECT_EQ(newui::ActionMatcher::find({&documentUndo, &textUndo}, kKey, newui::kmCtrl, c.tree, {c.canvas}), &documentUndo);
    EXPECT_EQ(newui::ActionMatcher::find({&documentUndo, &textUndo}, kKey, newui::kmCtrl, c.tree, {c.textInput}), &textUndo);
}

TEST(ActionMatcher, ChordBindingIsNotAOneStrokeMatch) {
    Contexts c;
    newui::Action action("chord");
    constexpr std::uint32_t second = static_cast<std::uint32_t>(newui::vkF2);
    action.addBinding({c.global, newui::vkF1, newui::kmCtrl, newui::vkF2, newui::kmCtrl});
    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmCtrl, c.tree, {}), nullptr);
    EXPECT_TRUE(newui::ActionMatcher::startsChord({&action}, kKey, newui::kmCtrl, c.tree, {}));
    EXPECT_FALSE(newui::ActionMatcher::startsChord({&action}, kKey, newui::kmUndefined, c.tree, {}));
    EXPECT_EQ(newui::ActionMatcher::findChord({&action}, kKey, newui::kmCtrl, second, newui::kmCtrl, c.tree, {}), &action);
    EXPECT_EQ(newui::ActionMatcher::findChord({&action}, kKey, newui::kmCtrl, second, newui::kmUndefined, c.tree, {}), nullptr);
    EXPECT_EQ(newui::ActionMatcher::findChord({&action}, kKey, newui::kmCtrl, kKey, newui::kmCtrl, c.tree, {}), nullptr);
}

TEST(ActionMatcher, ChordFollowsItsContext) {
    Contexts c;
    newui::Action action("chord");
    constexpr std::uint32_t second = static_cast<std::uint32_t>(newui::vkF2);
    action.addBinding({c.canvas, newui::vkF1, newui::kmCtrl, newui::vkF2, newui::kmUndefined});
    EXPECT_FALSE(newui::ActionMatcher::startsChord({&action}, kKey, newui::kmCtrl, c.tree, {c.textInput}));
    EXPECT_TRUE(newui::ActionMatcher::startsChord({&action}, kKey, newui::kmCtrl, c.tree, {c.trace}));
    EXPECT_EQ(newui::ActionMatcher::findChord({&action}, kKey, newui::kmCtrl, second, newui::kmUndefined, c.tree, {c.textInput}), nullptr);
    EXPECT_EQ(newui::ActionMatcher::findChord({&action}, kKey, newui::kmCtrl, second, newui::kmUndefined, c.tree, {c.trace}), &action);
}

TEST(ActionMatcher, OneStrokeBindingNeverMatchesAsAChord) {
    Contexts c;
    newui::Action action("plain");
    action.addBinding({c.global, newui::vkF1, newui::kmCtrl});
    EXPECT_FALSE(newui::ActionMatcher::startsChord({&action}, kKey, newui::kmCtrl, c.tree, {}));
    EXPECT_EQ(newui::ActionMatcher::find({&action}, kKey, newui::kmCtrl, c.tree, {}), &action);
}

TEST(ActionMatcher, FirstRegisteredWinsATie) {
    Contexts c;
    newui::Action first("first");
    newui::Action second("second");
    first.setHotkey(newui::vkF1);
    second.setHotkey(newui::vkF1);
    EXPECT_EQ(newui::ActionMatcher::find({&first, &second}, kKey, newui::kmUndefined, c.tree, {}), &first);
}
