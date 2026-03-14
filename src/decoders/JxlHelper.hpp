
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include <jxl/decode.h>

namespace JxlHelper
{
    // Shared RGBA uint8 pixel format used by all JXL decoders in ANPV
    inline constexpr JxlPixelFormat jxlFormat = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };

    // Decode a JXL codestream to RGBA uint8 pixels (top-down row order).
    // On success, outWidth and outHeight are set and the returned vector
    // contains (outWidth * outHeight * 4) bytes in RGBA order.
    std::vector<uint8_t> decodeCodestream(const uint8_t *data, size_t dataSize, uint32_t &outWidth, uint32_t &outHeight);

    // Convert RGBA byte-ordered pixels to ARGB32 (0xAARRGGBB) layout.
    void convertRGBAtoARGB32(uint32_t *__restrict dst, const uint8_t *__restrict src, uint32_t count);
}
