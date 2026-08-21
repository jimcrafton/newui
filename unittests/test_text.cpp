#include "newui/text.h"

#include "newui/font.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"

#include <d2d1.h>
#include <dwrite.h>

#include <gtest/gtest.h>

#include <any>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

using newui::text::Caret;
using newui::text::CharChangeKind;
using newui::text::DirectWriteResources;
using newui::text::TextInputTraits;
using newui::text::TextLayoutEngine;
using newui::text::TextModel;
using newui::text::TextPosition;
using newui::text::TextRange;
using newui::text::TextRenderer;
using newui::text::TextSelection;
using newui::text::TextStorage;

namespace {

// Reads back the alpha channel at (x, y) from a BLImage already painted
// into - same pattern test_viewstyle.cpp's LabelStyle/ThemedViewStyle
// tests already use to verify real rendered output rather than just
// "didn't crash".
uint8_t AlphaAt(BLImage& image, int x, int y) {
    BLImageData data;
    image.get_data(&data);
    const uint8_t* base = static_cast<const uint8_t*>(data.pixel_data);
    const uint32_t* row = reinterpret_cast<const uint32_t*>(base + y * data.stride);
    return uint8_t(row[x] >> 24);
}

}  // namespace

TEST(TextPosition, DefaultConstructedIsInvalid) {
    TextPosition pos;
    EXPECT_FALSE(pos.isValid());
    EXPECT_EQ(pos.offset(), TextPosition::Invalid);
}

TEST(TextPosition, ExplicitOffsetIsValid) {
    TextPosition pos(5);
    EXPECT_TRUE(pos.isValid());
    EXPECT_EQ(pos.offset(), 5u);
}

TEST(TextPosition, SetOffsetUpdatesValue) {
    TextPosition pos(5);
    pos.setOffset(9);
    EXPECT_EQ(pos.offset(), 9u);
}

TEST(TextPosition, ComparisonOperatorsOrderByOffset) {
    TextPosition a(3);
    TextPosition b(7);
    TextPosition c(7);

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a <= b);
    EXPECT_TRUE(b > a);
    EXPECT_TRUE(b >= a);
    EXPECT_TRUE(b == c);
    EXPECT_FALSE(b != c);
    EXPECT_TRUE(a != b);
}

TEST(TextRange, DefaultConstructedIsInvalidAndEmpty) {
    TextRange range;
    EXPECT_FALSE(range.isValid());
    EXPECT_TRUE(range.isEmpty());
    EXPECT_EQ(range.start(), TextRange::Invalid);
    EXPECT_EQ(range.length(), 0u);
}

TEST(TextRange, StartLengthConstructorComputesEnd) {
    TextRange range(4, 6);
    EXPECT_TRUE(range.isValid());
    EXPECT_FALSE(range.isEmpty());
    EXPECT_EQ(range.start(), 4u);
    EXPECT_EQ(range.length(), 6u);
    EXPECT_EQ(range.end(), 10u);
}

TEST(TextRange, ZeroLengthRangeIsAnOrdinaryEmptyRangeNotInvalid) {
    // A caret with no selection - length() == 0 at a valid start(), not
    // the same thing as a default-constructed (start() == Invalid) range.
    TextRange caret(4, 0);
    EXPECT_TRUE(caret.isValid());
    EXPECT_TRUE(caret.isEmpty());
    EXPECT_EQ(caret.end(), 4u);
}

TEST(TextRange, ContainsChecksHalfOpenInterval) {
    TextRange range(4, 6);  // [4, 10)
    EXPECT_FALSE(range.contains(3));
    EXPECT_TRUE(range.contains(4));
    EXPECT_TRUE(range.contains(9));
    EXPECT_FALSE(range.contains(10));
}

TEST(TextRange, EqualityComparesStartAndLength) {
    EXPECT_EQ(TextRange(2, 3), TextRange(2, 3));
    EXPECT_NE(TextRange(2, 3), TextRange(2, 4));
    EXPECT_NE(TextRange(2, 3), TextRange(3, 3));
}

TEST(TextInputTraits, DefaultsAreEditableInsecureUnlimited) {
    TextInputTraits traits;
    EXPECT_FALSE(traits.isSecureTextEntry());
    EXPECT_FALSE(traits.isReadOnly());
    EXPECT_EQ(traits.maxLength(), 0u);
}

TEST(TextInputTraits, SettersUpdateTheirOwnField) {
    TextInputTraits traits;
    traits.setSecureTextEntry(true);
    traits.setReadOnly(true);
    traits.setMaxLength(140);

    EXPECT_TRUE(traits.isSecureTextEntry());
    EXPECT_TRUE(traits.isReadOnly());
    EXPECT_EQ(traits.maxLength(), 140u);
}

TEST(TextStorage, DefaultConstructedIsEmpty) {
    TextStorage storage;
    EXPECT_TRUE(storage.empty());
    EXPECT_EQ(storage.length(), 0u);
    EXPECT_EQ(storage.text(), L"");
}

TEST(TextStorage, ConstructedFromStringHoldsItDirectly) {
    TextStorage storage(L"hello");
    EXPECT_FALSE(storage.empty());
    EXPECT_EQ(storage.length(), 5u);
    EXPECT_EQ(storage.text(), L"hello");
}

TEST(TextStorage, SetTextReplacesWholeContent) {
    TextStorage storage(L"hello");
    storage.setText(L"goodbye");
    EXPECT_EQ(storage.text(), L"goodbye");
    EXPECT_EQ(storage.length(), 7u);
}

TEST(TextStorage, AtReturnsCharacterAtOffset) {
    TextStorage storage(L"hello");
    EXPECT_EQ(storage.at(0), L'h');
    EXPECT_EQ(storage.at(4), L'o');
}

TEST(TextStorage, AtReturnsNulCharacterForOutOfRangeOffsetRatherThanThrowing) {
    TextStorage storage(L"hello");
    EXPECT_EQ(storage.at(5), L'\0');
    EXPECT_EQ(storage.at(1000), L'\0');
}

