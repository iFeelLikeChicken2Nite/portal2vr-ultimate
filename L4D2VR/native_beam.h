#pragma once

#include "sdk/vector.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>

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
// own player-owned fallback, including native handle bookkeeping. This
// fallback is EYES_FOLLOW (6), NOT world-origin in the installed binary.
// FactoryAttachment removes that automatic CP0 update only for our beam.
constexpr std::uintptr_t kMuzzleLookupReturnOffset = 0xB3;
constexpr std::uintptr_t kPropertyCreateReturnOffset = 0x100;

inline int FactoryAttachment(std::uintptr_t scopedLookupReturn,
    std::uintptr_t returnAddress, const char *name, int attachType, int attachment)
{
    constexpr auto delta = kPropertyCreateReturnOffset - kMuzzleLookupReturnOffset;
    if (scopedLookupReturn && scopedLookupReturn <= UINTPTR_MAX - delta &&
        returnAddress == scopedLookupReturn + delta && name &&
        std::strcmp(name, "robot_point_beam") == 0 && attachType == 6 && attachment == -1)
        // Native updater 172754..172765 skips ABSORIGIN (0) after creation,
        // while EYES_FOLLOW (6) recomputes CP0 from EyePosition at 1729E9.
        // Keep native ownership/registration; VR alone maintains CP0/1/2.
        return 0;
    return attachType;
}

// Read-only observation: never dereference/retain ownership of an effect.
// In the audited build IClientRenderable is the subobject at primary+8.
class RenderProbe
{
public:
    void SetEffect(std::uintptr_t effect) { m_Effect.store(effect); }
    bool ObserveDraw(std::uintptr_t renderable, int eyeScope)
    {
        const auto effect = m_Effect.load();
        if (!effect || effect > UINTPTR_MAX - 8 || renderable != effect + 8)
            return false;
        const unsigned bit = eyeScope == 1 ? 1u : eyeScope == 2 ? 2u : 4u;
        return !(m_DrawScopes.fetch_or(bit) & bit);
    }
    std::optional<unsigned> StereoPairSummary()
    {
        // Only the outer RenderView thread owns this bounded counter.
        if (!m_Effect.load() || m_Reported || ++m_Pairs < 120)
            return std::nullopt;
        m_Reported = true;
        return m_DrawScopes.load();
    }
private:
    std::atomic<std::uintptr_t> m_Effect{0};
    std::atomic<unsigned> m_DrawScopes{0};
    unsigned m_Pairs = 0;
    bool m_Reported = false;
};

inline bool UsePlayerOwnedFallback(std::uintptr_t scopedCaller,
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
