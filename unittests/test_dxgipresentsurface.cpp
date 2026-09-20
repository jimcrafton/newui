#include "newui/dxgipresentsurface.h"
#include "newui/gdipresentsurface.h"
#include "newui/popuptool.h"
#include "newui/rootview.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <future>
#include <thread>

#include "presentsurface_test_helpers.h"

using presenttest::ScopedPlainWindow;
using presenttest::ScopedTargetDc;

namespace {

    // The DXGI-specific tests below need a real hardware D3D11 adapter. A machine without one (a
    // Remote Desktop session, a VM, no GPU driver) is a legitimate place to run this suite: the surface
    // falls back to GDI there by design, which the fallback tests cover - these just have nothing to
    // test, so they skip rather than fail.
#define SKIP_WITHOUT_DXGI(surface) \
    if ((surface).backend() != newui::PresentBackend::Dxgi) { \
        GTEST_SKIP() << "no usable D3D11 hardware adapter here - surface fell back to GDI"; \
    }

    std::uint32_t pixelAt(BLImage& image, int x, int y) {
        BLImageData data;
        image.get_data(&data);
        const auto* row = static_cast<const std::uint8_t*>(data.pixel_data) + intptr_t(y) * data.stride;
        return reinterpret_cast<const std::uint32_t*>(row)[x];
    }

    void fillRed(BLImage& image, double x, double y, double w, double h) {
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(0xFFFF0000));
        ctx.fill_rect(BLRect(x, y, w, h));
        ctx.end();
    }

    // Restores whatever defaultPresentBackend() was, however a test exits.
    struct DefaultBackendGuard {
        newui::PresentBackend saved = newui::defaultPresentBackend();
        ~DefaultBackendGuard() {
            newui::setDefaultPresentBackend(saved);
        }
    };

    // A real, visible top-level window a swap chain can present to and DWM will composite - the only way
    // to see what actually reached the screen (as opposed to "no call failed"). Tiny and non-activating.
    struct ScopedVisibleWindow {
        static constexpr int kSize = 64;
        HWND hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
            0, 0, kSize, kSize, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);

        ScopedVisibleWindow() {
            if (hwnd) {
                ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            }
        }

        ~ScopedVisibleWindow() {
            if (hwnd) {
                ::DestroyWindow(hwnd);
            }
        }
    };

    // The color DWM's own composition of hwnd has at (x, y) - CLR_INVALID if the capture itself failed.
    COLORREF composedPixel(HWND hwnd, int x, int y) {
        HDC screen = ::GetDC(nullptr);
        HDC mem = ::CreateCompatibleDC(screen);
        HBITMAP bmp = ::CreateCompatibleBitmap(screen, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize);
        HGDIOBJ old = ::SelectObject(mem, bmp);
        const BOOL ok = ::PrintWindow(hwnd, mem, 2 /* PW_RENDERFULLCONTENT: include DWM-composed (flip-model) content */);
        const COLORREF color = ok ? ::GetPixel(mem, x, y) : CLR_INVALID;
        ::SelectObject(mem, old);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
        ::ReleaseDC(nullptr, screen);
        return color;
    }

    // DWM composes a presented frame asynchronously, so poll briefly instead of sampling once.
    COLORREF composedPixelEventually(HWND hwnd, int x, int y, COLORREF expected) {
        COLORREF color = CLR_INVALID;
        for (int attempt = 0; attempt < 40; ++attempt) {
            color = composedPixel(hwnd, x, y);
            if (color == expected) {
                break;
            }
            ::Sleep(25);
        }
        return color;
    }

    void fillBlueWithRedSquare(BLImage& image) {
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(0xFF0000FF));  // opaque blue
        ctx.fill_all();
        ctx.set_fill_style(BLRgba32(0xFFFF0000));  // opaque red
        ctx.fill_rect(BLRect(10, 10, 20, 20));
        ctx.end();
    }

    const HWND kBogusWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1));

}

// ---- buffer, before any window exists ---------------------------------------------------------

TEST(DxgiPresentSurfaceResize, StartsInvalid) {
    newui::DxgiPresentSurface surface;

    EXPECT_FALSE(surface.isValid());
    EXPECT_TRUE(surface.image().is_empty());
}

