#pragma once

#include <atomic>
#include <string>

// The Portal 2 Sixense MotionPack runs its own game DLLs: client_sixense.dll
// and server_sixense.dll replace client.dll and server.dll. Everything else in
// the mod names the stock modules; this maps those names onto whichever
// variant the running game actually loaded.
namespace GameModules {

enum class Variant { Unknown, Stock, Sixense };

inline constexpr const char *kStockClient = "client.dll";
inline constexpr const char *kStockServer = "server.dll";
inline constexpr const char *kSixenseClient = "client_sixense.dll";
inline constexpr const char *kSixenseServer = "server_sixense.dll";

template <typename Char>
bool SameName(const Char *a, const Char *b)
{
    const auto lower = [](Char c) { return (c >= 'A' && c <= 'Z') ? static_cast<Char>(c - 'A' + 'a') : c; };
    for (; *a && *b; ++a, ++b)
        if (lower(*a) != lower(*b)) return false;
    return *a == *b;
}

// The Sixense client wins when both are present: the MotionPack is a separate
// install, so a loaded client_sixense.dll means its game code is running.
template <typename IsLoaded>
Variant DetectVariant(IsLoaded isLoaded)
{
    if (isLoaded(kSixenseClient)) return Variant::Sixense;
    if (isLoaded(kStockClient)) return Variant::Stock;
    return Variant::Unknown;
}

inline const char *ResolveName(const char *name, Variant variant)
{
    if (variant != Variant::Sixense || !name) return name;
    if (SameName(name, kStockClient)) return kSixenseClient;
    if (SameName(name, kStockServer)) return kSixenseServer;
    return name;
}

inline std::atomic<Variant> &ActiveVariant()
{
    static std::atomic<Variant> variant{Variant::Unknown};
    return variant;
}

inline void SetVariant(Variant variant) { ActiveVariant().store(variant); }
inline Variant CurrentVariant() { return ActiveVariant().load(); }
inline bool IsSixense() { return CurrentVariant() == Variant::Sixense; }

inline const char *Resolve(const char *name) { return ResolveName(name, CurrentVariant()); }
inline std::string Resolve(const std::string &name) { return ResolveName(name.c_str(), CurrentVariant()); }

inline const wchar_t *ResolveWide(const wchar_t *name)
{
    if (!IsSixense() || !name) return name;
    if (SameName(name, L"client.dll")) return L"client_sixense.dll";
    if (SameName(name, L"server.dll")) return L"server_sixense.dll";
    return name;
}

} // namespace GameModules
