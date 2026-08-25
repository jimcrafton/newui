// A visual reference for ViewStyle's image-fill handling (viewstyle.h) -
// ImageFillMode::Tile (repeats the image at its own natural size, the
// only behavior that used to exist), Stretch (scaled to exactly cover
// the swatch, ignoring the image's own aspect ratio), Align's nine-way
// ImageAlignment anchor grid (drawn unscaled, anchored within the swatch -
// only the image's own footprint gets painted, the rest is left showing
// whatever's behind it), and opacity (ViewStyle::opacity, which already
// applied uniformly to colors/gradients/images - the last row just makes
// that visible for an image fill specifically). Nothing here is
// interactive; resize the window to see every mode keep responding
// correctly as the swatches' own bounds change.
//
// Two source images, both loaded through newui::Bundle::instance().
// loadImage() - the same Resources/-relative resource-loading path a
// real app would use for its own art (see bundle.h's own class comment):
//   - A small, deliberately asymmetric procedural swatch (four differently
//     colored quadrants plus a dark border) for the Tile/Stretch/Align
//     grid - a busy real photo makes a *repeated* tile or a precise
//     anchor position harder to read at a glance than four flat, named
//     colors does. Generated once in memory and written under Resources/
//     the first time this example runs (mirroring Bundle::writeTextFile()'s
//     own "idempotent, create if missing" convention, bundle.cpp); every
//     load after that, this run included, goes through Bundle::loadImage()
//     for real.
//   - st-enterprise-nc1701-a.png, a real photo with a big asymmetric
//     subject and plenty of plain background, for the opacity row -
//     recognizable content fading against the window background reads
//     better there than a flat color swatch would.
//   - robot1.png (fully opaque) and robot2.png (genuinely transparent
//     outside its own subject's silhouette) for Row 5, which exercises
//     the real newui::Image control (controls.h) - not the bare
//     SubView+ViewStyle every earlier row uses - and its default style,
//     ImageFillStyle (viewstyle.h): a checkerboard should appear behind
//     robot2.png's real transparent pixels, but never behind robot1.png.
// All three real photos are copied from this source directory into
// imagefill1's own Resources/ folder by CMakeLists.txt as a post-build
// step, same as a real app's asset pipeline would.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>

namespace {

    const char* kSwatchFileName = "ImageFillSwatch.png";
    const char* kShipFileName = "st-enterprise-nc1701-a.png";
    constexpr float kSwatchSize = 56.0f;

    // A small, deliberately asymmetric test image - four differently
    // colored quadrants plus a dark border - rather than a photo or a
    // solid color, so tiling seams, Stretch's aspect distortion, and
    // *which* corner/edge an Align anchor actually landed on are all
    // unambiguous at a glance.
    BLImage BuildSwatchImage() {
        BLImage image(int(kSwatchSize), int(kSwatchSize), BL_FORMAT_PRGB32);
        BLContext ctx(image);

        float half = kSwatchSize * 0.5f;
        ctx.set_fill_style(newui::Color::fromName("tomato").toBLRgba32());
        ctx.fill_rect(BLRect(0, 0, half, half));
        ctx.set_fill_style(newui::Color::fromName("mediumseagreen").toBLRgba32());
        ctx.fill_rect(BLRect(half, 0, half, half));
        ctx.set_fill_style(newui::Color::fromName("steelblue").toBLRgba32());
        ctx.fill_rect(BLRect(0, half, half, half));
        ctx.set_fill_style(newui::Color::fromName("goldenrod").toBLRgba32());
        ctx.fill_rect(BLRect(half, half, half, half));

        ctx.set_stroke_style(newui::Color::fromName("black").toBLRgba32());
        ctx.set_stroke_width(2.0f);
        ctx.stroke_box(1.0, 1.0, double(kSwatchSize) - 1.0, double(kSwatchSize) - 1.0);

        ctx.end();
        return image;
    }

    // Generates BuildSwatchImage() straight to disk under Resources/ the
    // first time this example runs, then always loads it back through
    // newui::Bundle::loadImage() - the point is to exercise that real
    // resource-loading path, not to hand ViewStyle the in-memory BLImage
    // BuildSwatchImage() only ever produces once.
    bool LoadOrCreateSwatchImage(BLImage& outImage) {
        newui::Bundle& bundle = newui::Bundle::instance();

        if (bundle.resourcePath(kSwatchFileName).empty()) {
            ::CreateDirectoryA(bundle.resourcesDir().c_str(), nullptr);  // idempotent, same as Bundle::writeTextFile()

            BLImage generated = BuildSwatchImage();
            std::string path = bundle.resourcesDir() + "\\" + kSwatchFileName;
            if (generated.write_to_file(path.c_str()) != BL_SUCCESS) {
                return false;
            }
            printf("generated %s\n", path.c_str());
        }

        return bundle.loadImage(kSwatchFileName, outImage);
    }