TEST(DxgiPresentSurfaceResize, WithNoWindowYetAllocatesAZeroedCpuBufferAndStaysDxgi) {
    // RootView sizes its buffer (setBounds()) before its HWND exists - the GPU side has to wait.
    newui::DxgiPresentSurface surface;

    ASSERT_TRUE(surface.resize(37, 11, BL_FORMAT_PRGB32));

    EXPECT_TRUE(surface.isValid());
    EXPECT_EQ(surface.backend(), newui::PresentBackend::Dxgi);
    BLImageData data;
    surface.image().get_data(&data);
    EXPECT_EQ(data.size.w, 37);
    EXPECT_EQ(data.size.h, 11);
    EXPECT_EQ(surface.image().format(), BL_FORMAT_PRGB32);
    const auto* bytes = static_cast<const std::uint8_t*>(data.pixel_data);
    for (int y = 0; y < data.size.h; ++y) {
        for (int x = 0; x < data.size.w * 4; ++x) {
            ASSERT_EQ(bytes[intptr_t(y) * data.stride + x], 0) << "non-zero byte at row " << y << ", byte " << x;
        }
    }
}

TEST(DxgiPresentSurfaceResize, NonPositiveSizeReleasesAndReturnsFalse) {
    newui::DxgiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));

    EXPECT_FALSE(surface.resize(0, 32, BL_FORMAT_XRGB32));

    EXPECT_FALSE(surface.isValid());
    EXPECT_TRUE(surface.image().is_empty());
}

TEST(DxgiPresentSurfaceResize, ReleaseIsSafeToCallTwiceAndTheSurfaceIsReusableAfterward) {
    newui::DxgiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));

    surface.release();
    surface.release();
    EXPECT_FALSE(surface.isValid());

    ASSERT_TRUE(surface.resize(16, 16, BL_FORMAT_XRGB32));
    EXPECT_TRUE(surface.isValid());
}

// ---- falling back to GDI ---------------------------------------------------------------------

TEST(DxgiPresentSurfaceFallback, AWindowNoSwapChainCanBeMadeForFallsBackToGdiKeepingTheRenderedPixels) {
    // A bogus HWND fails CreateSwapChainForHwnd() (or, on a machine with no hardware D3D11 device at
    // all, fails earlier) - either way the surface has to end up presenting via GDI with the pixels
    // that were already rendered into it, not a blank buffer.
    newui::DxgiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    fillRed(surface.image(), 10, 5, 20, 10);
    surface.setWindow(kBogusWindow);

    surface.present(newui::Rect(0, 0, 64, 32));

    EXPECT_EQ(surface.backend(), newui::PresentBackend::Gdi);
    ASSERT_TRUE(surface.isValid());
    BLImageData data;
    surface.image().get_data(&data);
    EXPECT_EQ(data.size.w, 64);
    EXPECT_EQ(data.size.h, 32);
    EXPECT_EQ(pixelAt(surface.image(), 15, 8), 0xFFFF0000u) << "rendered pixel lost in the fallback";
    EXPECT_EQ(pixelAt(surface.image(), 2, 2), 0u);
}

TEST(DxgiPresentSurfaceFallback, FallenBackSurfaceKeepsWorkingThroughTheSameInterface) {
    newui::DxgiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    surface.setWindow(kBogusWindow);
    surface.present(newui::Rect(0, 0, 64, 32));
    ASSERT_EQ(surface.backend(), newui::PresentBackend::Gdi);

    ASSERT_TRUE(surface.resize(20, 10, BL_FORMAT_XRGB32));

    BLImageData data;
    surface.image().get_data(&data);
    EXPECT_EQ(data.size.w, 20);
    EXPECT_EQ(data.size.h, 10);
    EXPECT_EQ(surface.backend(), newui::PresentBackend::Gdi) << "a surface that gave up on DXGI must not retry";
}

TEST(DxgiPresentSurfaceFallback, FallenBackSurfacePaintsThroughGdiOnWmPaint) {
    newui::DxgiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    fillRed(surface.image(), 10, 5, 20, 10);
    surface.setWindow(kBogusWindow);
    surface.present(newui::Rect(0, 0, 64, 32));
    ASSERT_EQ(surface.backend(), newui::PresentBackend::Gdi);
    ScopedTargetDc target(64, 32);

    surface.paint(target.dc, newui::Rect(0, 0, 64, 32));

    EXPECT_EQ(::GetPixel(target.dc, 15, 8), RGB(255, 0, 0));
}

// ---- real DXGI presentation ------------------------------------------------------------------

