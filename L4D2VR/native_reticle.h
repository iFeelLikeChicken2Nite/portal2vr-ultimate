#pragma once

#include "aim_feedback.h"
#include <cstdint>
#include <limits>
#include <optional>

// Installed Portal 2 x86 vguimatsurface.dll evidence; NOT the SDK 2013 ABI.
// AA10 translates the rectangle then clips it against the current HUD panel.
// 7A10 sets its orthographic canvas from the active render-context viewport.
namespace NativeReticle
{
inline float DistanceScale(bool enabled, float distanceUnits, float unitsPerMeter)
{
    if (!enabled || !std::isfinite(distanceUnits) || distanceUnits < 0.0f ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0f)
        return 1.0f;
    // Keep the HUD readable: smooth, bounded reduction over 1-10 real meters.
    const double meters = static_cast<double>(distanceUnits) / unitsPerMeter;
    const double t = std::clamp((meters - 1.0) / 9.0, 0.0, 1.0);
    return static_cast<float>(1.0 - 0.2 * t * t * (3.0 - 2.0 * t));
}

struct SpriteLayout
{
    AimFeedback::ScreenPoint position;
    int width, height;
};

inline std::optional<SpriteLayout> ScaleLayout(const AimFeedback::ScreenPoint &position,
    const AimFeedback::ProjectedPoint &hit, int width, int height, float scale)
{
    if (width <= 0 || height <= 0 || width > 192 || height > 192 ||
        !std::isfinite(scale) || scale < 0.8f || scale > 1.0f ||
        !std::isfinite(hit.x) || !std::isfinite(hit.y))
        return std::nullopt;
    // Scale all status layers about the shared world hit, not individual
    // sprite centers. Leave Source's atlas UV, artwork, color and alpha alone.
    const double x = std::round(hit.x + (static_cast<double>(position.x) - hit.x) * scale);
    const double y = std::round(hit.y + (static_cast<double>(position.y) - hit.y) * scale);
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
        return std::nullopt;
    return SpriteLayout{{static_cast<int>(x), static_cast<int>(y)},
        (std::max)(1, static_cast<int>(std::lround(width * scale))),
        (std::max)(1, static_cast<int>(std::lround(height * scale)))};
}

constexpr std::uintptr_t kClipSetterRva = 0x1010;
constexpr std::uintptr_t kClipRectRva = 0x13B680;
constexpr std::uintptr_t kSurfaceTranslationOffset = 0xC;
constexpr std::size_t kDrawTexturedSubRectSlot = 103;
constexpr std::size_t kGetScreenSizeSlot = 42;

struct SurfaceProbe
{
    std::uintptr_t module;
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uintptr_t vtable;
    std::uintptr_t drawTexturedSubRect;
    std::uintptr_t getScreenSize;
};

inline bool Supported(const SurfaceProbe &probe)
{
    return probe.module && probe.timestamp == 0x6A4466CE && probe.imageSize == 0x198000 &&
        probe.module <= UINTPTR_MAX - probe.imageSize &&
        probe.vtable == probe.module + 0xC4ED4 &&
        probe.drawTexturedSubRect == probe.module + 0xAA10 &&
        probe.getScreenSize == probe.module + 0xB8C0;
}

struct ClipRect
{
    int left, top, right, bottom;

    bool Contains(int x, int y, int width, int height) const
    {
        return width > 0 && height > 0 && x >= left && y >= top &&
            static_cast<std::int64_t>(x) + width <= right &&
            static_cast<std::int64_t>(y) + height <= bottom;
    }
};

inline std::optional<AimFeedback::ScreenPoint> SurfacePosition(
    const AimFeedback::ScreenPoint &eyePosition, int translationX, int translationY)
{
    const auto x = static_cast<std::int64_t>(eyePosition.x) - translationX;
    const auto y = static_cast<std::int64_t>(eyePosition.y) - translationY;
    if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX)
        return std::nullopt;
    return AimFeedback::ScreenPoint{static_cast<int>(x), static_cast<int>(y)};
}

// SurfaceAccess supplies the verified native cdecl clip setter. No direct
// writes to game object/global fields, and no change to clipping enable state.
template<class SurfaceAccess>
class ClipScope
{
public:
    ClipScope(SurfaceAccess &surface, const ClipRect &previous, const ClipRect &eye)
        : m_Surface(surface), m_Previous(previous) { m_Surface.SetClipRect(eye); }
    ~ClipScope() { m_Surface.SetClipRect(m_Previous); }
    ClipScope(const ClipScope &) = delete;
    ClipScope &operator=(const ClipScope &) = delete;
private:
    SurfaceAccess &m_Surface;
    ClipRect m_Previous;
};
} // namespace NativeReticle