TEST(TextStorage, SubstringExtractsRequestedRange) {
    TextStorage storage(L"hello world");
    EXPECT_EQ(storage.substring(TextRange(6, 5)), L"world");
    EXPECT_EQ(storage.substring(TextRange(0, 5)), L"hello");
}

TEST(TextStorage, SubstringClampsARangeThatRunsPastTheEnd) {
    TextStorage storage(L"hello");
    EXPECT_EQ(storage.substring(TextRange(3, 100)), L"lo");
}

TEST(TextStorage, SubstringOfARangeEntirelyPastTheEndIsEmpty) {
    TextStorage storage(L"hello");
    EXPECT_EQ(storage.substring(TextRange(10, 5)), L"");
}

TEST(TextStorage, InsertAtOffsetSplicesTextIn) {
    TextStorage storage(L"hlo");
    storage.insert(1, L"el");
    EXPECT_EQ(storage.text(), L"hello");
}

TEST(TextStorage, InsertAtStartPrepends) {
    TextStorage storage(L"world");
    storage.insert(0, L"hello ");
    EXPECT_EQ(storage.text(), L"hello world");
}

TEST(TextStorage, InsertAtEndAppends) {
    TextStorage storage(L"hello");
    storage.insert(storage.length(), L" world");
    EXPECT_EQ(storage.text(), L"hello world");
}

TEST(TextStorage, InsertAtOffsetPastEndClampsToEnd) {
    TextStorage storage(L"hello");
    storage.insert(1000, L"!");
    EXPECT_EQ(storage.text(), L"hello!");
}

TEST(TextStorage, RemoveDeletesTheGivenRange) {
    TextStorage storage(L"hello world");
    storage.remove(TextRange(5, 6));  // " world"
    EXPECT_EQ(storage.text(), L"hello");
}

TEST(TextStorage, RemoveClampsARangeThatRunsPastTheEnd) {
    TextStorage storage(L"hello");
    storage.remove(TextRange(3, 100));
    EXPECT_EQ(storage.text(), L"hel");
}

TEST(TextStorage, RemoveOfARangeEntirelyPastTheEndIsANoOp) {
    TextStorage storage(L"hello");
    storage.remove(TextRange(10, 5));
    EXPECT_EQ(storage.text(), L"hello");
}

TEST(TextStorage, ReplaceSwapsTheGivenRangeForNewText) {
    TextStorage storage(L"hello world");
    storage.replace(TextRange(6, 5), L"there");
    EXPECT_EQ(storage.text(), L"hello there");
}

TEST(TextStorage, ReplaceWithLongerTextGrowsStorage) {
    TextStorage storage(L"hi");
    storage.replace(TextRange(0, 2), L"hello");
    EXPECT_EQ(storage.text(), L"hello");
}

TEST(TextStorage, ReplaceWithEmptyRangeActsAsInsert) {
    TextStorage storage(L"hed");
    storage.replace(TextRange(1, 0), L"ell");
    EXPECT_EQ(storage.text(), L"helled");
}

TEST(Caret, DefaultConstructedIsInactiveAndNotVisible) {
    Caret caret;
    EXPECT_FALSE(caret.isActive());
    EXPECT_FALSE(caret.isVisible());
}

TEST(Caret, SetPositionUpdatesPosition) {
    Caret caret;
    caret.setPosition(TextPosition(7));
    EXPECT_EQ(caret.position(), TextPosition(7));
}

TEST(Caret, ColorGetterSetterRoundTrips) {
    Caret caret;
    newui::Color red(1.0f, 0.0f, 0.0f, 1.0f);
    caret.setColor(red);
    EXPECT_EQ(caret.color().r, red.r);
    EXPECT_EQ(caret.color().g, red.g);
    EXPECT_EQ(caret.color().b, red.b);
    EXPECT_EQ(caret.color().a, red.a);
}

TEST(Caret, StopWhenNotActiveIsANoOp) {
    Caret caret;
    caret.stop();
    EXPECT_FALSE(caret.isActive());
}

TEST(Caret, StartMakesItActiveAndVisible) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    Caret caret;
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false;

    runLoop.post([&]() {
        // interval(0) disables the blink timer entirely - stays solidly
        // active/visible with no blink race to synchronize against,
        // ideal for a deterministic check.
        caret.start(runLoop, std::chrono::milliseconds(0));
        {
            std::lock_guard<std::mutex> lock(mutex);
            started = true;
        }
        cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return started; }));
    }

    EXPECT_TRUE(caret.isActive());
    EXPECT_TRUE(caret.isVisible());

    runLoop.post([&]() { caret.stop(); });
    runLoop.quit();
    loopThread.join();
}

TEST(Caret, SecondStartWhileAlreadyActiveIsANoOp) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    Caret caret;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;

    runLoop.post([&]() {
        caret.start(runLoop, std::chrono::milliseconds(0));
        // A second start() while already active must be a no-op, not a
        // second independent timer - otherwise a single stop() below
        // would leave one of the two still running.
        caret.start(runLoop, std::chrono::milliseconds(15));
        caret.stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; }));
    }

    EXPECT_FALSE(caret.isActive());

    runLoop.quit();
    loopThread.join();
}

TEST(Caret, StopMakesItInactiveAndNotVisible) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    Caret caret;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;

    runLoop.post([&]() {
        caret.start(runLoop, std::chrono::milliseconds(15));
        caret.stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; }));
    }

    EXPECT_FALSE(caret.isActive());
    EXPECT_FALSE(caret.isVisible());

    runLoop.quit();
    loopThread.join();
}

