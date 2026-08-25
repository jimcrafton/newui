#include "newui/clipboardmgr.h"
#include "newui/frame.h"
#include "newui/rootview.h"

#include <gtest/gtest.h>

#include <algorithm>

TEST(ClipboardManager, SetTextThenGetTextRoundTripsUnicodeContent) {
    std::wstring written = L"Hello, 世界! éèê";
    ASSERT_TRUE(newui::ClipboardManager::setText(written));

    std::wstring read;
    ASSERT_TRUE(newui::ClipboardManager::getText(read));
    EXPECT_EQ(read, written);
}

TEST(ClipboardManager, SetTextWithEmptyStringRoundTrips) {
    ASSERT_TRUE(newui::ClipboardManager::setText(L""));

    std::wstring read;
    ASSERT_TRUE(newui::ClipboardManager::getText(read));
    EXPECT_EQ(read, L"");
}

TEST(ClipboardManager, GetTextReturnsFalseWhenClipboardHoldsNoText) {
    ASSERT_TRUE(::OpenClipboard(nullptr));
    ::EmptyClipboard();
    ::CloseClipboard();

    std::wstring read = L"unchanged";
    EXPECT_FALSE(newui::ClipboardManager::getText(read));
    EXPECT_EQ(read, L"unchanged");
}

TEST(ClipboardManager, RegisterCustomFormatIsStableAcrossCalls) {
    UINT first = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test");
    UINT second = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test");

    EXPECT_NE(first, 0u);
    EXPECT_EQ(first, second);
}

TEST(ClipboardManager, RegisterCustomFormatRoundTripsTheNameViaGetClipboardFormatNameW) {
    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-roundtrip");
    ASSERT_NE(format, 0u);

    wchar_t buffer[256] = {};
    int length = ::GetClipboardFormatNameW(format, buffer, 256);
    ASSERT_GT(length, 0);
    EXPECT_EQ(std::wstring(buffer, static_cast<std::size_t>(length)), L"application/x-newui-test-roundtrip");
}

TEST(ClipboardManager, SetCustomDataThenGetCustomDataRoundTrips) {
    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-custom-data");
    std::vector<std::uint8_t> written = {1, 2, 3, 4, 5, 250};

    ASSERT_TRUE(newui::ClipboardManager::setCustomData(format, written));

    std::vector<std::uint8_t> read;
    ASSERT_TRUE(newui::ClipboardManager::getCustomData(format, read));
    EXPECT_EQ(read, written);
}

TEST(ClipboardManager, SetCustomDataWithEmptyDataSucceeds) {
    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-custom-data-empty");

    // GlobalAlloc()/SetClipboardData() don't reliably support a genuine
    // zero-byte block (confirmed live - setCustomData() pads to at least
    // 1 byte internally to work around it, see clipboardmgr.cpp), so
    // this only confirms writing "no real content" doesn't fail outright -
    // not that any particular byte count comes back.
    ASSERT_TRUE(newui::ClipboardManager::setCustomData(format, {}));

    std::vector<std::uint8_t> read;
    EXPECT_TRUE(newui::ClipboardManager::getCustomData(format, read));
}

TEST(ClipboardManager, GetCustomDataReturnsFalseForAFormatNotOnTheClipboard) {
    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-custom-data-absent");
    ASSERT_TRUE(::OpenClipboard(nullptr));
    ::EmptyClipboard();
    ::CloseClipboard();

    std::vector<std::uint8_t> read;
    EXPECT_FALSE(newui::ClipboardManager::getCustomData(format, read));
}

TEST(ClipboardManager, SetDelayedRendererFailsWhenOwnerHasNoRealWindow) {
    // Never initialize()'d - windowHandle() stays nullptr, same pattern
    // test_rootview.cpp's TestableRootView instances rely on.
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "root");
    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-delayed-no-window");

    bool rendererCalled = false;
    bool result = newui::ClipboardManager::setDelayedRenderer(
        format,
        [&rendererCalled]() { rendererCalled = true; return std::vector<std::uint8_t>{1}; },
        &root);

    EXPECT_FALSE(result);
    EXPECT_FALSE(rendererCalled);
    newui::ClipboardManager::clearDelayedRenderer(format);
}

