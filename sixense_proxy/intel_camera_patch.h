#pragma once

// The Steam Perceptual Pack (app 247120) crashes at the Valve logo on every PC
// without its Creative Senz3D camera: client_sixense.dll's player init opens
// the Intel Perceptual camera when the convar sixense_intel_enabled is on (its
// default "1"), gets a null capture device and dereferences it.
//
// sixense_intel_enabled is the game's own master switch for the camera code,
// so the fix is to make its default "0". client_sixense.dll imports
// sixense.dll, so this proxy is initialised after client_sixense.dll is mapped
// but before its static ConVar constructors run. The registration is
//
//     push 2000h            ; flags
//     push <"1">            ; default value
//     push <"sixense_intel_enabled">
//     mov  ecx, <ConVar>
//
// and the patch repoints the default-value operand at a "0" string. Matching
// on the strings (not on fixed offsets) keeps it valid for any build of
// client_sixense.dll that registers the convar this way.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace IntelCameraPatch {

inline constexpr char kConVarName[] = "sixense_intel_enabled";
inline constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

// Image bytes as mapped in memory, with `base` the address of image[0].
// Returns the offset of the 4-byte default-value operand to rewrite, or
// kNotFound when the registration is not there or does not default to "1".
inline std::size_t FindEnabledDefaultOperand(const std::uint8_t *image, std::size_t size,
                                             std::uint32_t base)
{
    if (!image || size < 16) return kNotFound;

    auto stringAt = [&](std::uint32_t address, const char *text) {
        const std::size_t length = std::strlen(text) + 1; // include the terminator
        if (address < base) return false;
        const std::size_t offset = address - base;
        return offset < size && length <= size - offset &&
            std::memcmp(image + offset, text, length) == 0;
    };
    auto operand = [&](std::size_t offset) {
        std::uint32_t value;
        std::memcpy(&value, image + offset, sizeof(value));
        return value;
    };

    // push imm32 (flags) ; push imm32 (default) ; push imm32 (name)
    for (std::size_t i = 0; i + 15 <= size; ++i) {
        if (image[i] != 0x68 || image[i + 5] != 0x68 || image[i + 10] != 0x68)
            continue;
        if (!stringAt(operand(i + 11), kConVarName) || !stringAt(operand(i + 6), "1"))
            continue;
        return i + 6;
    }
    return kNotFound;
}

} // namespace IntelCameraPatch