TEST(Caret, BlinksBetweenVisibleAndHiddenOverTime) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    Caret caret;
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<bool> samples;
    constexpr size_t kSampleCount = 8;

    runLoop.post([&]() {
        caret.start(runLoop, std::chrono::milliseconds(15));
        // Samples caret.isVisible() from this same loop thread - visible_
        // is only ever mutated by the blink timer, which also fires here,
        // so this never races the timer the way reading it from the test
        // thread directly would.
        runLoop.postDelayed(std::chrono::milliseconds(10), [&]() {
            std::lock_guard<std::mutex> lock(mutex);
            samples.push_back(caret.isVisible());
            bool done = samples.size() >= kSampleCount;
            if (done) {
                cv.notify_all();
            }
            return done;
            });
        });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5),
            [&] { return samples.size() >= kSampleCount; }));
    }

    bool sawVisible = false;
    bool sawHidden = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (bool visible : samples) {
            if (visible) {
                sawVisible = true;
            } else {
                sawHidden = true;
            }
        }
    }
    // 15ms blink interval, sampled every 10ms for kSampleCount samples
    // (80ms total) - several toggles are expected in that window, so
    // both states should show up rather than this happening to catch
    // only one phase.
    EXPECT_TRUE(sawVisible);
    EXPECT_TRUE(sawHidden);

    runLoop.post([&]() { caret.stop(); });
    runLoop.quit();
    loopThread.join();
}

TEST(Caret, OnVisibilityChangedFiresOnEachBlinkTick) {
    // Regression test: onVisibilityChanged didn't exist at first - the
    // blink timer flipped isVisible() with nothing to tell an owning
    // control to repaint, so a blinking caret only visibly updated when
    // something *else* (e.g. a mouse-hover repaint) incidentally
    // repainted it too (confirmed live via examples/controls1.cpp - the
    // caret only blinked while the mouse was moving).
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    Caret caret;
    std::mutex mutex;
    std::condition_variable cv;
    int changedCount = 0;
    constexpr int kExpectedTicks = 3;

    caret.onVisibilityChanged.add([&](Caret&) {
        std::lock_guard<std::mutex> lock(mutex);
        ++changedCount;
        if (changedCount >= kExpectedTicks) {
            cv.notify_all();
        }
        return newui::SyncReturn::Handled;
        });

    runLoop.post([&]() {
        caret.start(runLoop, std::chrono::milliseconds(15));
        });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5),
            [&] { return changedCount >= kExpectedTicks; }));
    }

    runLoop.post([&]() { caret.stop(); });
    runLoop.quit();
    loopThread.join();
}

TEST(Caret, DrawDoesNothingWhenNotVisible) {
    Caret caret;  // never start()ed - isVisible() stays false
    ASSERT_FALSE(caret.isVisible());

    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    caret.draw(ctx, newui::Point(5.0f, 2.0f), 16.0f);
    ctx.end();

    EXPECT_EQ(AlphaAt(image, 5, 10), 0);
}

TEST(Caret, DrawPaintsASystemWidthBarWhenVisible) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    Caret caret;
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false;

    runLoop.post([&]() {
        caret.start(runLoop, std::chrono::milliseconds(0));  // solidly visible, no blink race
        {
            std::lock_guard<std::mutex> lock(mutex);
            started = true;
        }
        cv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return started; }));
    }

    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    caret.draw(ctx, newui::Point(5.0f, 2.0f), 16.0f);
    ctx.end();

    EXPECT_GT(AlphaAt(image, 5, 10), 0);
    // Well clear of even a widened (accessibility) caret bar.
    EXPECT_EQ(AlphaAt(image, 15, 10), 0);

    runLoop.post([&]() { caret.stop(); });
    runLoop.quit();
    loopThread.join();
}

TEST(TextSelection, DefaultIsEmpty) {
    TextSelection selection;
    EXPECT_TRUE(selection.isEmpty());
    EXPECT_TRUE(selection.ranges().empty());
}

TEST(TextSelection, SetRangeReplacesAnyExistingRanges) {
    TextSelection selection;
    selection.addRange(TextRange(0, 3));
    selection.setRange(TextRange(5, 2));
    ASSERT_EQ(selection.ranges().size(), 1u);
    EXPECT_EQ(selection.ranges()[0], TextRange(5, 2));
}

TEST(TextSelection, AddRangeAppendsAlongsideExisting) {
    TextSelection selection;
    selection.addRange(TextRange(0, 3));
    selection.addRange(TextRange(5, 2));
    ASSERT_EQ(selection.ranges().size(), 2u);
    EXPECT_EQ(selection.ranges()[0], TextRange(0, 3));
    EXPECT_EQ(selection.ranges()[1], TextRange(5, 2));
}

TEST(TextSelection, ClearEmptiesEveryRange) {
    TextSelection selection;
    selection.setRange(TextRange(0, 3));
    selection.clear();
    EXPECT_TRUE(selection.isEmpty());
}

TEST(TextSelection, ContainsChecksEveryRange) {
    TextSelection selection;
    selection.addRange(TextRange(0, 3));   // [0, 3)
    selection.addRange(TextRange(10, 5));  // [10, 15)

    EXPECT_TRUE(selection.contains(0));
    EXPECT_TRUE(selection.contains(2));
    EXPECT_FALSE(selection.contains(3));
    EXPECT_TRUE(selection.contains(12));
    EXPECT_FALSE(selection.contains(20));
}

TEST(TextSelection, ColorGetterSetterRoundTrips) {
    TextSelection selection;
    newui::Color green(0.0f, 1.0f, 0.0f, 0.5f);
    selection.setColor(green);
    EXPECT_EQ(selection.color().r, green.r);
    EXPECT_EQ(selection.color().g, green.g);
    EXPECT_EQ(selection.color().b, green.b);
    EXPECT_EQ(selection.color().a, green.a);
}

TEST(TextSelection, DefaultColorMatchesTheLiveSystemHighlightColor) {
    TextSelection selection;
    newui::Color expected = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground);
    EXPECT_EQ(selection.color().r, expected.r);
    EXPECT_EQ(selection.color().g, expected.g);
    EXPECT_EQ(selection.color().b, expected.b);
    EXPECT_EQ(selection.color().a, expected.a);
}

