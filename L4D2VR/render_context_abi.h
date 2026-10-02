#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace Portal2MaterialAbi {

// Verified against the Windows Portal 2 materialsystem.dll dated 2026-07-01.
// Other builds must retain the native HUD draw rather than call unknown slots.
inline constexpr std::size_t kViewportSlot = 38;
inline constexpr std::size_t kGetViewportSlot = 39;
inline constexpr std::size_t kDrawScreenSpaceRectangleSlot = 105;

enum class Kind { Unsupported, Queued, Immediate };

struct Probe
{
    std::uintptr_t moduleBase;
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uintptr_t vtable;
    std::uintptr_t viewport;
    std::uintptr_t getViewport;
    std::uintptr_t drawScreenSpaceRectangle;
};

inline Kind Classify(const Probe &probe)
{
    if (probe.moduleBase == 0 || probe.timestamp != 0x6A4466CAu ||
        probe.imageSize != 0x14D000u ||
        probe.moduleBase > (std::numeric_limits<std::uintptr_t>::max)() - probe.imageSize)
        return Kind::Unsupported;
    const auto at = [&](std::uintptr_t rva) { return probe.moduleBase + rva; };
    if (probe.vtable == at(0x9BEF4u) && probe.viewport == at(0x27710u) &&
        probe.getViewport == at(0x25190u) &&
        probe.drawScreenSpaceRectangle == at(0x27C40u))
        return Kind::Queued;
    if (probe.vtable == at(0x9ED4Cu) && probe.viewport == at(0x2D8B0u) &&
        probe.getViewport == at(0x2CAF0u) &&
        probe.drawScreenSpaceRectangle == at(0x2A510u))
        return Kind::Immediate;
    return Kind::Unsupported;
}

} // namespace Portal2MaterialAbi