TEST(ClipboardManager, DelayedRendererProducesDataOnlyWhenAskedFor) {
    newui::Frame frame;
    ASSERT_TRUE(frame.initialize());

    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-delayed-render-format");
    std::vector<std::uint8_t> payload = {9, 8, 7, 6};
    int renderCount = 0;

    ASSERT_TRUE(newui::ClipboardManager::setDelayedRenderer(
        format,
        [payload, &renderCount]() { ++renderCount; return payload; },
        &frame.rootView()));

    EXPECT_EQ(renderCount, 0);

    // Simulates what RootView::handleMessage()'s WM_RENDERFORMAT case
    // does - a unit test has no second real process available to
    // actually trigger the message, but WM_RENDERFORMAT's own contract
    // (clipboard already open) is easy to reproduce directly.
    ASSERT_TRUE(::OpenClipboard(frame.rootView().windowHandle()));
    newui::ClipboardManager::handleRenderFormat(format);
    ::CloseClipboard();

    EXPECT_EQ(renderCount, 1);

    std::vector<std::uint8_t> read;
    ASSERT_TRUE(newui::ClipboardManager::getCustomData(format, read));
    EXPECT_EQ(read, payload);

    newui::ClipboardManager::clearDelayedRenderer(format);
    // destroy() is protected (Frame::handleMessage()'s own WM_DESTROY
    // case is the real caller) - DestroyWindow() is the normal Win32
    // teardown path, and Frame's destructor throws if frameHandle_ is
    // still live when it runs.
    ::DestroyWindow(frame.frameHandle());
}

TEST(ClipboardManager, HandleRenderAllFormatsRendersEveryRegisteredFormat) {
    newui::Frame frame;
    ASSERT_TRUE(frame.initialize());

    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-render-all-formats");
    std::vector<std::uint8_t> payload = {42};

    ASSERT_TRUE(newui::ClipboardManager::setDelayedRenderer(
        format,
        [payload]() { return payload; },
        &frame.rootView()));

    // Unlike handleRenderFormat() above, WM_RENDERALLFORMATS' own
    // contract is that the *handler* opens the clipboard itself -
    // handleRenderAllFormats() does that internally.
    newui::ClipboardManager::handleRenderAllFormats(frame.rootView().windowHandle());

    std::vector<std::uint8_t> read;
    ASSERT_TRUE(newui::ClipboardManager::getCustomData(format, read));
    EXPECT_EQ(read, payload);

    newui::ClipboardManager::clearDelayedRenderer(format);
    // destroy() is protected (Frame::handleMessage()'s own WM_DESTROY
    // case is the real caller) - DestroyWindow() is the normal Win32
    // teardown path, and Frame's destructor throws if frameHandle_ is
    // still live when it runs.
    ::DestroyWindow(frame.frameHandle());
}

TEST(ClipboardManager, ClearDelayedRendererStopsItFromBeingInvoked) {
    newui::Frame frame;
    ASSERT_TRUE(frame.initialize());

    UINT format = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-test-clear-delayed");
    bool rendererCalled = false;

    ASSERT_TRUE(newui::ClipboardManager::setDelayedRenderer(
        format,
        [&rendererCalled]() { rendererCalled = true; return std::vector<std::uint8_t>{1}; },
        &frame.rootView()));

    newui::ClipboardManager::clearDelayedRenderer(format);

    ASSERT_TRUE(::OpenClipboard(frame.rootView().windowHandle()));
    newui::ClipboardManager::handleRenderFormat(format);
    ::CloseClipboard();

    EXPECT_FALSE(rendererCalled);
    ::DestroyWindow(frame.frameHandle());
}

// --- Phase 4: MIME type mapping layer ---