TEST(TextSelection, ResetColorRevertsToTheLiveSystemDefault) {
    TextSelection selection;
    selection.setColor(newui::Color(0.0f, 1.0f, 0.0f, 0.5f));
    selection.resetColor();

    newui::Color expected = newui::UIColorManager::colorFor(newui::UIColorRole::HighlightBackground);
    EXPECT_EQ(selection.color().r, expected.r);
    EXPECT_EQ(selection.color().g, expected.g);
    EXPECT_EQ(selection.color().b, expected.b);
    EXPECT_EQ(selection.color().a, expected.a);
}

TEST(TextSelection, DrawPaintsOneRectPerRange) {
    TextSelection selection;
    selection.setRange(TextRange(0, 3));

    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    selection.draw(ctx, std::vector<newui::Rect>{newui::Rect(2.0f, 2.0f, 10.0f, 10.0f)});
    ctx.end();

    EXPECT_GT(AlphaAt(image, 5, 5), 0);
    EXPECT_EQ(AlphaAt(image, 15, 15), 0);
}

TEST(TextSelection, DrawPaintsNothingForAZeroWidthRect) {
    // draw() no longer inspects ranges_ at all - it just paints every
    // rect it's given (see its own doc comment: index-pairing against
    // ranges_ silently dropped every rect past the first for a
    // multi-line-wrapped selection, a real bug). A genuinely empty range
    // never produces a rect with real area in the first place -
    // TextLayoutEngine::hitTestRange() returns a zero-width rect for one,
    // not an omitted entry (see its own regression test) - so a
    // zero-width rect here is what a real caller would actually pass,
    // not a fabricated non-zero one the old version of this test used.
    TextSelection selection;
    selection.setRange(TextRange(3, 0));  // an empty (caret-like) range

    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    selection.draw(ctx, std::vector<newui::Rect>{newui::Rect(2.0f, 2.0f, 0.0f, 10.0f)});
    ctx.end();

    EXPECT_EQ(AlphaAt(image, 5, 5), 0);
}

TEST(TextSelection, DrawIgnoresRangesWithNoMatchingRect) {
    TextSelection selection;
    selection.addRange(TextRange(0, 3));
    selection.addRange(TextRange(5, 2));

    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    // Only one rect for two ranges - draw() should paint just the first
    // and stop, not read past rects' own end.
    selection.draw(ctx, std::vector<newui::Rect>{newui::Rect(2.0f, 2.0f, 4.0f, 4.0f)});
    ctx.end();

    EXPECT_GT(AlphaAt(image, 3, 3), 0);
}

TEST(TextModel, DefaultConstructedIsEmpty) {
    TextModel model;
    EXPECT_TRUE(model.empty());
    EXPECT_EQ(model.length(), 0u);
    EXPECT_EQ(model.text(), L"");
}

TEST(TextModel, ConstructedFromStringHoldsItDirectly) {
    TextModel model(L"hello");
    EXPECT_FALSE(model.empty());
    EXPECT_EQ(model.length(), 5u);
    EXPECT_EQ(model.text(), L"hello");
}

TEST(TextModel, StorageReflectsCurrentContent) {
    TextModel model(L"hello");
    EXPECT_EQ(model.storage().text(), L"hello");
}

TEST(TextModel, SetTextReplacesContentAndFiresOnChanged) {
    TextModel model(L"hello");
    int onChangedCount = 0;
    model.onChanged.add([&](newui::Model&) {
        ++onChangedCount;
        return newui::SyncReturn::Handled;
        });

    model.setText(L"goodbye");

    EXPECT_EQ(model.text(), L"goodbye");
    EXPECT_EQ(onChangedCount, 1);
}

TEST(TextModel, InsertUpdatesTextAndFiresOnChanged) {
    TextModel model(L"hlo");
    int onChangedCount = 0;
    model.onChanged.add([&](newui::Model&) {
        ++onChangedCount;
        return newui::SyncReturn::Handled;
        });

    model.insert(1, L"el");

    EXPECT_EQ(model.text(), L"hello");
    EXPECT_EQ(onChangedCount, 1);
}

TEST(TextModel, RemoveUpdatesTextAndFiresOnChanged) {
    TextModel model(L"hello world");
    int onChangedCount = 0;
    model.onChanged.add([&](newui::Model&) {
        ++onChangedCount;
        return newui::SyncReturn::Handled;
        });

    model.remove(TextRange(5, 6));

    EXPECT_EQ(model.text(), L"hello");
    EXPECT_EQ(onChangedCount, 1);
}

TEST(TextModel, ReplaceUpdatesTextAndFiresOnChanged) {
    TextModel model(L"hello world");
    int onChangedCount = 0;
    model.onChanged.add([&](newui::Model&) {
        ++onChangedCount;
        return newui::SyncReturn::Handled;
        });

    model.replace(TextRange(6, 5), L"there");

    EXPECT_EQ(model.text(), L"hello there");
    EXPECT_EQ(onChangedCount, 1);
}

TEST(TextModel, ClearEmptiesContentAndFiresOnClearedAndOnChanged) {
    TextModel model(L"hello");
    int onClearedCount = 0;
    int onChangedCount = 0;
    model.onCleared.add([&](newui::Model&) {
        ++onClearedCount;
        return newui::SyncReturn::Handled;
        });
    model.onChanged.add([&](newui::Model&) {
        ++onChangedCount;
        return newui::SyncReturn::Handled;
        });

    model.clear();

    EXPECT_TRUE(model.empty());
    EXPECT_EQ(onClearedCount, 1);
    EXPECT_EQ(onChangedCount, 1);
}

TEST(TextModel, ValueReturnsCurrentTextBoxedAsWstring) {
    TextModel model(L"hello");
    std::any result = model.value();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::wstring>(result), L"hello");
}

