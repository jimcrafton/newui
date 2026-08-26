#pragma once

#include <blend2d/blend2d.h>

#include <cstdint>
#include <vector>

namespace newui {

// CF_DIB's raw payload is exactly a BMP file's body (BITMAPINFOHEADER +
// pixel data) with the 14-byte BITMAPFILEHEADER stripped off. These two
// functions do that stripping/synthesis around Blend2D's own BMP codec -
// shared by ClipboardManager::setImage/getImage (src/clipboardmgr.cpp) and
// the OLE drag-and-drop CF_DIB extraction (include/newui/dragndrop.h) so
// the transform exists in exactly one place.

// Encodes image as a BMP via Blend2D and strips its BITMAPFILEHEADER,
// leaving outDibBytes holding CF_DIB's real payload. Returns false if
// image is empty or the BMP encode fails.
bool imageToDibBytes(const BLImage& image, std::vector<std::uint8_t>& outDibBytes);

// Reverse of imageToDibBytes() - synthesizes a BITMAPFILEHEADER around
// dibBytes (a raw CF_DIB payload) and decodes the result into outImage.
// Returns false if dibBytes is too small to hold a BITMAPINFOHEADER or the
// BMP decode fails.
bool dibBytesToImage(const std::vector<std::uint8_t>& dibBytes, BLImage& outImage);

}
