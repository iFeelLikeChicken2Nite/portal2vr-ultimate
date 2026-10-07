#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// The Sixense Perceptual Pack's client_sixense.dll predates the four
// "unscaled" ints Portal 2 later added after x/y/width/height, so its
// CViewSetup is 0xF4 bytes instead of 0x104. Everything after them (the ortho
// and custom-matrix block, then fov through the end) is the same bytes, only
// 0x10 earlier. Portal2VR keeps working on the stock layout and converts at
// the RenderView boundary.
//
//                     stock          Sixense
//   x, y, w, h        0, 8, 10, 18   0, 4, 8, C
//   unscaled x/y/w/h  4, C, 14, 1C   -
//   ortho..matrix     20..67         10..57
//   fov..end          68..103        58..F3
namespace ViewSetupLayout {

inline constexpr std::size_t kStockSize = 0x104;
inline constexpr std::size_t kSixenseSize = 0xF4;

inline void FromSixense(const void *sixense, void *stock)
{
    const auto *src = static_cast<const std::uint8_t *>(sixense);
    auto *dst = static_cast<std::uint8_t *>(stock);
    std::memset(dst, 0, kStockSize);
    for (int i = 0; i < 4; ++i) {
        // x, y, width, height; the unscaled copies match until Portal2VR edits them.
        std::memcpy(dst + 8 * i, src + 4 * i, 4);
        std::memcpy(dst + 8 * i + 4, src + 4 * i, 4);
    }
    std::memcpy(dst + 0x20, src + 0x10, 0x48);
    std::memcpy(dst + 0x68, src + 0x58, 0x9C);
}

inline void ToSixense(const void *stock, void *sixense)
{
    const auto *src = static_cast<const std::uint8_t *>(stock);
    auto *dst = static_cast<std::uint8_t *>(sixense);
    for (int i = 0; i < 4; ++i)
        std::memcpy(dst + 4 * i, src + 8 * i, 4);
    std::memcpy(dst + 0x10, src + 0x20, 0x48);
    std::memcpy(dst + 0x58, src + 0x68, 0x9C);
}

} // namespace ViewSetupLayout
