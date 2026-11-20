// Logging harness for the three DxgiPresentSurface GPU hooks (dxgipresentsurface.h): GpuDeviceHook,
// GpuCompositeHook and GpuResizeHook, registered through RootView::setGpu*Hook(). It walks the validation
// checklist from the hooks work - every event is printed to stdout, and the window shows running counts.
//
//   gpuhooks1.exe          -> DXGI (the whole point of this example)
//   gpuhooks1.exe gdi      -> GDI: setGpu*Hook() return false and no hook may ever fire (checklist item 1)
//
// What to do, and what to expect:
//   * Startup:           one "device" line with non-null pointers and the window's size.
//   * Resize the window: "resize" lines (both the in-place ResizeBuffers() path and, after a minimize/restore,
//                        the rebuild path); the size matches the window.
//   * Hover the buttons: "composite" count rises on every present, including chrome-only frames. (Only
//                        every 60th is printed.)
//   * Device loss:       disable and re-enable the display adapter in Device Manager (or use a GPU-reset tool)
//                        - expect a "device GONE" line (null pointers) followed by a "device" line with a
//                        *different* pointer. There's no portable way to fake this from inside the process.
//   * Minimize:          "device GONE" (the swap chain is released at 0x0), then "device" again on restore.
//   * "Throw in hook":   the next composite call throws; presentation must carry on and the strip below keeps
//                        updating - the exception is swallowed by DxgiPresentSurface.
//   * "Toggle strip":    the composite hook paints a 24px red strip along the bottom of the back buffer with
//                        ClearView() - proof that GPU drawing reaches the screen on top of Blend2D's chrome.
//   * "Report":          prints PASS/FAIL/INFO for each automatic check.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/presentsurface.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/uicolormanager.h"

#include <d3d11_1.h>
#include <wrl/client.h>

#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>

#pragma comment(lib, "d3d11.lib")

extern void registerReflectionData();

namespace {

    struct HookStats {
        int deviceEvents = 0;      // non-null device hook calls
        int deviceGoneEvents = 0;  // null device hook calls
        int resizeEvents = 0;
        int compositeEvents = 0;
        int throwsSwallowed = 0;

        ID3D11Device* currentDevice = nullptr;
        std::set<ID3D11Device*> devicesSeen;
        UINT deviceWidth = 0, deviceHeight = 0;
        UINT lastResizeWidth = 0, lastResizeHeight = 0;
        bool resizeBeforeDevice = false;  // a resize hook call that arrived while no device was live

        bool stripOn = false;
        bool throwNext = false;
    };

    const char* verdict(bool ok) {
        return ok ? "PASS" : "FAIL";
    }

}

