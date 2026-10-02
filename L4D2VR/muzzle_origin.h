#pragma once

#include "sdk/vector.h"
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace MuzzleOrigin {

inline bool Finite(const Vector &v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline bool Finite(const QAngle &a)
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// Passive animated attachment sample, never a retained engine entity pointer.
// Local coordinates allow the next pose to move/turn the beam without reusing
// a world-space point from the previous render or forcing Source SetupBones.
class Sample {
public:
    void Invalidate() { m_Valid = false; }

    bool Capture(const Vector &world, const Vector &modelOrigin, const QAngle &modelAngles,
                 std::uintptr_t player, std::uintptr_t weapon, double now)
    {
        Invalidate();
        if (!player || !weapon || !Finite(world) || !Finite(modelOrigin) ||
            !Finite(modelAngles) || !std::isfinite(now))
            return false;
        Vector forward, right, up;
        QAngle::AngleVectors(modelAngles, &forward, &right, &up);
        const Vector delta = world - modelOrigin;
        // Generous bound for a viewmodel attachment, not a muzzle calibration.
        constexpr float maxOffsetUnits = 256.0f;
        if (!Finite(delta) || delta.LengthSqr() > maxOffsetUnits * maxOffsetUnits)
            return false;
        const auto dot = [&](const Vector &axis) {
            return delta.x * axis.x + delta.y * axis.y + delta.z * axis.z;
        };
        m_Local = {dot(forward), dot(right), dot(up)};
        if (!Finite(m_Local)) return false;
        m_Player = player;
        m_Weapon = weapon;
        m_Time = now;
        m_Valid = true;
        return true;
    }

    std::optional<Vector> Origin(const Vector &modelOrigin, const QAngle &modelAngles,
                                 std::uintptr_t player, std::uintptr_t weapon, double now) const
    {
        constexpr double maxAgeSeconds = 0.25;
        if (!m_Valid || !player || !weapon || player != m_Player || weapon != m_Weapon ||
            !std::isfinite(now) || now < m_Time || now - m_Time > maxAgeSeconds ||
            !Finite(modelOrigin) || !Finite(modelAngles))
            return std::nullopt;
        Vector forward, right, up;
        QAngle::AngleVectors(modelAngles, &forward, &right, &up);
        const Vector world = modelOrigin + forward * m_Local.x + right * m_Local.y + up * m_Local.z;
        return Finite(world) ? std::optional<Vector>{world} : std::nullopt;
    }

private:
    bool m_Valid = false;
    Vector m_Local{0, 0, 0};
    std::uintptr_t m_Player = 0, m_Weapon = 0;
    double m_Time = 0.0;
};

// Source may calculate attachments on a threaded bone job. Keep identity and
// sample as one synchronized state; no engine calls occur inside these locks.
class State {
public:
    void SelectIdentity(std::uintptr_t player, std::uintptr_t weapon)
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        if (player != m_Player || weapon != m_Weapon || !player || !weapon)
            m_Sample.Invalidate();
        m_Player = player;
        m_Weapon = weapon;
    }

    void Reset()
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        m_Sample.Invalidate();
        m_Player = m_Weapon = 0;
    }

    bool Capture(const Vector &world, const Vector &modelOrigin, const QAngle &modelAngles, double now)
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Sample.Capture(world, modelOrigin, modelAngles, m_Player, m_Weapon, now);
    }

    std::optional<Vector> Origin(const Vector &modelOrigin, const QAngle &modelAngles, double now) const
    {
        const std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Sample.Origin(modelOrigin, modelAngles, m_Player, m_Weapon, now);
    }

private:
    mutable std::mutex m_Mutex;
    Sample m_Sample;
    std::uintptr_t m_Player = 0, m_Weapon = 0;
};

} // namespace MuzzleOrigin
