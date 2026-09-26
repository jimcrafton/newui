#include "newui/textfolding.h"

#include <gtest/gtest.h>

using namespace newui;

namespace {
    // "a {" / "  b" / "}" / "c", with [3, 8) foldable.
    text::TextFold blockFold(bool collapsed) {
        text::TextFold fold;
        fold.start = 3;
        fold.length = 5;
        fold.collapsed = collapsed;
        return fold;
    }

    void setUp(TextFoldingControl& control, bool collapsed) {
        control.setBounds(Rect(0, 0, 300, 200));
        control.setText(L"a {\n  b\n}\nc");
        control.setFolds({ blockFold(collapsed) });
    }
}

TEST(TextFoldingControl, ACollapsedFoldIsLaidOutAsOneLine) {
    TextFoldingControl control;
    setUp(control, true);
    control.controller().ensureLayoutUpToDate();
    EXPECT_EQ(control.controller().layoutEngine().lineCount(), 2u);

    control.setFoldCollapsed(0, false);
    control.controller().ensureLayoutUpToDate();
    EXPECT_EQ(control.controller().layoutEngine().lineCount(), 4u);
}

TEST(TextFoldingControl, APlainTextControlHasNoFolding) {
    TextControl control;
    control.setBounds(Rect(0, 0, 300, 200));
    control.setText(L"a {\n  b\n}\nc");
    control.controller().ensureLayoutUpToDate();
    EXPECT_EQ(control.controller().layoutEngine().lineCount(), 4u);
}

TEST(TextFoldingControl, TextInsertedBeforeAFoldMovesIt) {
    TextFoldingControl control;
    setUp(control, true);
    control.model().insert(0, L"xy");
    ASSERT_EQ(control.folds().size(), 1u);
    EXPECT_EQ(control.folds()[0].start, 5u);
    EXPECT_EQ(control.folds()[0].length, 5u);
    EXPECT_TRUE(control.folds()[0].collapsed);

    control.model().insert(0, L"z");   // a single character: the Char event path
    EXPECT_EQ(control.folds()[0].start, 6u);
}

TEST(TextFoldingControl, TextAfterAFoldLeavesItAlone) {
    TextFoldingControl control;
    setUp(control, true);
    control.model().insert(8, L"!");   // right at its end
    control.model().remove(text::TextRange(10, 2));
    ASSERT_EQ(control.folds().size(), 1u);
    EXPECT_EQ(control.folds()[0].start, 3u);
    EXPECT_EQ(control.folds()[0].length, 5u);
}

TEST(TextFoldingControl, AnEditInsideAFoldResizesAndExpandsIt) {
    TextFoldingControl control;
    setUp(control, true);
    control.model().insert(6, L"bbb");
    ASSERT_EQ(control.folds().size(), 1u);
    EXPECT_EQ(control.folds()[0].length, 8u);
    EXPECT_FALSE(control.folds()[0].collapsed);
}

TEST(TextFoldingControl, AnEditAcrossAFoldsEdgeDropsIt) {
    TextFoldingControl control;
    setUp(control, false);
    control.model().remove(text::TextRange(1, 4));
    EXPECT_TRUE(control.folds().empty());
}

TEST(TextFoldingControl, ReplacingAllTheTextDropsEveryFold) {
    TextFoldingControl control;
    setUp(control, true);
    control.setText(L"something else entirely");
    EXPECT_TRUE(control.folds().empty());
}

TEST(TextFoldingControl, TheCaretNeverRestsInsideACollapsedFold) {
    TextFoldingControl control;
    setUp(control, false);
    control.caret().setPosition(text::TextPosition(6));

    // Collapsing around the caret moves it to the fold's start.
    control.setFoldCollapsed(0, true);
    EXPECT_EQ(control.caret().position().offset(), 3u);

    // Setting collapsed folds around the caret expands them.
    control.setFoldCollapsed(0, false);
    control.caret().setPosition(text::TextPosition(6));
    control.setFolds({ blockFold(true) });
    EXPECT_FALSE(control.folds()[0].collapsed);

    TextFoldingController* folding = control.foldingController();
    ASSERT_NE(folding, nullptr);
    control.setFoldCollapsed(0, true);
    EXPECT_TRUE(folding->expandFoldsAt(5));
    EXPECT_FALSE(control.folds()[0].collapsed);
    EXPECT_FALSE(folding->expandFoldsAt(3)) << "its edges are not inside it";
}

TEST(TextFoldingControl, FoldsAreSortedOutermostFirst) {
    TextFoldingControl control;
    control.setText(L"0123456789");
    text::TextFold inner;
    inner.start = 2;
    inner.length = 2;
    text::TextFold outer;
    outer.start = 2;
    outer.length = 6;
    text::TextFold first;
    first.start = 0;
    first.length = 1;
    text::TextFold empty;
    empty.start = 5;
    control.setFolds({ inner, outer, first, empty });
    ASSERT_EQ(control.folds().size(), 3u);
    EXPECT_EQ(control.folds()[0].start, 0u);
    EXPECT_EQ(control.folds()[1].length, 6u);
    EXPECT_EQ(control.folds()[2].length, 2u);
}

namespace {
    // Red pixels at or below y, painting control into a 300x200 image.
    std::size_t redPixelsFrom(TextFoldingControl& control, int fromY) {
        BLImage image(300, 200, BL_FORMAT_PRGB32);
        {
            BLContext ctx(image);
            ctx.clear_all();
            control.paint(ctx);
            ctx.end();
        }
        BLImageData data{};
        image.get_data(&data);
        std::size_t count = 0;
        for (int y = fromY; y < 200; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride);
            for (int x = 0; x < 300; ++x) {
                const std::uint32_t px = row[x];
                const std::uint32_t r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
                count += (r > 120 && g < 60 && b < 60) ? 1 : 0;
            }
        }
        return count;
    }
}

// Collapsed, the block's lines aren't drawn: nothing red below the first line.
TEST(TextFoldingControl, ACollapsedFoldHidesItsLinesWhenPainted) {
    TextFoldingControl control;
    control.setBounds(Rect(0, 0, 300, 200));
    control.setText(L"WWW {\nWWWWWW\nWWWWWW\n} WWW");
    control.setTextColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
    text::TextFold fold;
    fold.start = 5;
    fold.length = 15;
    control.setFolds({ fold });
    control.controller().ensureLayoutUpToDate();
    const int belowFirstLine = static_cast<int>(control.getClientBounds().top() + control.controller().layoutEngine().lineHeight(0)) + 2;

    EXPECT_GT(redPixelsFrom(control, belowFirstLine), 20u);
    control.setFoldCollapsed(0, true);
    EXPECT_EQ(redPixelsFrom(control, belowFirstLine), 0u);
    EXPECT_GT(redPixelsFrom(control, 0), 20u) << "the first line, with the text after the fold, still draws";
}