TEST(DxgiPresentSurfacePresent, PresentingToARealWindowStaysOnDxgi) {
    ScopedPlainWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(100, 100, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);
    fillRed(surface.image(), 10, 10, 30, 30);

    surface.present(newui::Rect(0, 0, 100, 100));

    EXPECT_EQ(surface.backend(), newui::PresentBackend::Dxgi);
    EXPECT_TRUE(surface.isValid());
}

TEST(DxgiPresentSurfacePresent, AnyDirtyRectIsSafe) {
    ScopedPlainWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(100, 100, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);
    surface.present(newui::Rect(0, 0, 100, 100));  // first frame is always the whole buffer

    surface.present(newui::Rect(10, 10, 20, 20));         // ordinary
    surface.present(newui::Rect(10.5f, 10.25f, 20.5f, 20.75f));  // fractional - snapped outward
    surface.present(newui::Rect(-50, -50, 30, 30));       // entirely off the top-left
    surface.present(newui::Rect(80, 80, 500, 500));       // runs off the bottom-right
    surface.present(newui::Rect(200, 200, 10, 10));       // entirely outside
    surface.present(newui::Rect());                       // empty

    EXPECT_EQ(surface.backend(), newui::PresentBackend::Dxgi)
        << "a valid-but-odd dirty rect must never cause a fallback - reason: " << surface.fallbackReason();
}

TEST(DxgiPresentSurfacePresent, SurvivesAResizeSequence) {
    ScopedPlainWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(100, 100, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);

    const int sizes[][2] = { {100, 100}, {200, 150}, {50, 50}, {300, 200}, {1, 1}, {640, 480} };
    for (const auto& size : sizes) {
        ASSERT_TRUE(surface.resize(size[0], size[1], BL_FORMAT_XRGB32));
        surface.present(newui::Rect(0, 0, float(size[0]), float(size[1])));

        BLImageData data;
        surface.image().get_data(&data);
        EXPECT_EQ(data.size.w, size[0]);
        EXPECT_EQ(data.size.h, size[1]);
        EXPECT_EQ(surface.backend(), newui::PresentBackend::Dxgi) << "after resizing to " << size[0] << "x" << size[1];
    }
}

TEST(DxgiPresentSurfacePresent, MovingToADifferentWindowRebuildsTheSwapChain) {
    ScopedPlainWindow first;
    ScopedPlainWindow second;
    ASSERT_NE(first.hwnd, nullptr);
    ASSERT_NE(second.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(first.hwnd);
    ASSERT_TRUE(surface.resize(100, 100, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);
    surface.present(newui::Rect(0, 0, 100, 100));

    surface.setWindow(second.hwnd);
    surface.present(newui::Rect(0, 0, 100, 100));

    EXPECT_EQ(surface.backend(), newui::PresentBackend::Dxgi);
}

TEST(DxgiPresentSurfacePaint, IsPassiveWhileDxgiIsPresenting) {
    // DWM keeps the last presented frame on screen by itself; touching the window with GDI while a
    // flip-model swap chain owns it is what causes flicker.
    ScopedPlainWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);
    ScopedTargetDc target(64, 32);

    surface.paint(target.dc, newui::Rect(0, 0, 64, 32));

    EXPECT_EQ(::GetPixel(target.dc, 5, 5), RGB(0, 0, 255)) << "paint() must not blit anything";
}

// ---- what actually reaches the screen --------------------------------------------------------
// The tests above only prove nothing *failed*. These read the window back through DWM composition, so
// they fail if a frame is uploaded but never copied to the back buffer (a blank, black window) - a real
// bug this suite once let through.

TEST(DxgiPresentSurfaceOnScreen, APresentedFrameShowsUpInTheWindow) {
    ScopedVisibleWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);
    fillBlueWithRedSquare(surface.image());

    surface.present(newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize));

    EXPECT_EQ(composedPixelEventually(window.hwnd, 20, 20, RGB(255, 0, 0)), RGB(255, 0, 0)) << "inside the red square";
    EXPECT_EQ(composedPixelEventually(window.hwnd, 50, 50, RGB(0, 0, 255)), RGB(0, 0, 255)) << "the blue background";
    EXPECT_EQ(surface.backend(), newui::PresentBackend::Dxgi) << surface.fallbackReason();
}

