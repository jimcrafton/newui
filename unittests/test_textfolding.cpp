#include "newui/textfolding.h"
#include "newui/fontmanager.h"
#include "newui/uicolormanager.h"

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

TEST(TextFoldingControl, TheGutterSitsLeftOfTheTextAndWidensWithTheLineCount) {
    TextFoldingControl control;
    setUp(control, false);
    const Rect client = control.getClientBounds();
    const Rect area = control.controller().textArea();
    EXPECT_GT(area.left(), client.left() + 10.0f);
    EXPECT_NEAR(area.right(), client.right(), 0.5f);

    std::wstring many;
    for (int i = 0; i < 1500; ++i) {
        many += L"x\n";
    }
    control.setText(many);
    EXPECT_GT(control.controller().textArea().left(), area.left()) << "four digits need more room than two";

    control.setGutterVisible(false);
    EXPECT_NEAR(control.controller().textArea().left(), client.left(), 0.5f);
}

TEST(TextFoldingControl, LineNumbersCountHiddenLines) {
    TextFoldingControl control;
    setUp(control, true);
    control.controller().ensureLayoutUpToDate();
    const text::TextLayoutEngine& engine = control.controller().layoutEngine();
    ASSERT_EQ(engine.lineCount(), 2u);
    EXPECT_EQ(engine.lineNumber(0), 0u);
    EXPECT_EQ(engine.lineNumber(1), 3u);
    EXPECT_GT(engine.lineBaseline(0), 0.0f);
    EXPECT_LT(engine.lineBaseline(0), engine.lineHeight(0));
}

TEST(TextFoldingControl, AFoldsMarkerIsOnTheLineItStartsOn) {
    TextFoldingControl control;
    setUp(control, false);
    control.controller().ensureLayoutUpToDate();
    TextFoldingController* folding = control.foldingController();
    ASSERT_NE(folding, nullptr);
    EXPECT_EQ(folding->markerFold(0), 0u);
    EXPECT_EQ(folding->markerFold(1), TextFoldingController::kNoFold);
    EXPECT_EQ(folding->markerFold(3), TextFoldingController::kNoFold);

    control.setFoldCollapsed(0, true);
    control.controller().ensureLayoutUpToDate();
    EXPECT_EQ(folding->markerFold(0), 0u);
    EXPECT_EQ(folding->markerFold(1), TextFoldingController::kNoFold);

    // A fold within one line has no marker.
    text::TextFold inline_;
    inline_.start = 0;
    inline_.length = 2;
    control.setFolds({ inline_ });
    control.controller().ensureLayoutUpToDate();
    EXPECT_EQ(folding->markerFold(0), TextFoldingController::kNoFold);
}

// The gutter really draws: ink in it, beyond its plain background.
TEST(TextFoldingControl, TheGutterPaintsNumbersAndMarkers) {
    TextFoldingControl control;
    setUp(control, false);
    // Left of the gutter's edge lines, so only numbers and markers count.
    const int gutterRight = static_cast<int>(control.controller().textArea().left()) - 6;
    BLImage image(300, 200, BL_FORMAT_PRGB32);
    {
        BLContext ctx(image);
        ctx.clear_all();
        control.paint(ctx);
        ctx.end();
    }
    BLImageData data{};
    image.get_data(&data);
    auto pixel = [&](int x, int y) {
        return reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride)[x];
    };
    const std::uint32_t background = pixel(gutterRight - 1, 190);
    std::size_t ink = 0;
    const Rect client = control.getClientBounds();   // inside the control's border
    for (int y = static_cast<int>(client.top()) + 1; y < static_cast<int>(client.bottom()) - 1; ++y) {
        for (int x = static_cast<int>(client.left()) + 1; x < gutterRight; ++x) {
            ink += pixel(x, y) != background ? 1 : 0;
        }
    }
    EXPECT_GT(ink, 40u);
}

// Return inserts a newline only in a multi-line controller - a swapped-in one must be too.
TEST(TextFoldingControl, ItsControllerIsMultiLine) {
    TextFoldingControl control;
    EXPECT_TRUE(control.controller().isMultiline());

    TextControl plain;
    plain.setController(std::make_unique<TextController>(plain));
    EXPECT_TRUE(plain.controller().isMultiline());
}

