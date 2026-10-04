#pragma once

#include <cstdint>

namespace CompatibilityHooks {
// ClientModePortalNormal's getter returns ST(0) as float and takes this in ECX.
using ViewModelFov = float (__thiscall *)(void *);

inline bool IsVerifiedShootCaller(std::uintptr_t actual, std::uintptr_t verified)
{
    // The target is folded with another virtual method. Never change its
    // result for that method, an unknown call site, or a missing build guard.
    return verified != 0 && actual == verified;
}
} // namespace CompatibilityHooks
