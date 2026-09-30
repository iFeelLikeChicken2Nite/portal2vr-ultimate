#pragma once

#include <cmath>
#include <optional>
#include "sdk/vector.h"

namespace AimFeedback {

inline bool ShouldRequestLaser(int aimMode, bool laserAvailable, bool controllerTracked,
                               bool activeWeapon, bool cursorVisible)
{
    return aimMode == 2 && laserAvailable && controllerTracked &&
        activeWeapon && !cursorVisible;
}

inline bool ShouldUseWorldAimMarker(int aimMode, bool enabled, bool overlayAvailable,
                                   bool controllerTracked, bool activeWeapon, bool cursorVisible)
{
    return aimMode == 2 && enabled && overlayAvailable && controllerTracked &&
           activeWeapon && !cursorVisible;
}

inline bool ShouldInspectActiveWeaponForAim(int aimMode, bool laserAvailable,
                                            bool markerEnabled, bool overlayAvailable)
{
    return aimMode == 2 && (laserAvailable || (markerEnabled && overlayAvailable));
}

struct WorldAimGeometry
{
    Vector beamEnd;
    Vector impactPoint;
    bool showImpact;
};

inline std::optional<WorldAimGeometry> PrepareWorldAimGeometry(
    const Vector &origin, const Vector &target, bool traceHit)
{
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))
        return std::nullopt;
    const Vector delta = target - origin;
    const double distance = std::sqrt(static_cast<double>(delta.x) * delta.x +
        static_cast<double>(delta.y) * delta.y + static_cast<double>(delta.z) * delta.z);
    if (!std::isfinite(distance) || distance < 0.01)
        return std::nullopt;

    const float impactInset = static_cast<float>(
        (distance < 4.0 ? distance * 0.25 : 1.0) / distance);
    return WorldAimGeometry{target, target - delta * impactInset, traceHit};
}

struct ScreenPoint
{
    int x;
    int y;
};

inline std::optional<ScreenPoint> ProjectedCrosshairPosition(
    bool clipped, float projectedX, float projectedY, int sourceX, int sourceY,
    int windowWidth, int windowHeight, int renderWidth, int renderHeight,
    int outputWidth, int outputHeight)
{
    if (clipped || !std::isfinite(projectedX) || !std::isfinite(projectedY) ||
        windowWidth <= 0 || windowHeight <= 0 || renderWidth <= 0 || renderHeight <= 0 ||
        outputWidth <= 0 || outputHeight <= 0 ||
        projectedX < 0.0f || projectedX >= renderWidth ||
        projectedY < 0.0f || projectedY >= renderHeight)
        return std::nullopt;

    const float x = projectedX * outputWidth / renderWidth + sourceX - windowWidth * 0.5f;
    const float y = projectedY * outputHeight / renderHeight + sourceY - windowHeight * 0.5f;
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < 0.0f || x >= outputWidth || y < 0.0f || y >= outputHeight)
        return std::nullopt;
    return ScreenPoint{static_cast<int>(x), static_cast<int>(y)};
}

inline std::optional<ScreenPoint> ProjectedCrosshairPosition(
    bool clipped, float projectedX, float projectedY, int sourceX, int sourceY,
    int windowWidth, int windowHeight, int renderWidth, int renderHeight)
{
    return ProjectedCrosshairPosition(clipped, projectedX, projectedY, sourceX, sourceY,
        windowWidth, windowHeight, renderWidth, renderHeight, renderWidth, renderHeight);
}

} // namespace AimFeedback
