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
// client_sixense.dll that registers the convar this way. The convar does not
// guard every path, so FindCameraAllocationChecks below covers the rest.

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

// The camera is also (re)created outside the convar's reach, e.g. by the
// "connect camera" retry, and every path makes the same null-device
// dereference. Each site allocates the 0x2948-byte camera object and checks
// the allocation before constructing it:
//
//     push 2948h            ; (a store may sit between push and call)
//     call <operator new>
//     add  esp, 4
//     cmp  eax, <reg>       ; reg holds 0
//     je   <no camera>      ; 0F 84 rel32
//     mov  ecx, eax
//     call <camera ctor>
//
// Turning each je into a jmp takes the game's own "no camera" path, so the
// camera object is never created. Returns the offsets of those je opcodes.
inline constexpr std::uint32_t kCameraSize = 0x2948;
inline constexpr std::size_t kMaxCameraSites = 8;

inline std::size_t FindCameraAllocationChecks(const std::uint8_t *image, std::size_t size,
                                              std::size_t (&sites)[kMaxCameraSites])
{
    if (!image) return 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i + 5 <= size && count < kMaxCameraSites; ++i) {
        if (image[i] != 0x68) continue;
        std::uint32_t pushed;
        std::memcpy(&pushed, image + i + 1, sizeof(pushed));
        if (pushed != kCameraSize) continue;
        // The call to operator new follows within a few instructions.
        for (std::size_t at = i + 5; at <= i + 21 && at + 24 <= size; ++at) {
            const std::uint8_t *p = image + at;
            if (p[0] == 0xE8 && p[5] == 0x83 && p[6] == 0xC4 && p[7] == 0x04 &&
                p[8] == 0x3B && (p[9] & 0xF8) == 0xC0 &&     // cmp eax, r32
                p[10] == 0x0F && p[11] == 0x84 &&               // je rel32
                p[16] == 0x8B && p[17] == 0xC8 && p[18] == 0xE8) { // mov ecx,eax; call
                sites[count++] = at + 10;
                break;
            }
        }
    }
    return count;
}

// With no camera object, a per-frame check (run for each split-screen slot,
// whatever sixense_intel_enabled says) raises "Connect Senz3D camera to
// computer" once the player is in game, and keeps the map paused behind it:
//
//     call <camera ready?>
//     test al, al
//     jne  <skip prompt>        ; 75 rel8, patched to jmp (EB)
//     cmp  [esi+838h], ebx      ; no prompt already pending
//     jne  <skip prompt>
//     mov  byte [esi+83Dh], 1   ; prompt showing
//
// Returns the offset of that first jne, or kNotFound.
inline std::size_t FindCameraPromptBranch(const std::uint8_t *image, std::size_t size)
{
    static constexpr int kPattern[] = {
        0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x75, -1,
        0x39, 0x9E, 0x38, 0x08, 0x00, 0x00, 0x75, -1,
        0xC6, 0x86, 0x3D, 0x08, 0x00, 0x00, 0x01 };
    constexpr std::size_t length = sizeof(kPattern) / sizeof(kPattern[0]);
    if (!image || size < length) return kNotFound;
    std::size_t match = kNotFound;
    for (std::size_t i = 0; i + length <= size; ++i) {
        std::size_t j = 0;
        while (j < length && (kPattern[j] < 0 || image[i + j] == kPattern[j])) ++j;
        if (j != length) continue;
        if (match != kNotFound) return kNotFound; // ambiguous: leave the game alone
        match = i + 7;
    }
    return match;
}

// Rewrites `je rel32` at site as `jmp rel32; nop` to the same target.
inline void JeToJmp(std::uint8_t *site)
{
    std::int32_t rel;
    std::memcpy(&rel, site + 2, sizeof(rel));
    rel += 1; // jmp rel32 is one byte shorter than je rel32
    site[0] = 0xE9;
    std::memcpy(site + 1, &rel, sizeof(rel));
    site[5] = 0x90;
}

} // namespace IntelCameraPatch