TEST(ClipboardDetail, WrapCfHtmlThenUnwrapRoundTripsTheFragmentExactly) {
    std::vector<std::uint8_t> rawHtml = {'<', 'b', '>', 'H', 'i', '<', '/', 'b', '>'};

    std::vector<std::uint8_t> wrapped = newui::detail::wrapCfHtml(rawHtml);
    std::string wrappedText(wrapped.begin(), wrapped.end());
    EXPECT_NE(wrappedText.find("Version:0.9"), std::string::npos);
    EXPECT_NE(wrappedText.find("<!--StartFragment-->"), std::string::npos);

    std::vector<std::uint8_t> unwrapped = newui::detail::unwrapCfHtml(wrapped);
    EXPECT_EQ(unwrapped, rawHtml);
}

TEST(ClipboardDetail, UnwrapCfHtmlReturnsInputUnchangedWhenHeaderTokensAreMissing) {
    std::vector<std::uint8_t> notCfHtml = {'j', 'u', 's', 't', ' ', 'b', 'y', 't', 'e', 's'};
    EXPECT_EQ(newui::detail::unwrapCfHtml(notCfHtml), notCfHtml);
}

TEST(ClipboardManager, GetOrRegisterFormatReturnsThePreSeededStandardFormatForTextPlain) {
    EXPECT_EQ(newui::ClipboardManager::getOrRegisterFormat(L"text/plain"), static_cast<UINT>(CF_UNICODETEXT));
}

TEST(ClipboardManager, GetOrRegisterFormatIsStableForACustomMimeType) {
    UINT first = newui::ClipboardManager::getOrRegisterFormat(L"application/x-newui-test-mime");
    UINT second = newui::ClipboardManager::getOrRegisterFormat(L"application/x-newui-test-mime");
    EXPECT_NE(first, 0u);
    EXPECT_EQ(first, second);
}

TEST(ClipboardManager, FormatNameResolvesAStandardMimeTypesFormatBack) {
    EXPECT_EQ(newui::ClipboardManager::formatName(CF_UNICODETEXT), L"text/plain");
}

TEST(ClipboardManager, FormatNameFallsBackToGetClipboardFormatNameWForAnUnknownFormat) {
    // Registered directly, bypassing getOrRegisterFormat() - not in
    // ClipboardManager's own cache, so formatName() has to fall back to
    // GetClipboardFormatNameW() to resolve it, the same way it would for
    // a format some *other* process registered first.
    UINT format = ::RegisterClipboardFormatW(L"application/x-newui-test-external-format");
    ASSERT_NE(format, 0u);

    EXPECT_EQ(newui::ClipboardManager::formatName(format), L"application/x-newui-test-external-format");
}

TEST(ClipboardManager, SetMimeDataThenGetMimeDataRoundTripsApplicationJson) {
    std::vector<std::uint8_t> written = {'{', '"', 'a', '"', ':', '1', '}'};
    ASSERT_TRUE(newui::ClipboardManager::setMimeData(L"application/json", written));

    std::vector<std::uint8_t> read;
    ASSERT_TRUE(newui::ClipboardManager::getMimeData(L"application/json", read));
    EXPECT_EQ(read, written);
}

TEST(ClipboardManager, SetMimeDataWrapsTextHtmlAndGetMimeDataUnwrapsIt) {
    std::vector<std::uint8_t> rawHtml = {'<', 'b', '>', 'H', 'i', '<', '/', 'b', '>'};
    ASSERT_TRUE(newui::ClipboardManager::setMimeData(L"text/html", rawHtml));

    // The raw clipboard payload under "HTML Format" is the *wrapped*
    // form, not rawHtml verbatim.
    std::vector<std::uint8_t> wrapped;
    ASSERT_TRUE(newui::ClipboardManager::getCustomData(newui::ClipboardManager::getOrRegisterFormat(L"text/html"), wrapped));
    std::string wrappedText(wrapped.begin(), wrapped.end());
    EXPECT_NE(wrappedText.find("StartFragment"), std::string::npos);

    // getMimeData() transparently unwraps it back to the original.
    std::vector<std::uint8_t> read;
    ASSERT_TRUE(newui::ClipboardManager::getMimeData(L"text/html", read));
    EXPECT_EQ(read, rawHtml);
}

