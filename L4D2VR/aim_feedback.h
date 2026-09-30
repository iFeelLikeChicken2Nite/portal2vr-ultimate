#pragma once

#include <cmath>
#include <optional>

namespace AimFeedback {

inline bool ShouldRequestLaser(int aimMode, bool laserAvailable, bool controllerTracked,
                               bool activeWeapon, bool cursorVisible)
{
    return aimMode == 2 && laserAvailable && controllerTracked &&
        activeWeapon && !cursorVisible;
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
