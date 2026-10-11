#include "newui/svgimage.h"

#include "svg.h"
#include "render_blend2d.h"
#include "svg_renderer.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace newui {

bool renderSvgFile(const std::string& path, int width, int height, BLImage& outImage) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return false;
    }

    const waavs::ByteSpan chunk(bytes.data(), bytes.size());
    auto doc = waavs::SVGFactory::createFromChunk(chunk, double(width), double(height), 96.0);
    if (!doc) {
        return false;
    }

    waavs::Surface surf(width, height);
    if (surf.data() == nullptr) {
        return false;
    }
    // Surface's owned buffer (SharedMemBuff::resetFromSize(), lang_memory.h)
    // is a raw `new uint8_t[]`, not zero-initialized - clear it to
    // transparent first so any part of the canvas the document doesn't
    // actually paint over (aspect-ratio letterboxing, a viewBox smaller
    // than width x height, ...) comes out transparent instead of garbage.
    std::memset(surf.data(), 0, surf.stride() * size_t(height));

    waavs::SVGB2DDriver ctx;
    ctx.attach(surf, 1, nullptr);
    // SVGDocument::draw()/drawRenderSubtree() is a stale "compatibility
    // path" (svgdocument.h/svggraphicselement.h's own comments say as
    // much) that's actually broken in this pinned commit - SVGRenderer is
    // the real, currently-working entry point (see its own drawChildren()/
    // drawElement() - proper clip-path handling included).
    waavs::SVGRenderer renderer;
    renderer.draw(*doc, ctx);
    ctx.detach();

    // Deliberately not blend2d_connect.h's blImageFromSurface() here - it
    // wraps surf's own pixel buffer via BLImage::create_from_data() with no
    // destroy_func, i.e. a non-owning *view*, not a copy. surf is local and
    // destructs when this function returns, which would leave outImage
    // dangling the moment its caller (Image's ".svg" constructors,
    // graphics.cpp) tried to read it - the same use-after-free class
    // documented on Gradient::rasterizePoints() (graphics.h). Copy the
    // pixels into a real, independently-owned BLImage instead.
    if (outImage.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS) {
        return false;
    }
    BLImageData dst{};
    outImage.get_data(&dst);

    const auto* src = static_cast<const std::uint8_t*>(surf.data());
    auto* dstBytes = static_cast<std::uint8_t*>(dst.pixel_data);
    const size_t rowBytes = size_t(width) * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(dstBytes + size_t(y) * dst.stride, src + size_t(y) * surf.stride(), rowBytes);
    }

    return true;
}

}