TEST(ClipboardManager, GetAvailableMimeTypesIncludesAKnownWrittenFormat) {
    ASSERT_TRUE(newui::ClipboardManager::setText(L"some text"));

    std::vector<std::wstring> mimeTypes = newui::ClipboardManager::getAvailableMimeTypes();
    EXPECT_NE(std::find(mimeTypes.begin(), mimeTypes.end(), L"text/plain"), mimeTypes.end());
}

TEST(ClipboardManager, SetFileListThenGetFileListRoundTrips) {
    std::vector<std::wstring> written = {L"C:\\fake\\file1.txt", L"C:\\fake\\file2.txt"};
    ASSERT_TRUE(newui::ClipboardManager::setFileList(written));

    std::vector<std::wstring> read;
    ASSERT_TRUE(newui::ClipboardManager::getFileList(read));
    EXPECT_EQ(read, written);
}

TEST(ClipboardManager, SetFileListWithEmptyListFails) {
    EXPECT_FALSE(newui::ClipboardManager::setFileList({}));
}

TEST(ClipboardManager, GetFileListReturnsFalseWhenClipboardHasNoFileList) {
    ASSERT_TRUE(::OpenClipboard(nullptr));
    ::EmptyClipboard();
    ::CloseClipboard();

    std::vector<std::wstring> read;
    EXPECT_FALSE(newui::ClipboardManager::getFileList(read));
}

TEST(ClipboardManager, GetPreferredDropEffectDefaultsToCopyWhenAbsent) {
    ASSERT_TRUE(::OpenClipboard(nullptr));
    ::EmptyClipboard();
    ::CloseClipboard();

    EXPECT_EQ(newui::ClipboardManager::getPreferredDropEffect(), newui::ClipboardManager::DropEffect::Copy);
}

TEST(ClipboardManager, SetFileListWithMoveEffectRoundTripsThroughGetPreferredDropEffect) {
    ASSERT_TRUE(newui::ClipboardManager::setFileList({L"C:\\fake\\cut-me.txt"}, newui::ClipboardManager::DropEffect::Move));
    EXPECT_EQ(newui::ClipboardManager::getPreferredDropEffect(), newui::ClipboardManager::DropEffect::Move);
}

TEST(ClipboardManager, SetImageThenGetImageRoundTripsDimensionsAndPixels) {
    BLImage written(4, 4, BL_FORMAT_PRGB32);
    {
        BLContext ctx(written);
        ctx.set_fill_style(BLRgba32(0xFFFF3300));
        ctx.fill_all();
        ctx.end();
    }

    ASSERT_TRUE(newui::ClipboardManager::setImage(written));

    BLImage read;
    ASSERT_TRUE(newui::ClipboardManager::getImage(read));
    EXPECT_EQ(read.size().w, written.size().w);
    EXPECT_EQ(read.size().h, written.size().h);

    BLImageData readData;
    ASSERT_EQ(read.get_data(&readData), BL_SUCCESS);
    auto* pixel = static_cast<const std::uint8_t*>(readData.pixel_data);
    // BGRA byte order in memory (little-endian PRGB32/XRGB32) - opaque
    // fill, so alpha and RGB should survive the CF_DIB round trip
    // exactly regardless of whether Blend2D's BMP codec dropped down to
    // 24bpp along the way.
    EXPECT_EQ(pixel[0], 0x00); // B
    EXPECT_EQ(pixel[1], 0x33); // G
    EXPECT_EQ(pixel[2], 0xFF); // R
}

TEST(ClipboardManager, SetImageWithAnEmptyImageFails) {
    BLImage empty;
    EXPECT_FALSE(newui::ClipboardManager::setImage(empty));
}

TEST(ClipboardManager, GetImageReturnsFalseWhenClipboardHasNoImage) {
    ASSERT_TRUE(::OpenClipboard(nullptr));
    ::EmptyClipboard();
    ::CloseClipboard();

    BLImage read;
    EXPECT_FALSE(newui::ClipboardManager::getImage(read));
}
