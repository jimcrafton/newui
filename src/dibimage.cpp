#include "newui/dibimage.h"

#include <windows.h>

#include <cstring>

namespace newui {

bool imageToDibBytes(const BLImage& image, std::vector<std::uint8_t>& outDibBytes) {
    if (image.size().w <= 0 || image.size().h <= 0) {
        return false;
    }

    BLImageCodec codec;
    if (codec.find_by_name("BMP") != BL_SUCCESS) {
        return false;
    }

    BLArray<std::uint8_t> bmpBytes;
    if (image.write_to_data(bmpBytes, codec) != BL_SUCCESS) {
        return false;
    }
    if (bmpBytes.size() <= sizeof(BITMAPFILEHEADER)) {
        return false;
    }

    outDibBytes.assign(bmpBytes.data() + sizeof(BITMAPFILEHEADER),
        bmpBytes.data() + bmpBytes.size());
    return true;
}

bool dibBytesToImage(const std::vector<std::uint8_t>& dibBytes, BLImage& outImage) {
    if (dibBytes.size() < sizeof(BITMAPINFOHEADER)) {
        return false;
    }

    // bfOffBits has to point past the BITMAPINFOHEADER *and* any color
    // table to where the real pixel data starts (only relevant for <=8bpp
    // images - anything Blend2D itself writes is 24/32bpp, but this may
    // just as well be decoding a DIB some other application produced).
    const auto* infoHeader = reinterpret_cast<const BITMAPINFOHEADER*>(dibBytes.data());
    std::size_t headerAndColorsSize = infoHeader->biSize;
    if (infoHeader->biBitCount != 0 && infoHeader->biBitCount <= 8) {
        std::size_t colorsCount = (infoHeader->biClrUsed != 0)
            ? infoHeader->biClrUsed
            : (static_cast<std::size_t>(1) << infoHeader->biBitCount);
        headerAndColorsSize += colorsCount * sizeof(RGBQUAD);
    }

    BITMAPFILEHEADER fileHeader = {};
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfSize = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + dibBytes.size());
    fileHeader.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + headerAndColorsSize);

    std::vector<std::uint8_t> bmpBytes(sizeof(BITMAPFILEHEADER) + dibBytes.size());
    std::memcpy(bmpBytes.data(), &fileHeader, sizeof(BITMAPFILEHEADER));
    std::memcpy(bmpBytes.data() + sizeof(BITMAPFILEHEADER), dibBytes.data(), dibBytes.size());

    return outImage.read_from_data(bmpBytes.data(), bmpBytes.size()) == BL_SUCCESS;
}

}