TEST(TextModel, SetValueWithWstringReplacesTextAndFiresOnChanged) {
    TextModel model(L"hello");
    int onChangedCount = 0;
    model.onChanged.add([&](newui::Model&) {
        ++onChangedCount;
        return newui::SyncReturn::Handled;
        });

    model.setValue(std::any(std::wstring(L"goodbye")));

    EXPECT_EQ(model.text(), L"goodbye");
    EXPECT_EQ(onChangedCount, 1);
}

TEST(TextModel, SetValueWithWrongTypeIsANoOp) {
    TextModel model(L"hello");
    model.setValue(std::any(std::string("goodbye")));  // not a wstring
    EXPECT_EQ(model.text(), L"hello");
}

TEST(TextModel, AddViewThenMutatingDoesNotCrashAndViewIsAutoRemovedOnDestruction) {
    TextModel model(L"hello");
    auto* view = new newui::SubView();
    model.addView(view);
    EXPECT_EQ(model.viewCount(), 1u);

    // Exercises updateAllViews() against a real registered View.
    model.setText(L"goodbye");

    // onDestroyed (what Model's auto-unregister subscribes to) fires from
    // View::destroy(), not from ~View() itself - a bare delete skips it,
    // same reasoning ViewDestroy's own tests (test_view.cpp) already
    // establish the destroy()-then-delete convention for.
    view->destroy();
    delete view;
    EXPECT_EQ(model.viewCount(), 0u);

    // Mutating again after the view is gone must not crash or touch freed
    // memory.
    model.setText(L"final");
    EXPECT_EQ(model.text(), L"final");
}

TEST(TextSelection, BeforeAndAfterSelectionChangedFireOnSetRangeWithTheNewRange) {
    TextSelection selection;
    TextRange seenBefore, seenAfter;
    int beforeCount = 0, afterCount = 0;
    selection.onBeforeSelectionChanged.add([&](TextSelection&, const TextRange& range, bool&) {
        ++beforeCount;
        seenBefore = range;
        return newui::SyncReturn::Handled;
        });
    selection.onAfterSelectionChanged.add([&](TextSelection&, const TextRange& range) {
        ++afterCount;
        seenAfter = range;
        return newui::SyncReturn::Handled;
        });

    selection.setRange(TextRange(3, 5));

    EXPECT_EQ(beforeCount, 1);
    EXPECT_EQ(afterCount, 1);
    EXPECT_EQ(seenBefore, TextRange(3, 5));
    EXPECT_EQ(seenAfter, TextRange(3, 5));
    ASSERT_EQ(selection.ranges().size(), 1u);
    EXPECT_EQ(selection.ranges()[0], TextRange(3, 5));
}

TEST(TextSelection, VetoingBeforeSelectionChangedPreventsSetRange) {
    TextSelection selection;
    selection.setRange(TextRange(0, 2));
    selection.onBeforeSelectionChanged.add([](TextSelection&, const TextRange&, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
        });
    int afterCount = 0;
    selection.onAfterSelectionChanged.add([&](TextSelection&, const TextRange&) {
        ++afterCount;
        return newui::SyncReturn::Handled;
        });

    selection.setRange(TextRange(3, 5));

    EXPECT_EQ(afterCount, 0);
    ASSERT_EQ(selection.ranges().size(), 1u);
    EXPECT_EQ(selection.ranges()[0], TextRange(0, 2));  // unchanged
}

TEST(TextSelection, BeforeAndAfterSelectionChangedFireOnAddRangeWithTheAddedRange) {
    TextSelection selection;
    selection.setRange(TextRange(0, 2));
    TextRange seen;
    selection.onAfterSelectionChanged.add([&](TextSelection&, const TextRange& range) {
        seen = range;
        return newui::SyncReturn::Handled;
        });

    selection.addRange(TextRange(10, 3));

    EXPECT_EQ(seen, TextRange(10, 3));
    ASSERT_EQ(selection.ranges().size(), 2u);
}

TEST(TextSelection, BeforeAndAfterSelectionChangedFireOnClearWithAnInvalidRange) {
    TextSelection selection;
    selection.setRange(TextRange(0, 2));
    TextRange seen(99, 99);  // deliberately not default, to prove clear()'s range actually overwrites it
    selection.onAfterSelectionChanged.add([&](TextSelection&, const TextRange& range) {
        seen = range;
        return newui::SyncReturn::Handled;
        });

    selection.clear();

    EXPECT_FALSE(seen.isValid());
    EXPECT_TRUE(selection.isEmpty());
}

TEST(TextSelection, VetoingBeforeSelectionChangedPreventsClear) {
    TextSelection selection;
    selection.setRange(TextRange(0, 2));
    selection.onBeforeSelectionChanged.add([](TextSelection&, const TextRange&, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
        });

    selection.clear();

    EXPECT_FALSE(selection.isEmpty());
}

namespace {

// Bundles every counter a TextModel event test needs - avoids seven
// TEST bodies each hand-declaring the same five counters/subscriptions.
struct TextModelEventCounters {
    int beforeChar = 0, afterChar = 0;
    int beforeRangeChanged = 0, afterRangeChanged = 0;
    int onChanged = 0;
    size_t lastCharOffset = 0;
    wchar_t lastChar = 0;
    CharChangeKind lastCharKind = CharChangeKind::Inserted;
    TextRange lastRange;
    std::wstring lastReplacement;