TEST(DxgiPresentSurfaceOnScreen, APartialPresentUpdatesTheDirtyBoxAndKeepsTheRestOfTheFrame) {
    ScopedVisibleWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);
    fillBlueWithRedSquare(surface.image());
    surface.present(newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize));
    ASSERT_EQ(composedPixelEventually(window.hwnd, 20, 20, RGB(255, 0, 0)), RGB(255, 0, 0));

    // Repaint only a 10x10 box, green, and present just that box.
    {
        BLContext ctx(surface.image());
        ctx.set_fill_style(BLRgba32(0xFF00FF00));
        ctx.fill_rect(BLRect(40, 40, 10, 10));
        ctx.end();
    }
    surface.present(newui::Rect(40, 40, 10, 10));

    EXPECT_EQ(composedPixelEventually(window.hwnd, 44, 44, RGB(0, 255, 0)), RGB(0, 255, 0)) << "the dirty box itself";
    EXPECT_EQ(composedPixel(window.hwnd, 20, 20), RGB(255, 0, 0)) << "outside the box: must keep the earlier frame";
    EXPECT_EQ(composedPixel(window.hwnd, 5, 55), RGB(0, 0, 255)) << "outside the box: must keep the earlier frame";
    EXPECT_EQ(surface.backend(), newui::PresentBackend::Dxgi) << surface.fallbackReason();
}

TEST(DxgiPresentSurfaceOnScreen, AResizedFrameShowsTheNewContent) {
    ScopedVisibleWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(32, 32, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);
    surface.present(newui::Rect(0, 0, 32, 32));

    // Resize back up to the window's own size and present fresh content: the rebuilt texture must
    // start from the *new* buffer, not carry the old one's (or blank) pixels.
    ASSERT_TRUE(surface.resize(ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize, BL_FORMAT_XRGB32));
    fillBlueWithRedSquare(surface.image());
    surface.present(newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize));

    EXPECT_EQ(composedPixelEventually(window.hwnd, 20, 20, RGB(255, 0, 0)), RGB(255, 0, 0));
    EXPECT_EQ(composedPixelEventually(window.hwnd, 50, 50, RGB(0, 0, 255)), RGB(0, 0, 255));
}

TEST(DxgiPresentSurfaceOnScreen, RepeatedMinimizeRestoreCyclesKeepPresenting) {
    // What a Frame does on minimize: its size goes to 0x0 (resize(0, 0) - which frees the buffer and the GPU
    // side), then back to a real size on restore, over and over.
    ScopedVisibleWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    newui::DxgiPresentSurface surface;
    surface.setWindow(window.hwnd);
    ASSERT_TRUE(surface.resize(ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize, BL_FORMAT_XRGB32));
    SKIP_WITHOUT_DXGI(surface);

    for (int cycle = 0; cycle < 6; ++cycle) {
        ASSERT_FALSE(surface.resize(0, 0, BL_FORMAT_XRGB32)) << "cycle " << cycle;
        EXPECT_FALSE(surface.isValid());
        surface.present(newui::Rect(0, 0, 0, 0));  // a stray present while minimized must be harmless

        ASSERT_TRUE(surface.resize(ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize, BL_FORMAT_XRGB32)) << "cycle " << cycle;
        // A different red each cycle, so a frame left over from an earlier cycle can't pass by accident.
        const std::uint32_t red = 60 + cycle * 30;
        {
            BLContext ctx(surface.image());
            ctx.set_fill_style(BLRgba32(0xFF000000u | (red << 16)));
            ctx.fill_all();
            ctx.end();
        }
        surface.present(newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize));

        EXPECT_EQ(composedPixelEventually(window.hwnd, 30, 30, RGB(red, 0, 0)), RGB(red, 0, 0)) << "cycle " << cycle;
        ASSERT_EQ(surface.backend(), newui::PresentBackend::Dxgi) << "cycle " << cycle << ": " << surface.fallbackReason();
    }
}