    // Two real photos for the newui::Image control row (Row 5) -
    // robot1.png decodes fully opaque everywhere (alpha == 255
    // throughout, confirmed by direct pixel sampling) despite being an
    // RGBA-encoded PNG, same as the ship photo above; robot2.png is
    // genuinely transparent outside its own subject's silhouette (alpha
    // == 0 there, also confirmed by sampling) - exactly the contrast
    // needed to show ImageFillStyle's checkerboard backdrop (viewstyle.h)
    // appearing for one and not the other. Both copied from this source
    // directory into imagefill1's own Resources/ folder by CMakeLists.txt,
    // same as st-enterprise-nc1701-a.png above.
    const char* kRobotOpaqueFileName = "robot1.png";
    const char* kRobotAlphaFileName = "robot2.png";

    newui::SyncReturn FrameClosed(newui::Frame& frame) {
        printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
        return newui::SyncReturn::Handled;
    }

    // One labeled swatch: a bordered SubView filled with swatchImage
    // under the given ImageFillMode/ImageAlignment/opacity, plus a
    // caption Label below it naming the combination. imageAlignment is
    // ignored by Tile/Stretch, so callers demonstrating those just pass
    // Center; opacity defaults to fully opaque for every cell but the
    // dedicated opacity row.
    newui::SubView* MakeSwatchCell(const std::string& caption, const BLImage& swatchImage,
            newui::ImageFillMode mode, newui::ImageAlignment align, float opacity = 1.0f) {
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
        swatch->style().setBackgroundImage(swatchImage);
        swatch->style().imageFillMode = mode;
        swatch->style().imageAlignment = align;
        swatch->style().opacity = opacity;
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

    // Exercises the real newui::Image control (controls.h), not a bare
    // SubView+ViewStyle like MakeSwatchCell() above - imagePath is
    // resolved by Image::setImagePath() itself (Bundle::loadImage()
    // first, a plain file-path read_from_file() fallback second - see
    // its own comment, controls.cpp), so this always passes just a bare
    // Bundle-relative resource name to prove that resolution actually
    // runs. mode/align are only applied when set (std::optional) -
    // omitting them proves Image's own default (Align/Center) is really
    // what's in effect, not something this helper is choosing for it.
    newui::SubView* MakeImageControlCell(const std::string& caption, const std::string& imagePath,
            std::optional<newui::ImageFillMode> mode = std::nullopt,
            std::optional<newui::ImageAlignment> align = std::nullopt) {
        auto* cell = new newui::SubView();
        cell->setVisible(true);

        auto cellLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        cellLayout->setSpacing(4.0f);
        cellLayout->setPadding(6.0f);
        cell->setLayout(std::move(cellLayout));

        auto* image = new newui::Image();
        image->setDesiredSize(newui::Size(0.0f, 150.0f));
        image->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
        if (mode.has_value()) {
            image->setImageFillMode(*mode);
        }
        if (align.has_value()) {
            image->setImageAlignment(*align);
        }
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
    printf("newui %s - image fill mode example\n", newui::version());
    printf("ViewStyle::ImageFillMode/ImageAlignment (viewstyle.h): Tile (repeats at natural size),\n");
    printf("Stretch (scaled to exactly cover the swatch, ignoring aspect ratio), and Align's nine-way\n");
    printf("anchor grid (drawn unscaled, anchored within the swatch - only its own footprint is filled,\n");
    printf("the rest shows the window background). Row 4 shows ViewStyle::opacity fading a Stretch-filled\n");
    printf("image (100%%/50%%/25%%). Row 5 uses the real newui::Image control (controls.h) and its\n");
    printf("ImageFillStyle - a checkerboard should appear behind robot2.png's real transparent pixels,\n");
    printf("but never behind robot1.png (fully opaque despite also being an RGBA PNG). Resize the window\n");
    printf("to see every mode keep responding correctly as each cell's own bounds change.\n");

    BLImage swatchImage;
    if (!LoadOrCreateSwatchImage(swatchImage)) {
        printf("Could not load or generate %s under Resources/ - can't continue.\n", kSwatchFileName);
        return 1;
    }

    BLImage shipImage;
    if (!newui::Bundle::instance().loadImage(kShipFileName, shipImage)) {
        printf("Could not load %s under Resources/ - can't continue.\n", kShipFileName);
        return 1;
    }

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("imagefill1");
    app.setFrame(&frame);

    frame.setTitle("Image Fill Mode Example");
    frame.setBounds(newui::Rect(10, 10, 820, 1280));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addStarColumn();
    grid->addStarColumn();
    grid->addStarColumn();
    grid->addFixedRow(190.0f);
    grid->addFixedRow(190.0f);
    grid->addFixedRow(190.0f);
    grid->addFixedRow(190.0f);
    grid->addFixedRow(190.0f);
    grid->addFixedRow(190.0f);
    grid->setRowSpacing(10.0f);
    grid->setColumnSpacing(10.0f);
    root.setLayout(std::move(grid));

    using newui::ImageAlignment;
    using newui::ImageFillMode;

    auto place = [&](std::size_t row, std::size_t column, newui::SubView* cell) {
        cell->setLayoutParams(std::make_unique<newui::GridLayoutParams>(row, column));
        root.addChild(cell);
    };

    // Row 0: Tile and Stretch - imageAlignment doesn't apply to either,
    // so both just pass Center as a don't-care.
    place(0, 0, MakeSwatchCell("Tile", swatchImage, ImageFillMode::Tile, ImageAlignment::Center));
    place(0, 1, MakeSwatchCell("Stretch", swatchImage, ImageFillMode::Stretch, ImageAlignment::Center));

    // Rows 1-3: the full nine-way Align anchor grid, laid out in the
    // same spatial arrangement the anchor names describe - TopLeft
    // literally sits top-left, Center sits in the middle, and so on.
    place(1, 0, MakeSwatchCell("Align: TopLeft", swatchImage, ImageFillMode::Align, ImageAlignment::TopLeft));
    place(1, 1, MakeSwatchCell("Align: Top", swatchImage, ImageFillMode::Align, ImageAlignment::Top));
    place(1, 2, MakeSwatchCell("Align: TopRight", swatchImage, ImageFillMode::Align, ImageAlignment::TopRight));

    place(2, 0, MakeSwatchCell("Align: Left", swatchImage, ImageFillMode::Align, ImageAlignment::Left));
    place(2, 1, MakeSwatchCell("Align: Center", swatchImage, ImageFillMode::Align, ImageAlignment::Center));
    place(2, 2, MakeSwatchCell("Align: Right", swatchImage, ImageFillMode::Align, ImageAlignment::Right));

    place(3, 0, MakeSwatchCell("Align: BottomLeft", swatchImage, ImageFillMode::Align, ImageAlignment::BottomLeft));
    place(3, 1, MakeSwatchCell("Align: Bottom", swatchImage, ImageFillMode::Align, ImageAlignment::Bottom));
    place(3, 2, MakeSwatchCell("Align: BottomRight", swatchImage, ImageFillMode::Align, ImageAlignment::BottomRight));

    // Row 4: ViewStyle::opacity (already a general multiply-the-alpha
    // knob, applied here to an image fill specifically) at 100%/50%/25% -
    // Stretch so the fade covers the whole swatch, and the real ship
    // photo rather than the flat-color swatch since recognizable content
    // fading against the window background reads better than a solid
    // color dimming does.
    place(4, 0, MakeSwatchCell("Opacity: 100%", shipImage, ImageFillMode::Stretch, ImageAlignment::Center, 1.0f));
    place(4, 1, MakeSwatchCell("Opacity: 50%", shipImage, ImageFillMode::Stretch, ImageAlignment::Center, 0.5f));
    place(4, 2, MakeSwatchCell("Opacity: 25%", shipImage, ImageFillMode::Stretch, ImageAlignment::Center, 0.25f));

    // Row 5: the real newui::Image control (controls.h), not the bare
    // SubView+ViewStyle every earlier row uses - and, unlike every image
    // above, exercises ImageFillStyle's checkerboard backdrop
    // (viewstyle.h). robot1.png is fully opaque (no checkerboard should
    // ever show through it, at any alignment); robot2.png is genuinely
    // transparent outside its own subject - Top explicitly picks an
    // anchor whose crop actually reaches that transparent region, so the
    // checkerboard is guaranteed visible there, while the first cell
    // (mode/align both omitted) proves Image's own real default is
    // Align/Center, not something this example is choosing for it.
    place(5, 0, MakeImageControlCell("Image: default (Align/Center)", kRobotAlphaFileName));
    place(5, 1, MakeImageControlCell("Image: Align Top (alpha)", kRobotAlphaFileName,
        ImageFillMode::Align, ImageAlignment::Top));
    place(5, 2, MakeImageControlCell("Image: Stretch (opaque)", kRobotOpaqueFileName, ImageFillMode::Stretch));

    app.run();

    return 0;
}