    explicit TextModelEventCounters(TextModel& model) {
        model.onBeforeChar.add([this](TextModel&, size_t offset, wchar_t ch, CharChangeKind kind, bool&) {
            ++beforeChar;
            lastCharOffset = offset;
            lastChar = ch;
            lastCharKind = kind;
            return newui::SyncReturn::Handled;
            });
        model.onAfterChar.add([this](TextModel&, size_t, wchar_t, CharChangeKind) {
            ++afterChar;
            return newui::SyncReturn::Handled;
            });
        model.onBeforeRangeChanged.add([this](TextModel&, const TextRange& range, const std::wstring& replacement, bool&) {
            ++beforeRangeChanged;
            lastRange = range;
            lastReplacement = replacement;
            return newui::SyncReturn::Handled;
            });
        model.onAfterRangeChanged.add([this](TextModel&, const TextRange&, const std::wstring&) {
            ++afterRangeChanged;
            return newui::SyncReturn::Handled;
            });
        model.onChanged.add([this](newui::Model&) {
            ++onChanged;
            return newui::SyncReturn::Handled;
            });
    }
};

}  // namespace

TEST(TextModel, SingleCharacterInsertFiresOnlyCharEvents) {
    TextModel model(L"hllo");
    TextModelEventCounters counters(model);

    model.insert(1, L"e");

    EXPECT_EQ(model.text(), L"hello");
    EXPECT_EQ(counters.beforeChar, 1);
    EXPECT_EQ(counters.afterChar, 1);
    EXPECT_EQ(counters.beforeRangeChanged, 0);
    EXPECT_EQ(counters.afterRangeChanged, 0);
    EXPECT_EQ(counters.onChanged, 1);
    EXPECT_EQ(counters.lastCharOffset, 1u);
    EXPECT_EQ(counters.lastChar, L'e');
    EXPECT_EQ(counters.lastCharKind, CharChangeKind::Inserted);
}

TEST(TextModel, MultiCharacterInsertFiresOnlyRangeChangedEvents) {
    TextModel model(L"hlo");
    TextModelEventCounters counters(model);

    model.insert(1, L"el");

    EXPECT_EQ(model.text(), L"hello");
    EXPECT_EQ(counters.beforeChar, 0);
    EXPECT_EQ(counters.afterChar, 0);
    EXPECT_EQ(counters.beforeRangeChanged, 1);
    EXPECT_EQ(counters.afterRangeChanged, 1);
    EXPECT_EQ(counters.lastRange, TextRange(1, 0));
    EXPECT_EQ(counters.lastReplacement, L"el");
}

TEST(TextModel, SingleCharacterRemoveFiresOnlyCharEventsWithTheRemovedCharacter) {
    TextModel model(L"hello");
    TextModelEventCounters counters(model);

    model.remove(TextRange(1, 1));  // removes 'e'

    EXPECT_EQ(model.text(), L"hllo");
    EXPECT_EQ(counters.beforeChar, 1);
    EXPECT_EQ(counters.afterChar, 1);
    EXPECT_EQ(counters.beforeRangeChanged, 0);
    EXPECT_EQ(counters.afterRangeChanged, 0);
    EXPECT_EQ(counters.lastCharOffset, 1u);
    EXPECT_EQ(counters.lastChar, L'e');
    EXPECT_EQ(counters.lastCharKind, CharChangeKind::Removed);
}

TEST(TextModel, MultiCharacterRemoveFiresOnlyRangeChangedEvents) {
    TextModel model(L"hello world");
    TextModelEventCounters counters(model);

    model.remove(TextRange(5, 6));

    EXPECT_EQ(model.text(), L"hello");
    EXPECT_EQ(counters.beforeChar, 0);
    EXPECT_EQ(counters.afterChar, 0);
    EXPECT_EQ(counters.beforeRangeChanged, 1);
    EXPECT_EQ(counters.afterRangeChanged, 1);
    EXPECT_EQ(counters.lastRange, TextRange(5, 6));
    EXPECT_EQ(counters.lastReplacement, L"");
}

TEST(TextModel, ReplaceAlwaysFiresRangeChangedEvenAtSingleCharacterLength) {
    TextModel model(L"hxllo");
    TextModelEventCounters counters(model);

    // A 1-for-1 replace isn't treated as a Char event - see TextModel's
    // own class comment on why replace() always goes through
    // RangeChanged regardless of length.
    model.replace(TextRange(1, 1), L"e");

    EXPECT_EQ(model.text(), L"hello");
    EXPECT_EQ(counters.beforeChar, 0);
    EXPECT_EQ(counters.afterChar, 0);
    EXPECT_EQ(counters.beforeRangeChanged, 1);
    EXPECT_EQ(counters.afterRangeChanged, 1);
    EXPECT_EQ(counters.lastRange, TextRange(1, 1));
    EXPECT_EQ(counters.lastReplacement, L"e");
}

TEST(TextModel, SetTextFiresRangeChangedWithTheFullOldRangeAndNewText) {
    TextModel model(L"hello");
    TextModelEventCounters counters(model);

    model.setText(L"goodbye");

    EXPECT_EQ(counters.beforeChar, 0);
    EXPECT_EQ(counters.beforeRangeChanged, 1);
    EXPECT_EQ(counters.lastRange, TextRange(0, 5));  // "hello"'s own length
    EXPECT_EQ(counters.lastReplacement, L"goodbye");
}

TEST(TextModel, ClearFiresRangeChangedWithTheFullOldRange) {
    TextModel model(L"hello");
    TextModelEventCounters counters(model);
    int onClearedCount = 0;
    model.onCleared.add([&](newui::Model&) {
        ++onClearedCount;
        return newui::SyncReturn::Handled;
        });

    model.clear();

    EXPECT_EQ(counters.beforeRangeChanged, 1);
    EXPECT_EQ(counters.afterRangeChanged, 1);
    EXPECT_EQ(counters.lastRange, TextRange(0, 5));
    EXPECT_EQ(counters.lastReplacement, L"");
    EXPECT_EQ(onClearedCount, 1);
}

TEST(TextModel, VetoingBeforeCharPreventsInsertAndSuppressesEverythingAfter) {
    TextModel model(L"hllo");
    model.onBeforeChar.add([](TextModel&, size_t, wchar_t, CharChangeKind, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
        });
    TextModelEventCounters counters(model);

    model.insert(1, L"e");

    EXPECT_EQ(model.text(), L"hllo");  // unchanged
    EXPECT_EQ(counters.afterChar, 0);
    EXPECT_EQ(counters.onChanged, 0);
}

