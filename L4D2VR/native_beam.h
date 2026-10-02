#pragma once

#include <cstdint>
#include <cstring>

namespace NativeBeam
{
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
