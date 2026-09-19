// Phase 1c feasibility spike for swapchain-plan.md: a DXGI flip-model swap chain on a WS_CHILD HWND
// whose PARENT lives on a different thread - the shape of the cpp_codetools VSIX host (RootView on a
// dedicated thread under a VS-owned parent). Plain Win32, no newui View classes: this isolates the
// window-composition question from RootView.
//
// Threading rules this spike deliberately follows (and would break if they were wrong):
//  - The parent thread NEVER SendMessage()s to the child, and never calls SetWindowPos/MoveWindow/
//    ShowWindow on it (those send messages internally). It only PostMessage()s; the child thread does
//    its own SetWindowPos().
//  - The parent thread never blocks waiting on the child thread. CreateWindowEx()/DestroyWindow() of a
//    cross-thread child SendMessage() WM_PARENTNOTIFY to the parent, so a parent blocked in join() or a
//    condvar wait would deadlock (HANDOFF Part 92's EditThreadHost bug). Shutdown is a posted handshake.
//
// Run with --stall to make the parent thread Sleep(1500) inside every WM_SIZE: the child's animation
// must keep running through it.

#include <blend2d/blend2d.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;

namespace {

    constexpr UINT WM_APP_CHILD_READY = WM_APP + 1;  // child -> parent, lParam = child HWND
    constexpr UINT WM_APP_CHILD_GONE = WM_APP + 2;   // child -> parent, child window destroyed
    constexpr UINT WM_APP_LAYOUT = WM_APP + 3;       // parent -> child, wParam = MAKELONG(x,y), lParam = MAKELONG(w,h)
    constexpr UINT WM_APP_QUIT = WM_APP + 4;         // parent -> child

    constexpr int kHeaderHeight = 40;  // parent-painted band above the child

    bool g_stall = false;
    HWND g_parent = nullptr;
    HWND g_child = nullptr;  // only ever touched on the parent thread, set from WM_APP_CHILD_READY
    bool g_childGone = false;