TEST(TextModel, VetoingBeforeRangeChangedPreventsReplaceAndSuppressesEverythingAfter) {
    TextModel model(L"hello world");
    model.onBeforeRangeChanged.add([](TextModel&, const TextRange&, const std::wstring&, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
        });
    TextModelEventCounters counters(model);

    model.replace(TextRange(6, 5), L"there");

    EXPECT_EQ(model.text(), L"hello world");  // unchanged
    EXPECT_EQ(counters.afterRangeChanged, 0);
    EXPECT_EQ(counters.onChanged, 0);
}

TEST(TextModel, VetoingBeforeRangeChangedPreventsClearAndOnClearedDoesNotFire) {
    TextModel model(L"hello");
    model.onBeforeRangeChanged.add([](TextModel&, const TextRange&, const std::wstring&, bool& canChange) {
        canChange = false;
        return newui::SyncReturn::Handled;
        });
    int onClearedCount = 0;
    model.onCleared.add([&](newui::Model&) {
        ++onClearedCount;
        return newui::SyncReturn::Handled;
        });

    model.clear();

    EXPECT_EQ(model.text(), L"hello");  // unchanged
    EXPECT_EQ(onClearedCount, 0);
}

TEST(DirectWriteResources, FactoriesAreLiveAndUsable) {
    ID2D1Factory& d2d = DirectWriteResources::d2dFactory();
    FLOAT dpiX = 0.0f, dpiY = 0.0f;
    d2d.GetDesktopDpi(&dpiX, &dpiY);
    EXPECT_GT(dpiX, 0.0f);

    IDWriteFactory& dwrite = DirectWriteResources::dwriteFactory();
    IDWriteTextFormat* format = nullptr;
    HRESULT hr = dwrite.CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &format);
    EXPECT_TRUE(SUCCEEDED(hr));
    if (format != nullptr) {
        format->Release();
    }
}

TEST(DirectWriteResources, RepeatedCallsReturnTheSameSingletonInstance) {
    // Same Meyer's-singleton contract FontManager::instance() etc.
    // already have - two calls resolve to the same live object, not two
    // independently-created factories.
    EXPECT_EQ(&DirectWriteResources::d2dFactory(), &DirectWriteResources::d2dFactory());
    EXPECT_EQ(&DirectWriteResources::dwriteFactory(), &DirectWriteResources::dwriteFactory());
}