int main(int argc, char** argv) {
    registerReflectionData();

    newui::PresentBackend backend = newui::PresentBackend::Dxgi;
    if (argc > 1 && !newui::parsePresentBackend(argv[1], backend)) {
        std::printf("usage: gpuhooks1 [gdi|dxgi]   (unrecognized: '%s')\n", argv[1]);
        backend = newui::PresentBackend::Dxgi;
    }
    newui::setDefaultPresentBackend(backend);

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("gpuhooks1");
    app.setFrame(&frame);

    frame.setTitle("GPU hooks logging example");
    frame.setBounds(newui::Rect(10, 10, 460, 300));

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto stats = std::make_shared<HookStats>();

    // ---- the three hooks, registered before the window exists ------------------------------------------
    const bool deviceOk = root.setGpuDeviceHook(
        [stats](ID3D11Device* device, ID3D11DeviceContext* context, UINT width, UINT height) {
            if (device == nullptr) {
                ++stats->deviceGoneEvents;
                stats->currentDevice = nullptr;
                std::printf("gpuhooks1: device GONE (ctx=%p, %ux%u)\n", static_cast<void*>(context), width, height);
            }
            else {
                ++stats->deviceEvents;
                const bool isNew = stats->devicesSeen.insert(device).second;
                stats->currentDevice = device;
                stats->deviceWidth = width;
                stats->deviceHeight = height;
                std::printf("gpuhooks1: device   dev=%p ctx=%p size=%ux%u%s\n", static_cast<void*>(device),
                    static_cast<void*>(context), width, height, isNew ? "  [new device pointer]" : "  [same pointer as before]");
            }
            std::fflush(stdout);
        });

    const bool resizeOk = root.setGpuResizeHook([stats](UINT width, UINT height) {
        ++stats->resizeEvents;
        stats->lastResizeWidth = width;
        stats->lastResizeHeight = height;
        if (stats->currentDevice == nullptr) {
            stats->resizeBeforeDevice = true;
        }
        std::printf("gpuhooks1: resize   %ux%u (device %s)\n", width, height, stats->currentDevice ? "live" : "NOT live");
        std::fflush(stdout);
    });

    const bool compositeOk = root.setGpuCompositeHook(
        [stats](ID3D11DeviceContext* context, ID3D11RenderTargetView* rtv) {
            ++stats->compositeEvents;
            if (stats->compositeEvents % 60 == 1) {
                std::printf("gpuhooks1: composite #%d (rtv=%p)\n", stats->compositeEvents, static_cast<void*>(rtv));
                std::fflush(stdout);
            }
            if (stats->throwNext) {
                stats->throwNext = false;
                ++stats->throwsSwallowed;
                std::printf("gpuhooks1: composite throwing on purpose\n");
                std::fflush(stdout);
                throw 1;
            }
            if (stats->stripOn && context != nullptr && rtv != nullptr) {
                Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
                if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
                    ID3D11Resource* resource = nullptr;
                    rtv->GetResource(&resource);
                    D3D11_TEXTURE2D_DESC desc = {};
                    if (resource != nullptr) {
                        static_cast<ID3D11Texture2D*>(resource)->GetDesc(&desc);
                        resource->Release();
                    }
                    const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
                    const LONG top = desc.Height > 24 ? LONG(desc.Height - 24) : 0;
                    const D3D11_RECT strip = { 0, top, LONG(desc.Width), LONG(desc.Height) };
                    context1->ClearView(rtv, red, &strip, 1);
                }
            }
        });

    std::printf("gpuhooks1: backend requested: %s; registration accepted (device/composite/resize): %d/%d/%d\n",
        backend == newui::PresentBackend::Dxgi ? "DXGI" : "GDI", deviceOk, compositeOk, resizeOk);
    std::fflush(stdout);

    // ---- UI --------------------------------------------------------------------------------------------
    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(8.0f);
    rootLayout->setPadding(16.0f);
    root.setLayout(std::move(rootLayout));

    auto* label = new newui::Label();
    label->setVisible(true);
    label->setText("Starting... click Report after resizing / hovering.");
    label->setDesiredSize(newui::Size(0.0f, 28.0f));
    root.addChild(label);

    auto addButton = [&root](const char* text, std::function<void()> action) {
        auto* button = new newui::Button();
        button->setVisible(true);
        button->setText(text);
        button->setDesiredSize(newui::Size(0.0f, 28.0f));
        button->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
            [action](newui::Control&) {
                action();
                return newui::SyncReturn::Handled;
            }));
        root.addChild(button);
    };

    addButton("Toggle strip (GPU-drawn red bar)", [stats, &root]() {
        stats->stripOn = !stats->stripOn;
        std::printf("gpuhooks1: strip %s\n", stats->stripOn ? "on" : "off");
        std::fflush(stdout);
        root.invalidate();  // a present() without repainting Blend2D content
    });

    addButton("Throw in hook (next composite)", [stats, &root]() {
        stats->throwNext = true;
        root.invalidate();
    });

    addButton("Report", [stats, label, &root, deviceOk, compositeOk, resizeOk, backend]() {
        const bool dxgi = root.presentBackend() == newui::PresentBackend::Dxgi;
        std::printf("\ngpuhooks1: ---- report ----\n");
        if (backend == newui::PresentBackend::Gdi) {
            const int events = stats->deviceEvents + stats->deviceGoneEvents + stats->resizeEvents + stats->compositeEvents;
            std::printf("  %s  GDI requested: no hook fired (events=%d)\n", verdict(events == 0), events);
            std::printf("  %s  setGpu*Hook() returned false under GDI\n", verdict(!deviceOk && !compositeOk && !resizeOk));
        }
        else if (!dxgi) {
            // Started on DXGI and gave up later (no hardware device while rebuilding after a loss, ...) -
            // hook events from before the fallback are expected, so this isn't a pass/fail check.
            std::printf("  INFO  fell back to GDI after starting on DXGI: device events=%d, GONE=%d, resize=%d, composite=%d\n",
                stats->deviceEvents, stats->deviceGoneEvents, stats->resizeEvents, stats->compositeEvents);
            std::printf("  %s  last device event was a GONE (no hook left pointing at a dead device)\n",
                verdict(stats->currentDevice == nullptr));
        }
        else {
            std::printf("  %s  device hook fired with a live device (events=%d)\n", verdict(stats->deviceEvents >= 1),
                stats->deviceEvents);
            std::printf("  %s  device hook size matches a real window (%ux%u)\n",
                verdict(stats->deviceWidth > 0 && stats->deviceHeight > 0), stats->deviceWidth, stats->deviceHeight);
            std::printf("  %s  composite hook fires (events=%d)\n", verdict(stats->compositeEvents > 0), stats->compositeEvents);
            std::printf("  %s  resize hook fired (events=%d) - resize the window if this is FAIL\n",
                verdict(stats->resizeEvents > 0), stats->resizeEvents);
            std::printf("  %s  no resize hook while the device was not live\n", verdict(!stats->resizeBeforeDevice));
            std::printf("  INFO  distinct device pointers seen: %zu (expect 2+ after a device-loss test)\n",
                stats->devicesSeen.size());
            std::printf("  INFO  device GONE events: %d (expect 1+ after minimize or device loss)\n", stats->deviceGoneEvents);
            std::printf("  INFO  throws swallowed: %d\n", stats->throwsSwallowed);
        }
        std::fflush(stdout);

        char text[160];
        std::snprintf(text, sizeof(text), "dev %d/gone %d  resize %d  composite %d  (see console)", stats->deviceEvents,
            stats->deviceGoneEvents, stats->resizeEvents, stats->compositeEvents);
        label->setText(text);
    });

    addButton("Hover target (watch composite count)", []() {});

    app.run();
    return 0;
}