// The gutter is set apart: its own background, not the text's.
TEST(TextFoldingControl, TheGutterHasItsOwnBackground) {
    TextFoldingControl control;
    control.setBounds(Rect(0, 0, 300, 200));
    control.setText(L"");
    BLImage image(300, 200, BL_FORMAT_PRGB32);
    {
        BLContext ctx(image);
        ctx.clear_all();
        control.paint(ctx);
        ctx.end();
    }
    BLImageData data{};
    image.get_data(&data);
    auto pixel = [&](int x, int y) {
        return reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride)[x];
    };
    const Rect client = control.getClientBounds();
    const int y = static_cast<int>(client.bottom()) - 5;
    EXPECT_EQ(pixel(static_cast<int>(client.left()) + 2, y),
        UIColorManager::colorFor(UIColorRole::WindowBackground).toBLRgba32().value);
    EXPECT_NE(pixel(static_cast<int>(client.left()) + 2, y), pixel(static_cast<int>(client.right()) - 5, y));
}

TEST(FontManagerMonospace, IsInstalledAndReallyMonospaced) {
    const Font font = FontManager::monospaceFont(14.0f);
    EXPECT_TRUE(FontManager::isInstalled(font.name()));
    EXPECT_EQ(font.size(), 14.0f);
    ASSERT_NE(font.blFont(), nullptr);
    EXPECT_NEAR(font.measureText("iiii").width, font.measureText("WWWW").width, 0.5f);
}

// Windows registers some families as "<name> Regular" (Cascadia Mono) - found either way.
TEST(FontManagerMonospace, AFamilyIsFoundWithoutItsRegularSuffix) {
    for (const SystemFontInfo& info : FontManager::listFonts()) {
        const std::string suffix = " Regular";
        if (info.name.size() > suffix.size() && info.name.compare(info.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            const std::string family = info.name.substr(0, info.name.size() - suffix.size());
            EXPECT_TRUE(FontManager::isInstalled(family)) << family;
            BLFont font;
            EXPECT_TRUE(FontManager::createFont(family, 12.0f, font)) << family;
            return;
        }
    }
    GTEST_SKIP() << "no \"<name> Regular\" font installed";
}

TEST(TextFoldingControl, DefaultsToTheMonospaceFont) {
    TextFoldingControl control;
    EXPECT_EQ(control.font().name(), FontManager::monospaceFont().name());
    EXPECT_EQ(control.font().size(), FontManager::monospaceFont().size());
}

// A multi-line editor types Tab; a single-line field lets it move focus.
TEST(TextControlTab, TextControlsTakeTabAndTextFieldsDoNot) {
    TextControl control;
    EXPECT_TRUE(control.wantsTabKey());
    TextFoldingControl folding;
    EXPECT_TRUE(folding.wantsTabKey());
    TextField field;
    EXPECT_FALSE(field.wantsTabKey());
}

// Tab stops land every tabWidth digits.
TEST(TextControlTab, TabStopsAreCountedInCharacters) {
    const Font font = FontManager::monospaceFont(14.0f);
    const float digit = font.measureText("0000").width / 4.0f;
    text::TextStorage storage(L"	X");
    auto afterTab = [&](std::size_t tabWidth) {
        text::TextLayoutEngine engine;
        engine.update(storage, font, 500.0f, 100.0f, true, {}, {}, tabWidth);
        Point topLeft;
        float height = 0.0f;
        engine.hitTestPosition(text::TextPosition(1), topLeft, height);
        return topLeft.x;
    };
    EXPECT_NEAR(afterTab(4), 4.0f * digit, 1.0f);
    EXPECT_NEAR(afterTab(2), 2.0f * digit, 1.0f);
    EXPECT_NEAR(afterTab(8), 8.0f * digit, 1.0f);

    TextControl control;
    EXPECT_EQ(control.tabWidth(), 4u);
    control.setTabWidth(2);
    EXPECT_EQ(control.controller().tabWidth(), 2u);
}
