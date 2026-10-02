#pragma once

#include "sdk/vector.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace NativeBeam
{
inline Vector ControlPointColor(int portalColor, const Vector (&palette)[3])
{
    // The installed native factory writes byte-scale RGB directly into CP2.
    // robot_point_beam remaps 0..255 to 0..1; retain native intensity here.
    return palette[std::clamp(portalColor, 0, 2)];
}

// client.dll 6AA07473 / FF3000: CreatePingPointer+0xB3 is the return
// address of its muzzle lookup. Returning -1 ONLY here selects the game's
// own player-owned PATTACH_WORLDORIGIN branch, including native handle
// bookkeeping. All other weapon/light/animated muzzle lookups stay intact.
constexpr std::uintptr_t kMuzzleLookupReturnOffset = 0xB3;

inline bool UseWorldOrigin(std::uintptr_t scopedCaller,
                           std::uintptr_t returnAddress, const char *name)
{
    return scopedCaller && returnAddress == scopedCaller && name &&
        std::strcmp(name, "muzzle") == 0;
}

class CreationScope
{
public:
    CreationScope(std::uintptr_t &caller, std::uintptr_t nativeReturnAddress)
        : m_Caller(caller), m_Previous(caller) { m_Caller = nativeReturnAddress; }
    ~CreationScope() { m_Caller = m_Previous; }
    CreationScope(const CreationScope &) = delete;
    CreationScope &operator=(const CreationScope &) = delete;
private:
    std::uintptr_t &m_Caller;
    std::uintptr_t m_Previous;
};
} // namespace NativeBeam
