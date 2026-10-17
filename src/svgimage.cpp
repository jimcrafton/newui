#include "newui/svgimage.h"

#include "svg.h"
#include "render_blend2d.h"
#include "svg_renderer.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>

namespace newui {

namespace {

    // The value of attribute `name` in the start tag `tag` (a whole attribute: `name` must follow white space and be
    // followed by "="), with the span of the attribute (name, quotes and value) so it can be cut out; false if absent.
    bool findAttribute(const std::string& tag, const char* name, std::string& value, std::size_t& begin, std::size_t& end) {
        const std::string key = name;
        for (std::size_t at = tag.find(key); at != std::string::npos; at = tag.find(key, at + 1)) {
            if (at == 0 || !std::isspace(static_cast<unsigned char>(tag[at - 1]))) {
                continue;   // stroke-width="..." and the like
            }
            std::size_t cursor = at + key.size();
            while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) {
                ++cursor;
            }
            if (cursor >= tag.size() || tag[cursor] != '=') {
                continue;
            }
            ++cursor;
            while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor]))) {
                ++cursor;
            }
            if (cursor >= tag.size() || (tag[cursor] != '"' && tag[cursor] != '\'')) {
                continue;
            }
            const char quote = tag[cursor];
            const std::size_t close = tag.find(quote, cursor + 1);
            if (close == std::string::npos) {
                continue;
            }
            value = tag.substr(cursor + 1, close - cursor - 1);
            begin = at;
            end = close + 1;
            return true;
        }
        return false;
    }

    // A number at the start of an SVG length ("32", "32px", "12.5pt"); false when there is none.
    bool leadingNumber(const std::string& text, double& out) {
        char* stop = nullptr;
        out = std::strtod(text.c_str(), &stop);
        return stop != text.c_str() && out > 0.0;
    }

    // An SVG with width / height attributes keeps that size however large the canvas it is rendered on, so a 32 x 32 cursor
    // drawn onto a 16 x 16 canvas came out cropped to its top-left quarter. Drop the two attributes from the root <svg>
    // (adding a viewBox from them first when there is none) and the drawing scales to the canvas like any other SVG.
    std::string scalableSvg(const std::string& text) {
        const std::size_t open = text.find("<svg");
        if (open == std::string::npos) {
            return text;
        }
        const std::size_t close = text.find('>', open);
        if (close == std::string::npos) {
            return text;
        }
        std::string tag = text.substr(open, close - open);
        std::string widthText, heightText, viewBoxText;
        std::size_t widthBegin = 0, widthEnd = 0, heightBegin = 0, heightEnd = 0, boxBegin = 0, boxEnd = 0;
        const bool hasWidth = findAttribute(tag, "width", widthText, widthBegin, widthEnd);
        const bool hasHeight = findAttribute(tag, "height", heightText, heightBegin, heightEnd);
        if (!hasWidth && !hasHeight) {
            return text;
        }
        const bool hasViewBox = findAttribute(tag, "viewBox", viewBoxText, boxBegin, boxEnd);
        std::string insertion;
        if (!hasViewBox) {
            double width = 0.0, height = 0.0;
            if (!hasWidth || !hasHeight || !leadingNumber(widthText, width) || !leadingNumber(heightText, height) ||
                widthText.find('%') != std::string::npos || heightText.find('%') != std::string::npos) {
                return text;   // no way to know the document's own coordinate space: leave it as written
            }
            insertion = " viewBox=\"0 0 " + std::to_string(width) + " " + std::to_string(height) + "\"";
        }
        // Cut the attribute that comes later in the tag first, so the earlier one's span stays valid.
        if (hasWidth && hasHeight) {
            const bool widthFirst = widthBegin < heightBegin;
            tag.erase(widthFirst ? heightBegin : widthBegin, (widthFirst ? heightEnd - heightBegin : widthEnd - widthBegin));
            tag.erase(widthFirst ? widthBegin : heightBegin, (widthFirst ? widthEnd - widthBegin : heightEnd - heightBegin));
        } else if (hasWidth) {
            tag.erase(widthBegin, widthEnd - widthBegin);
        } else {
            tag.erase(heightBegin, heightEnd - heightBegin);
        }
        return text.substr(0, open) + tag + insertion + text.substr(close);
    }

}  // namespace

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

    const std::string scalable = scalableSvg(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const waavs::ByteSpan chunk(reinterpret_cast<const std::uint8_t*>(scalable.data()), scalable.size());
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