TEST(DxgiPresentSurfaceOnScreen, ASurfaceUsedEntirelyOnAWorkerThreadPresentsToAWindowOwnedByAnotherThread) {
    // The VSIX-host shape: the window belongs to (and this test's thread is) one thread that is NOT pumping
    // messages while the surface - created, sized, presented to and destroyed on a worker thread - does its
    // work. Anything in swap-chain creation/resize/present that sent a message to the window's thread would
    // deadlock here, so the worker is watched with a timeout instead of joined blindly.
    ScopedVisibleWindow window;
    ASSERT_NE(window.hwnd, nullptr);

    struct Outcome {
        bool sized = false;
        newui::PresentBackend backend = newui::PresentBackend::Gdi;
        std::string reason;
    };
    auto outcome = std::make_shared<Outcome>();
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    const HWND hwnd = window.hwnd;

    std::thread worker([outcome, done, hwnd]() {
        {
            newui::DxgiPresentSurface surface;
            surface.setWindow(hwnd);
            outcome->sized = surface.resize(ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize, BL_FORMAT_XRGB32);
            if (outcome->sized) {
                fillBlueWithRedSquare(surface.image());
                surface.present(newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize));
                // Resize while the owning thread still isn't pumping (ResizeBuffers path), then a partial present.
                outcome->sized = surface.resize(ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize, BL_FORMAT_XRGB32);
                fillBlueWithRedSquare(surface.image());
                surface.present(newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize));
                surface.present(newui::Rect(5, 5, 10, 10));
            }
            outcome->backend = surface.backend();
            outcome->reason = surface.fallbackReason();
        }  // surface (and its swap chain) destroyed on the worker, before the window
        done->set_value();
    });

    if (finished.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        worker.detach();  // it's stuck - don't hang the whole suite on a join
        FAIL() << "the worker deadlocked using a surface on a window owned by a thread that isn't pumping messages";
    }
    worker.join();

    EXPECT_TRUE(outcome->sized);
    if (outcome->backend != newui::PresentBackend::Dxgi) {
        GTEST_SKIP() << "no usable D3D11 hardware adapter here - surface fell back to GDI: " << outcome->reason;
    }
    // Back on the window's own thread: the worker's last present must have reached the screen.
    EXPECT_EQ(composedPixelEventually(window.hwnd, 20, 20, RGB(255, 0, 0)), RGB(255, 0, 0));
    EXPECT_EQ(composedPixelEventually(window.hwnd, 50, 50, RGB(0, 0, 255)), RGB(0, 0, 255));
}

// ---- choosing a backend ----------------------------------------------------------------------

TEST(PresentBackendSelection, ParsesGdiAndDxgiCaseInsensitively) {
    newui::PresentBackend out = newui::PresentBackend::Gdi;

    EXPECT_TRUE(newui::parsePresentBackend("dxgi", out));
    EXPECT_EQ(out, newui::PresentBackend::Dxgi);
    EXPECT_TRUE(newui::parsePresentBackend("GDI", out));
    EXPECT_EQ(out, newui::PresentBackend::Gdi);
    EXPECT_TRUE(newui::parsePresentBackend("DxGi", out));
    EXPECT_EQ(out, newui::PresentBackend::Dxgi);
}

TEST(PresentBackendSelection, RejectsAnythingElseAndLeavesTheOutputAlone) {
    newui::PresentBackend out = newui::PresentBackend::Dxgi;

    EXPECT_FALSE(newui::parsePresentBackend("", out));
    EXPECT_FALSE(newui::parsePresentBackend("d3d", out));
    EXPECT_FALSE(newui::parsePresentBackend("auto", out));
    EXPECT_FALSE(newui::parsePresentBackend(" gdi", out));
    EXPECT_EQ(out, newui::PresentBackend::Dxgi);
}

TEST(PresentBackendSelection, CreatePresentSurfaceGivesTheRequestedKind) {
    auto gdi = newui::createPresentSurface(newui::PresentBackend::Gdi);
    auto dxgi = newui::createPresentSurface(newui::PresentBackend::Dxgi);

    ASSERT_NE(gdi, nullptr);
    ASSERT_NE(dxgi, nullptr);
    EXPECT_NE(dynamic_cast<newui::GdiPresentSurface*>(gdi.get()), nullptr);
    EXPECT_NE(dynamic_cast<newui::DxgiPresentSurface*>(dxgi.get()), nullptr);
    EXPECT_EQ(gdi->backend(), newui::PresentBackend::Gdi);
    EXPECT_EQ(dxgi->backend(), newui::PresentBackend::Dxgi);
}

TEST(PresentBackendSelection, SetDefaultChangesWhatDefaultReports) {
    DefaultBackendGuard guard;

    newui::setDefaultPresentBackend(newui::PresentBackend::Dxgi);
    EXPECT_EQ(newui::defaultPresentBackend(), newui::PresentBackend::Dxgi);
    newui::setDefaultPresentBackend(newui::PresentBackend::Gdi);
    EXPECT_EQ(newui::defaultPresentBackend(), newui::PresentBackend::Gdi);
}

