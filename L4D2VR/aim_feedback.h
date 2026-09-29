#pragma once

namespace AimFeedback {

inline bool ShouldRequestLaser(int aimMode, bool laserAvailable, bool controllerTracked,
                               bool activeWeapon, bool cursorVisible)
{
    return aimMode == 2 && laserAvailable && controllerTracked &&
        activeWeapon && !cursorVisible;
}

} // namespace AimFeedback
