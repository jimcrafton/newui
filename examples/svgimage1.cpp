// A visual reference for SVG support (svgimage.h, vendored via
// 3rdparty/svgandme) wired into two places: newui::gfx::Image's ".svg"
// constructors (graphics.h) and newui::Bundle::loadImage() (bundle.h).
//
// Source: peacockspider.svg (examples/, copied from this source directory
// into this example's own Resources/ folder by CMakeLists.txt, same as
// imagefill1's photos) - a real-world, non-trivial SVG (paths, gradients,
// symbols, filters) from svgandme's own gallery/, not a toy icon, so
// rendering it actually exercises the parser/renderer rather than just the
// plumbing around it.
//
// Row 0 uses newui::gfx::Image(path, width, height) directly, at four
// different explicit pixel sizes - each is a fresh rasterization at that
// exact size (svgandme re-renders the vector content, it doesn't scale a
// bitmap), so all four should look equally crisp despite the size range.
// Row 1 uses the real newui::Image control (controls.h) via
// setImagePath(), which resolves through Bundle::loadImage() first (see
// Image::updateImage()'s own comment, controls.cpp) - exercising
// Bundle::loadImage()'s new ".svg" handling instead, at its fixed
// kDefaultSvgRasterSize x kDefaultSvgRasterSize default (svgimage.h).

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/graphics.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

    const char* kSvgFileName = "peacockspider.svg";

    newui::SyncReturn FrameClosed(newui::Frame& frame) {
        printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
        return newui::SyncReturn::Handled;
    }

    // One labeled swatch: a bordered SubView filled (Stretch, so the
    // swatch's own bounds don't have to match the rasterized image's
    // aspect ratio) with a pre-rasterized image, plus a caption Label
    // below it.
    newui::SubView* MakeSwatchCell(const std::string& caption, const BLImage& image) {
        auto* cell = new newui::SubView();
        cell->setVisible(true);

        auto cellLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        cellLayout->setSpacing(4.0f);
        cellLayout->setPadding(6.0f);
        cell->setLayout(std::move(cellLayout));

        auto* swatch = new newui::SubView();
        swatch->setVisible(true);
        swatch->setDesiredSize(newui::Size(0.0f, 150.0f));
        swatch->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        swatch->style().setBackgroundImage(image);
        swatch->style().imageFillMode = newui::ImageFillMode::Stretch;
        swatch->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
        swatch->style().borderWidth = 1.0f;
        cell->addChild(swatch);

        auto* label = new newui::Label();
        label->setText(caption);
        label->setDesiredSize(newui::Size(0.0f, 18.0f));
        label->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        cell->addChild(label);

        return cell;
    }

    newui::SubView* MakeImageControlCell(const std::string& caption, const std::string& imagePath) {
        auto* cell = new newui::SubView();
        cell->setVisible(true);

        auto cellLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        cellLayout->setSpacing(4.0f);
        cellLayout->setPadding(6.0f);
        cell->setLayout(std::move(cellLayout));

        auto* image = new newui::Image();
        image->setDesiredSize(newui::Size(0.0f, 150.0f));
        image->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        image->setImageFillMode(newui::ImageFillMode::Align);
        image->setImageAlignment(newui::ImageAlignment::Center);
        image->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
        image->style().borderWidth = 1.0f;
        image->setImagePath(imagePath);
        cell->addChild(image);

        auto* label = new newui::Label();
        label->setText(caption);
        label->setDesiredSize(newui::Size(0.0f, 18.0f));
        label->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        cell->addChild(label);

        return cell;
    }

}

int main() {
    printf("newui %s - SVG image example\n", newui::version());
    printf("Row 0: newui::gfx::Image(path, width, height) (graphics.h) rasterizing %s\n", kSvgFileName);
    printf("directly at four explicit pixel sizes - each is a fresh render, not a scaled bitmap,\n");
    printf("so all four should look equally crisp.\n");
    printf("Row 1: the real newui::Image control (controls.h) via setImagePath(), which resolves\n");
    printf("through Bundle::loadImage()'s own new SVG handling at its fixed default size.\n");

    newui::Bundle& bundle = newui::Bundle::instance();
    std::string svgPath = bundle.resourcePath(kSvgFileName);
    if (svgPath.empty()) {
        printf("Could not find %s under Resources/ - can't continue.\n", kSvgFileName);
        return 1;
    }

    // Kept alive for the whole run (through app.run() below) - gfx::Image
    // owns the real DIB-backed pixel buffer each swatch's BLImage copy
    // below still points at (see graphics.h's own Image class comment),
    // so these can't be temporaries.
    std::vector<newui::gfx::Image> rasterizedIcons;
    rasterizedIcons.emplace_back(svgPath, 48, 48);
    rasterizedIcons.emplace_back(svgPath, 96, 96);
    rasterizedIcons.emplace_back(svgPath, 192, 192);
    rasterizedIcons.emplace_back(svgPath, 384, 384);

    for (const auto& icon : rasterizedIcons) {
        if (!icon.isValid()) {
            printf("Failed to rasterize %s - can't continue.\n", svgPath.c_str());
            return 1;
        }
    }

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("svgimage1");
    app.setFrame(&frame);

    frame.setTitle("SVG Image Example");
    frame.setBounds(newui::Rect(10, 10, 820, 420));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addStarColumn();
    grid->addStarColumn();
    grid->addStarColumn();
    grid->addStarColumn();
    grid->addFixedRow(190.0f);
    grid->addFixedRow(190.0f);
    grid->setRowSpacing(10.0f);
    grid->setColumnSpacing(10.0f);
    root.setLayout(std::move(grid));

    auto place = [&](std::size_t row, std::size_t column, newui::SubView* cell) {
        cell->setLayoutParams(std::make_unique<newui::GridLayoutParams>(row, column));
        root.addChild(cell);
    };

    place(0, 0, MakeSwatchCell("gfx::Image, 48x48", rasterizedIcons[0].blImage()));
    place(0, 1, MakeSwatchCell("gfx::Image, 96x96", rasterizedIcons[1].blImage()));
    place(0, 2, MakeSwatchCell("gfx::Image, 192x192", rasterizedIcons[2].blImage()));
    place(0, 3, MakeSwatchCell("gfx::Image, 384x384", rasterizedIcons[3].blImage()));

    place(1, 0, MakeImageControlCell("Image control (Bundle::loadImage default)", kSvgFileName));

    app.run();

    return 0;
}