TEST(PresentBackendSelection, ANewRootViewUsesTheCurrentDefault) {
    DefaultBackendGuard guard;

    newui::setDefaultPresentBackend(newui::PresentBackend::Gdi);
    auto* gdiRoot = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "gdiRoot");
    newui::setDefaultPresentBackend(newui::PresentBackend::Dxgi);
    auto* dxgiRoot = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "dxgiRoot");

    EXPECT_EQ(gdiRoot->presentBackend(), newui::PresentBackend::Gdi);
    EXPECT_EQ(dxgiRoot->presentBackend(), newui::PresentBackend::Dxgi);

    gdiRoot->destroy();
    delete gdiRoot;
    dxgiRoot->destroy();
    delete dxgiRoot;
}

TEST(PresentBackendSelection, APopupToolStaysOnGdiWhateverTheDefaultIs) {
    // A layered popup is presented via UpdateLayeredWindow(), which a flip-model swap chain can't do.
    DefaultBackendGuard guard;
    newui::setDefaultPresentBackend(newui::PresentBackend::Dxgi);

    auto* popup = new newui::PopupTool(nullptr, nullptr, newui::Rect(0, 0, 100, 100), "popup");

    EXPECT_EQ(popup->presentBackend(), newui::PresentBackend::Gdi);

    popup->destroy();
    delete popup;
}

// ---- RootView::invalidate() under DXGI -------------------------------------------------------
// invalidate() used to be a bare ::InvalidateRect(), which does nothing under DXGI (WM_PAINT is
// passive there), so content drawn straight into getImageBuffer() and then "invalidated" never showed.

TEST(RootViewInvalidateOnScreen, DrawingIntoTheBufferThenInvalidatingShowsItUnderDxgi) {
    DefaultBackendGuard guard;
    newui::setDefaultPresentBackend(newui::PresentBackend::Dxgi);
    ScopedVisibleWindow parent;
    ASSERT_NE(parent.hwnd, nullptr);
    auto* root = new newui::RootView(parent.hwnd, ::GetModuleHandleW(nullptr),
        newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize), "root");
    ASSERT_TRUE(root->initialize());
    if (root->presentBackend() != newui::PresentBackend::Dxgi) {
        root->destroy();
        delete root;
        GTEST_SKIP() << "no usable D3D11 hardware adapter here - fell back to GDI";
    }

    {
        BLContext ctx(root->getImageBuffer());
        ctx.set_fill_style(BLRgba32(0xFFFF0000));
        ctx.fill_rect(BLRect(10, 10, 30, 30));
        ctx.end();
    }
    root->invalidate();

    EXPECT_EQ(composedPixelEventually(parent.hwnd, 20, 20, RGB(255, 0, 0)), RGB(255, 0, 0));

    root->destroy();
    delete root;
}

TEST(RootViewInvalidateOnScreen, InvalidatingJustARegionShowsJustThatRegionUnderDxgi) {
    DefaultBackendGuard guard;
    newui::setDefaultPresentBackend(newui::PresentBackend::Dxgi);
    ScopedVisibleWindow parent;
    ASSERT_NE(parent.hwnd, nullptr);
    auto* root = new newui::RootView(parent.hwnd, ::GetModuleHandleW(nullptr),
        newui::Rect(0, 0, ScopedVisibleWindow::kSize, ScopedVisibleWindow::kSize), "root");
    ASSERT_TRUE(root->initialize());
    if (root->presentBackend() != newui::PresentBackend::Dxgi) {
        root->destroy();
        delete root;
        GTEST_SKIP() << "no usable D3D11 hardware adapter here - fell back to GDI";
    }

    {
        BLContext ctx(root->getImageBuffer());
        ctx.set_fill_style(BLRgba32(0xFFFF0000));
        ctx.fill_rect(BLRect(0, 0, 20, 20));    // inside the region invalidated below
        ctx.set_fill_style(BLRgba32(0xFF0000FF));
        ctx.fill_rect(BLRect(40, 40, 20, 20));  // outside it: drawn into the buffer but never presented
        ctx.end();
    }
    newui::Rect region(0, 0, 20, 20);
    root->invalidate(&region);

    EXPECT_EQ(composedPixelEventually(parent.hwnd, 10, 10, RGB(255, 0, 0)), RGB(255, 0, 0)) << "the invalidated region";
    EXPECT_NE(composedPixel(parent.hwnd, 50, 50), RGB(0, 0, 255)) << "outside the region: not presented yet";

    root->destroy();
    delete root;
}