    bool check(HRESULT hr, const char* what) {
        if (FAILED(hr)) {
            std::printf("swapchain2: %s failed, hr=0x%08lX\n", what, static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }

    // Everything below the child thread's own window - device, swap chain, staging texture. Only ever
    // used from the child thread.
    struct Presenter {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain1> swapChain;
        ComPtr<ID3D11Texture2D> staging;
        UINT width = 0, height = 0;
        ULONGLONG start = GetTickCount64();
        unsigned frames = 0;
        ULONGLONG lastReport = GetTickCount64();

        bool create(HWND hwnd, UINT w, UINT h) {
            D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0 };
            if (!check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    fl, 1, D3D11_SDK_VERSION, &device, nullptr, &context), "D3D11CreateDevice")) {
                return false;
            }
            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            if (!check(device.As(&dxgiDevice), "IDXGIDevice") || !check(dxgiDevice->GetAdapter(&adapter), "GetAdapter") ||
                !check(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIFactory2")) {
                return false;
            }
            DXGI_SWAP_CHAIN_DESC1 sd = {};
            sd.Width = w;
            sd.Height = h;
            sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.Scaling = DXGI_SCALING_NONE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            return check(factory->CreateSwapChainForHwnd(device.Get(), hwnd, &sd, nullptr, nullptr, &swapChain),
                "CreateSwapChainForHwnd(child)") && resize(w, h);
        }

        bool resize(UINT w, UINT h) {
            if (!swapChain || w == 0 || h == 0) {
                return false;
            }
            staging.Reset();
            if (!check(swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers")) {
                return false;
            }
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = w;
            desc.Height = h;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (!check(device->CreateTexture2D(&desc, nullptr, &staging), "CreateTexture2D(staging)")) {
                return false;
            }
            width = w;
            height = h;
            return true;
        }

        void renderAndPresent() {
            if (!staging || !swapChain) {
                return;
            }
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (!check(context->Map(staging.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map")) {
                return;
            }
            {
                BLImage img;
                img.create_from_data(int(width), int(height), BL_FORMAT_PRGB32, mapped.pData, intptr_t(mapped.RowPitch));
                BLContext ctx(img);

                const double w = double(width), h = double(height);
                BLGradient bg(BLLinearGradientValues(0, 0, 0, h));
                bg.add_stop(0.0, BLRgba32(0xFF14532D));
                bg.add_stop(1.0, BLRgba32(0xFF052E16));
                ctx.set_fill_style(bg);
                ctx.fill_all();

                // Orbiting dot driven by wall-clock time: successive screenshots differ iff the child thread
                // is genuinely still presenting.
                const double t = double(GetTickCount64() - start) / 1000.0;
                const double r = (w < h ? w : h) * 0.3;
                ctx.set_fill_style(BLRgba32(0xFFFACC15));
                ctx.fill_circle(BLCircle(w * 0.5 + r * std::cos(t * 2.0), h * 0.5 + r * std::sin(t * 2.0), 18.0));

                // Edge frame, same purpose as swapchain1: a size/position mismatch shows as a missing side.
                ctx.set_fill_style(BLRgba32(0xFFEF4444));
                const double e = 4.0;
                ctx.fill_rect(BLRect(0, 0, w, e));
                ctx.fill_rect(BLRect(0, h - e, w, e));
                ctx.fill_rect(BLRect(0, 0, e, h));
                ctx.fill_rect(BLRect(w - e, 0, e, h));
                ctx.end();
            }
            context->Unmap(staging.Get(), 0);

            ComPtr<ID3D11Texture2D> back;
            if (!check(swapChain->GetBuffer(0, IID_PPV_ARGS(&back)), "GetBuffer")) {
                return;
            }
            context->CopyResource(back.Get(), staging.Get());
            DXGI_PRESENT_PARAMETERS pp = {};
            HRESULT hr = swapChain->Present1(1, 0, &pp);
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                std::printf("swapchain2: device lost (0x%08lX) - not handled in spike\n", static_cast<unsigned long>(hr));
            }
            else {
                check(hr, "Present1");
            }

            ++frames;
            ULONGLONG now = GetTickCount64();
            if (now - lastReport >= 1000) {
                std::printf("swapchain2: child presented %u frames in the last second\n", frames);
                frames = 0;
                lastReport = now;
            }
        }
    };

    Presenter* g_presenter = nullptr;  // child thread only

    LRESULT CALLBACK childProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_SIZE:
            if (g_presenter && g_presenter->resize(LOWORD(lp), HIWORD(lp))) {
                g_presenter->renderAndPresent();
            }
            return 0;
        case WM_TIMER:
            if (g_presenter) {
                g_presenter->renderAndPresent();
            }
            return 0;
        case WM_APP_LAYOUT:
            // The child moves/resizes itself - the parent never calls SetWindowPos on it.
            ::SetWindowPos(hwnd, nullptr, short(LOWORD(wp)), short(HIWORD(wp)), LOWORD(lp), HIWORD(lp),
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        case WM_APP_QUIT:
            ::KillTimer(hwnd, 1);
            ::DestroyWindow(hwnd);  // sends WM_PARENTNOTIFY to the parent thread - which is pumping, by design
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            ::BeginPaint(hwnd, &ps);  // passive - the swap chain owns the surface
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    struct ChildInit {
        HWND parent;
        int x, y, w, h;
    };

    void childThread(ChildInit init) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = childProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = L"swapchain2_child";
        wc.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        ::RegisterClassExW(&wc);

        // Cross-thread parent: this call SendMessage()s WM_PARENTNOTIFY etc. to the parent thread, which
        // must therefore be pumping (it is - see main()).
        HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            init.x, init.y, init.w, init.h, init.parent, nullptr, wc.hInstance, nullptr);
        if (!hwnd) {
            std::printf("swapchain2: child CreateWindowEx failed, gle=%lu\n", ::GetLastError());
            ::PostMessageW(init.parent, WM_APP_CHILD_GONE, 0, 0);
            return;
        }

        Presenter presenter;
        g_presenter = &presenter;
        RECT rc = {};
        ::GetClientRect(hwnd, &rc);
        if (presenter.create(hwnd, UINT(rc.right), UINT(rc.bottom))) {
            presenter.renderAndPresent();
        }
        ::SetTimer(hwnd, 1, 16, nullptr);
        ::PostMessageW(init.parent, WM_APP_CHILD_READY, 0, reinterpret_cast<LPARAM>(hwnd));

        MSG msg;
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        g_presenter = nullptr;  // presenter (and its D3D objects) go out of scope right after
        // Posted, never blocked on: tells the parent it is now safe to destroy itself.
        ::PostMessageW(init.parent, WM_APP_CHILD_GONE, 0, 0);
    }

    void layoutChild(HWND parent) {
        if (!g_child) {
            return;
        }
        RECT rc = {};
        ::GetClientRect(parent, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top - kHeaderHeight;
        if (w > 0 && h > 0) {
            ::PostMessageW(g_child, WM_APP_LAYOUT, MAKELONG(0, kHeaderHeight), MAKELONG(w, h));
        }
    }

    LRESULT CALLBACK parentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_APP_CHILD_READY:
            g_child = reinterpret_cast<HWND>(lp);
            layoutChild(hwnd);  // pick up any resize that happened while the child was being created
            return 0;
        case WM_SIZE:
            layoutChild(hwnd);
            if (g_stall && wp != SIZE_MINIMIZED) {
                std::printf("swapchain2: parent WM_SIZE %dx%d - stalling 1500ms\n", LOWORD(lp), HIWORD(lp));
            }
            if (g_stall && wp != SIZE_MINIMIZED) {
                ::Sleep(1500);  // parent thread busy: the child must keep animating regardless
            }
            return 0;
        case WM_CLOSE:
            std::printf("swapchain2: parent WM_CLOSE (child=%p gone=%d)\n", static_cast<void*>(g_child), int(g_childGone));
            if (g_child && !g_childGone) {
                ::PostMessageW(g_child, WM_APP_QUIT, 0, 0);  // wait for WM_APP_CHILD_GONE before destroying
                return 0;
            }
            break;
        case WM_APP_CHILD_GONE:
            std::printf("swapchain2: parent got WM_APP_CHILD_GONE\n");
            g_childGone = true;
            g_child = nullptr;
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = ::BeginPaint(hwnd, &ps);
            RECT rc = {};
            ::GetClientRect(hwnd, &rc);
            RECT band = { 0, 0, rc.right, kHeaderHeight };
            HBRUSH br = ::CreateSolidBrush(RGB(30, 30, 30));
            ::FillRect(dc, &band, br);
            ::DeleteObject(br);
            ::SetBkMode(dc, TRANSPARENT);
            ::SetTextColor(dc, RGB(230, 230, 230));
            const wchar_t* text = L"Parent window (thread A) - painted with GDI. Child below: DXGI swap chain on thread B.";
            ::TextOutW(dc, 10, 12, text, int(wcslen(text)));
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // spike diagnostics must survive a force-kill
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stall") == 0) {
            g_stall = true;
        }
    }

    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = parentProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"swapchain2_parent";
    wc.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    ::RegisterClassExW(&wc);

    g_parent = ::CreateWindowExW(0, wc.lpszClassName, L"Swap Chain Child-HWND Spike", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        10, 10, 820, 460, nullptr, nullptr, inst, nullptr);
    ::ShowWindow(g_parent, SW_SHOW);

    RECT rc = {};
    ::GetClientRect(g_parent, &rc);
    std::thread worker(childThread, ChildInit{ g_parent, 0, kHeaderHeight, int(rc.right), int(rc.bottom) - kHeaderHeight });

    // Pump immediately - the child thread's CreateWindowEx() needs this thread to be servicing messages.
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // Only reached after the parent was destroyed, which (WM_APP_CHILD_GONE) only happens after the child
    // thread has finished with its window and is about to return - so this join can't block on the parent.
    worker.join();
    return 0;
}