TEST(TextRenderer, RenderWithNonPositiveSizeIsANoOp) {
    TextRenderer renderer;
    BLImage image(10, 10, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    newui::Font font;
    newui::Color black(0.0f, 0.0f, 0.0f, 1.0f);
    renderer.render(ctx, 0, 10, L"hello", font, black);   // width <= 0
    renderer.render(ctx, 10, 0, L"hello", font, black);   // height <= 0
    ctx.end();

    BLImageData data;
    image.get_data(&data);
    const uint8_t* base = static_cast<const uint8_t*>(data.pixel_data);
    const uint32_t* row = reinterpret_cast<const uint32_t*>(base);
    EXPECT_EQ(row[0] >> 24, 0u);
}

TEST(TextRenderer, RenderDrawsVisiblePixelsForNonEmptyText) {
    TextRenderer renderer;
    BLImage image(120, 40, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    newui::Font font;
    font.setSize(16.0f);
    newui::Color black(0.0f, 0.0f, 0.0f, 1.0f);
    renderer.render(ctx, 120, 40, L"Hi", font, black);
    ctx.end();

    BLImageData data;
    image.get_data(&data);
    const uint8_t* base = static_cast<const uint8_t*>(data.pixel_data);
    bool sawInk = false;
    for (int y = 0; y < 40 && !sawInk; ++y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(base + y * data.stride);
        for (int x = 0; x < 120; ++x) {
            if ((row[x] >> 24) > 0) {
                sawInk = true;
                break;
            }
        }
    }
    EXPECT_TRUE(sawInk);
}

TEST(TextRenderer, RenderIsSafeAcrossRepeatedCallsWithTheSameFont) {
    // The very first render() on a fresh TextRenderer has to (re)create
    // its cached IDWriteTextFormat from scratch (starts null) - a second
    // render() with the exact same Font exercises ensureTextFormat()'s
    // cache-hit path instead. Neither is directly observable from outside,
    // but both have to run without crashing/misbehaving.
    TextRenderer renderer;
    BLImage image(120, 40, BL_FORMAT_PRGB32);
    BLContext ctx(image);

    newui::Font font;
    newui::Color black(0.0f, 0.0f, 0.0f, 1.0f);

    renderer.render(ctx, 120, 40, L"one", font, black);
    renderer.render(ctx, 120, 40, L"two", font, black);
    ctx.end();

    SUCCEED();
}

TEST(TextRenderer, RenderIsSafeAcrossRepeatedCallsWithADifferentFont) {
    // Exercises ensureTextFormat()'s cache-miss/recreate path - the
    // second call's Font differs (size), forcing the cached
    // IDWriteTextFormat to be dropped and rebuilt.
    TextRenderer renderer;
    BLImage image(120, 40, BL_FORMAT_PRGB32);
    BLContext ctx(image);

    newui::Font smallFont;
    smallFont.setSize(10.0f);
    newui::Font bigFont;
    bigFont.setSize(20.0f);
    newui::Color black(0.0f, 0.0f, 0.0f, 1.0f);

    renderer.render(ctx, 120, 40, L"one", smallFont, black);
    renderer.render(ctx, 120, 40, L"two", bigFont, black);
    ctx.end();

    SUCCEED();
}

TEST(TextLayoutEngine, HitTestMethodsReturnNothingBeforeAnySuccessfulUpdate) {
    TextLayoutEngine engine;

    EXPECT_TRUE(engine.hitTestRange(TextRange(0, 5)).empty());

    newui::Point topLeft(99.0f, 99.0f);
    float height = 99.0f;
    engine.hitTestPosition(TextPosition(0), topLeft, height);
    EXPECT_EQ(topLeft, newui::Point());
    EXPECT_EQ(height, 0.0f);

    EXPECT_FALSE(engine.hitTestPoint(newui::Point(5.0f, 5.0f)).isValid());
}

TEST(TextLayoutEngine, UpdateWithValidInputsSucceeds) {
    TextLayoutEngine engine;
    TextStorage storage(L"Hello World");
    newui::Font font;

    EXPECT_TRUE(engine.update(storage, font, 500.0f, 200.0f));
}

TEST(TextLayoutEngine, HitTestPositionReturnsAPositiveHeightForAValidPosition) {
    TextLayoutEngine engine;
    TextStorage storage(L"Hello World");
    newui::Font font;
    font.setSize(16.0f);
    ASSERT_TRUE(engine.update(storage, font, 500.0f, 200.0f));

    newui::Point topLeft;
    float height = 0.0f;
    engine.hitTestPosition(TextPosition(0), topLeft, height);

    EXPECT_GT(height, 0.0f);
    EXPECT_NEAR(topLeft.x, 0.0f, 0.5f);
    EXPECT_NEAR(topLeft.y, 0.0f, 0.5f);
}

TEST(TextLayoutEngine, HitTestPositionLeavesOutputsAtDefaultForAnInvalidPosition) {
    TextLayoutEngine engine;
    TextStorage storage(L"Hello World");
    newui::Font font;
    ASSERT_TRUE(engine.update(storage, font, 500.0f, 200.0f));

    newui::Point topLeft(99.0f, 99.0f);
    float height = 99.0f;
    engine.hitTestPosition(TextPosition(), topLeft, height);  // default = invalid

    EXPECT_EQ(topLeft, newui::Point());
    EXPECT_EQ(height, 0.0f);
}

TEST(TextLayoutEngine, HitTestRangeReturnsOneRectForARangeThatFitsOnASingleLine) {
    TextLayoutEngine engine;
    TextStorage storage(L"Hello World");
    newui::Font font;
    font.setSize(16.0f);
    // Wide/tall enough that "Hello World" needs no wrapping at all.
    ASSERT_TRUE(engine.update(storage, font, 500.0f, 200.0f));

    std::vector<newui::Rect> rects = engine.hitTestRange(TextRange(0, 5));  // "Hello"

    ASSERT_EQ(rects.size(), 1u);
    EXPECT_GT(rects[0].width(), 0.0f);
    EXPECT_GT(rects[0].height(), 0.0f);
}

TEST(TextLayoutEngine, HitTestRangeReturnsMultipleRectsForARangeSpanningWrappedLines) {
    TextLayoutEngine engine;
    // A long run of distinct words with a narrow maxWidth forces DirectWrite
    // to wrap across several lines - the whole-text range then has to span
    // more than one of those lines.
    TextStorage storage(L"AAAA BBBB CCCC DDDD EEEE FFFF");
    newui::Font font;
    font.setSize(20.0f);
    ASSERT_TRUE(engine.update(storage, font, 60.0f, 500.0f));

    std::vector<newui::Rect> rects = engine.hitTestRange(TextRange(0, storage.length()));

    EXPECT_GT(rects.size(), 1u);
}

TEST(TextLayoutEngine, HitTestRangeOfAnOutOfBoundsRangeDoesNotCrash) {
    // DirectWrite doesn't report "nothing" for a range entirely past the
    // end of the text - it reports a degenerate (zero-width) region at
    // the end-of-text position instead. This only asserts that calling
    // it with an out-of-bounds range is safe (no crash/exception) and
    // doesn't fabricate real (non-zero-width) selection geometry out of
    // nowhere - not that the result is empty.
    TextLayoutEngine engine;
    TextStorage storage(L"Hello");
    newui::Font font;
    ASSERT_TRUE(engine.update(storage, font, 500.0f, 200.0f));

    std::vector<newui::Rect> rects = engine.hitTestRange(TextRange(1000, 5));

    for (const newui::Rect& rect : rects) {
        EXPECT_FLOAT_EQ(rect.width(), 0.0f);
    }
}

TEST(TextLayoutEngine, HitTestPointReturnsAPositionNearTheStartForAPointNearTheOrigin) {
    TextLayoutEngine engine;
    TextStorage storage(L"Hello World");
    newui::Font font;
    font.setSize(16.0f);
    ASSERT_TRUE(engine.update(storage, font, 500.0f, 200.0f));

    TextPosition position = engine.hitTestPoint(newui::Point(1.0f, 5.0f));

    ASSERT_TRUE(position.isValid());
    EXPECT_LE(position.offset(), storage.length());
}

TEST(TextLayoutEngine, UpdateReflectsChangedTextOnASubsequentCall) {
    TextLayoutEngine engine;
    newui::Font font;
    font.setSize(16.0f);

    TextStorage shortStorage(L"Hi");
    ASSERT_TRUE(engine.update(shortStorage, font, 500.0f, 200.0f));
    std::vector<newui::Rect> shortRects = engine.hitTestRange(TextRange(0, shortStorage.length()));
    ASSERT_EQ(shortRects.size(), 1u);

    TextStorage longerStorage(L"Hello World, this is a longer line of text");
    ASSERT_TRUE(engine.update(longerStorage, font, 500.0f, 200.0f));
    std::vector<newui::Rect> longerRects = engine.hitTestRange(TextRange(0, longerStorage.length()));

    ASSERT_EQ(longerRects.size(), 1u);
    // A longer string laid out on the same single line should measure
    // wider - proves update() actually rebuilt against the new text
    // rather than reusing the first call's cached layout.
    EXPECT_GT(longerRects[0].width(), shortRects[0].width());
}
