#pragma once

#include <blend2d/blend2d.h>

#include <string>

namespace newui {

// Stand-in target size for callers that have no explicit pixel size to
// rasterize an SVG at and no other sizing context to infer one from (a
// plain filename with no accompanying width/height) - see renderSvgFile()'s
// own comment for why svgandme can't supply a real "natural size" here.
// Shared so gfx::Image's and Bundle::loadImage()'s no-size SVG paths
// (graphics.cpp, bundle.cpp) don't each pick their own default.
inline constexpr int kDefaultSvgRasterSize = 32;

// Rasterizes the SVG file at path into a freshly-created width x height
// BLImage (BL_FORMAT_PRGB32) - Blend2D has no SVG decoder of its own, so
// this drives svgandme (3rdparty/svgandme/svg, vendored, namespace waavs)
// as the parser/renderer instead. Kept out of graphics.h/gfx::Image's own
// header (only src/svgimage.cpp includes any waavs:: header) so svgandme's
// ~150 headers don't become a transitive include of every graphics.h
// consumer - see gfx::Image's ".svg"-sniffing constructors (graphics.h),
// the only current caller.
//
// width/height double as the pixel canvas svgandme resolves the document's
// own viewBox/percentage sizing against (SVGFactory::createFromChunk()'s
// own w/h/ppi parameters) - this matches what an <img width height> tag
// would render, not a post-hoc scale of some independently-discovered
// "natural" size (svgandme's current API has no reliable, canvas-
// independent way to ask for that - the top-level <svg> element's raw
// declared width/height/viewBox is discarded once resolved against
// whatever canvas size it was first bound to).
//
// Returns false, leaving outImage untouched, if path can't be read, width
// or height isn't positive, or svgandme fails to parse the file.
bool renderSvgFile(const std::string& path, int width, int height, BLImage& outImage);

}
