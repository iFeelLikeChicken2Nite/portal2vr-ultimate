#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class StartupSymbolRole { Required, WarningOnly, Laser };

inline StartupSymbolRole RoleForStartupSymbol(std::string_view name)
{
    if (name == "EyePosition" || name == "Weapon_ShootPosition" ||
        name == "GetViewModelFOV")
        return StartupSymbolRole::WarningOnly;
    if (name == "CreatePingPointer" || name == "Precache" ||
        name == "PrecacheParticleSystem" || name == "SetControlPoint" ||
        name == "StopEmission")
        return StartupSymbolRole::Laser;
    return StartupSymbolRole::Required;
}

struct StartupSymbolStatus {
    bool requiredAvailable = true;
    bool laserAvailable = true;
    std::vector<std::string> warnings;
};

inline void InspectStartupSymbol(StartupSymbolStatus &status, const char *name,
                                 std::uintptr_t address)
{
    if (address) return;
    switch (RoleForStartupSymbol(name)) {
    case StartupSymbolRole::Required:
        status.requiredAvailable = false;
        break;
    case StartupSymbolRole::WarningOnly:
        status.warnings.emplace_back(name);
        break;
    case StartupSymbolRole::Laser:
        status.laserAvailable = false;
        break;
    }
}
