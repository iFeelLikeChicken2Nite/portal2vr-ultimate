#pragma once

#include "sdk/vector.h"

#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace PortalOrientation {

// Column basis in Source coordinates: X forward, Y left, Z up.
class Rotation {
public:
    static Rotation Identity()
    {
        return Rotation{};
    }

    static std::optional<Rotation> FromVMatrix(const VMatrix &matrix)
    {
        Rotation result;
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (!std::isfinite(matrix[row][column]))
                    return std::nullopt;
                result.m_Basis[row][column] = matrix[row][column];
            }
            if (!std::isfinite(matrix[row][3]))
                return std::nullopt;
        }
        for (int column = 0; column < 4; ++column)
            if (!std::isfinite(matrix[3][column]))
                return std::nullopt;
        if (std::fabs(matrix[3][0]) > 0.01f || std::fabs(matrix[3][1]) > 0.01f ||
            std::fabs(matrix[3][2]) > 0.01f || std::fabs(matrix[3][3] - 1.0f) > 0.01f)
            return std::nullopt;

        constexpr float kTolerance = 0.02f;
        for (int a = 0; a < 3; ++a) {
            for (int b = 0; b < 3; ++b) {
                float dot = 0.0f;
                for (int row = 0; row < 3; ++row)
                    dot += result.m_Basis[row][a] * result.m_Basis[row][b];
                if (std::fabs(dot - (a == b ? 1.0f : 0.0f)) > kTolerance)
                    return std::nullopt;
            }
        }
        const Vector forward = result.Rotate({1, 0, 0});
        const Vector left = result.Rotate({0, 1, 0});
        const Vector up = result.Rotate({0, 0, 1});
        const Vector cross{forward.y * left.z - forward.z * left.y,
                           forward.z * left.x - forward.x * left.z,
                           forward.x * left.y - forward.y * left.x};
        const float determinant = cross.x * up.x + cross.y * up.y + cross.z * up.z;
        if (std::fabs(determinant - 1.0f) > kTolerance)
            return std::nullopt;
        return result;
    }

    Vector Rotate(const Vector &vector) const
    {
        return {m_Basis[0][0] * vector.x + m_Basis[0][1] * vector.y + m_Basis[0][2] * vector.z,
                m_Basis[1][0] * vector.x + m_Basis[1][1] * vector.y + m_Basis[1][2] * vector.z,
                m_Basis[2][0] * vector.x + m_Basis[2][1] * vector.y + m_Basis[2][2] * vector.z};
    }

    // this * after: after acts on a vector first.
    Rotation Compose(const Rotation &after) const
    {
        Rotation result;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) {
                result.m_Basis[row][column] = 0.0f;
                for (int inner = 0; inner < 3; ++inner)
                    result.m_Basis[row][column] += m_Basis[row][inner] * after.m_Basis[inner][column];
            }
        return result;
    }

    Rotation Inverse() const
    {
        Rotation result;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                result.m_Basis[row][column] = m_Basis[column][row];
        return result;
    }

    Rotation YawOnly() const
    {
        const float yaw = HeadingRadians();
        const float sine = std::sin(yaw);
        const float cosine = std::cos(yaw);
        Rotation result;
        result.m_Basis[0][0] = cosine;
        result.m_Basis[1][0] = sine;
        result.m_Basis[0][1] = -sine;
        result.m_Basis[1][1] = cosine;
        return result;
    }

    Rotation PreserveHorizon() const
    {
        const Vector forward = Rotate({1, 0, 0});
        const float horizontal = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        const float yaw = HeadingRadians();
        const Vector left = horizontal > 0.0001f ?
            Vector{-forward.y / horizontal, forward.x / horizontal, 0.0f} :
            Vector{-std::sin(yaw), std::cos(yaw), 0.0f};
        const Vector up{forward.y * left.z - forward.z * left.y,
                        forward.z * left.x - forward.x * left.z,
                        forward.x * left.y - forward.y * left.x};
        Rotation result;
        result.SetColumns(forward, left, up);
        return result;
    }

    bool NearlyEquals(const Rotation &other) const
    {
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                if (std::fabs(m_Basis[row][column] - other.m_Basis[row][column]) > 0.0001f)
                    return false;
        return true;
    }

private:
    float m_Basis[3][3]{{1,0,0}, {0,1,0}, {0,0,1}};

    float HeadingRadians() const
    {
        const Vector forward = Rotate({1, 0, 0});
        if (forward.x * forward.x + forward.y * forward.y > 0.00000001f)
            return std::atan2(forward.y, forward.x);
        const Vector left = Rotate({0, 1, 0});
        return std::atan2(-left.x, left.y);
    }

    void SetColumns(const Vector &forward, const Vector &left, const Vector &up)
    {
        m_Basis[0][0] = forward.x; m_Basis[1][0] = forward.y; m_Basis[2][0] = forward.z;
        m_Basis[0][1] = left.x; m_Basis[1][1] = left.y; m_Basis[2][1] = left.z;
        m_Basis[0][2] = up.x; m_Basis[1][2] = up.y; m_Basis[2][2] = up.z;
    }
};

enum class Mode { LegacyYaw, FullRotation, YawOnly, PreserveHorizon };
enum class QueueResult { Disabled, Queued, Duplicate, Full, InvalidPlayer };

inline Rotation EffectiveRotation(Mode mode, const Rotation &world)
{
    if (mode == Mode::YawOnly)
        return world.YawOnly();
    if (mode == Mode::PreserveHorizon)
        return world.PreserveHorizon();
    return world;
}

struct FrameResult {
    Rotation world;
    Rotation effective;
    unsigned applied = 0;
    bool playerChanged = false;
};

class RigAnchor {
public:
    void Reanchor(const Vector &baseHmdOffset, const Vector &worldHmdOffset)
    {
        m_BaseHmdOffset = baseHmdOffset;
        m_WorldHmdOffset = worldHmdOffset;
        m_HasAnchor = true;
    }

    Vector MapHmd(const Vector &baseHmdOffset, const Rotation &effective) const
    {
        if (!m_HasAnchor)
            return baseHmdOffset;
        return m_WorldHmdOffset + effective.Rotate(baseHmdOffset - m_BaseHmdOffset);
    }

    Vector MapRelative(const Vector &relativeOffset, const Rotation &effective) const
    {
        return effective.Rotate(relativeOffset);
    }

    void Reset() { m_HasAnchor = false; }

private:
    bool m_HasAnchor = false;
    Vector m_BaseHmdOffset{0,0,0};
    Vector m_WorldHmdOffset{0,0,0};
};

class Coordinator {
public:
    void SetMode(Mode mode)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Mode == mode)
            return;
        m_Mode = mode;
        ClearAll();
    }

    Mode GetMode() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Mode;
    }

    QueueResult Queue(std::uintptr_t playerKey, std::uintptr_t portalKey,
                      const Rotation &rotation)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Mode == Mode::LegacyYaw)
            return QueueResult::Disabled;
        if (!playerKey || !portalKey)
            return QueueResult::InvalidPlayer;
        if (m_PlayerKey && m_PlayerKey != playerKey)
            ClearAll();
        m_PlayerKey = playerKey;
        if (!m_Pending.empty()) {
            const auto &last = m_Pending.back();
            if (last.portalKey == portalKey && last.rotation.NearlyEquals(rotation))
                return QueueResult::Duplicate;
        }
        if (m_Pending.empty() && m_LastPortalKey == portalKey &&
            m_LastRotation.NearlyEquals(rotation) &&
            m_FrameSequence - m_LastAppliedFrame <= 2)
            return QueueResult::Duplicate;
        if (m_Pending.size() == 4)
            return QueueResult::Full;
        m_Pending.push_back({portalKey, rotation});
        return QueueResult::Queued;
    }

    FrameResult Drain(std::uintptr_t playerKey)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const bool playerChanged = m_PlayerKey && playerKey != m_PlayerKey;
        if (playerChanged)
            ClearAll();
        m_PlayerKey = playerKey;
        ++m_FrameSequence;
        FrameResult result;
        result.playerChanged = playerChanged;
        while (!m_Pending.empty()) {
            const auto event = m_Pending.front();
            m_Pending.pop_front();
            m_World = event.rotation.Compose(m_World);
            m_LastPortalKey = event.portalKey;
            m_LastRotation = event.rotation;
            m_LastAppliedFrame = m_FrameSequence;
            ++result.applied;
        }
        result.world = m_World;
        result.effective = EffectiveRotation(m_Mode, m_World);
        return result;
    }

    void CancelPending()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pending.clear();
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ClearAll();
    }

private:
    struct Event {
        std::uintptr_t portalKey;
        Rotation rotation;
    };
    void ClearAll()
    {
        m_PlayerKey = 0;
        m_Pending.clear();
        m_World = Rotation::Identity();
        m_LastPortalKey = 0;
        m_LastRotation = Rotation::Identity();
        m_LastAppliedFrame = 0;
        m_FrameSequence = 0;
    }

    mutable std::mutex m_Mutex;
    Mode m_Mode = Mode::LegacyYaw;
    std::uintptr_t m_PlayerKey = 0;
    std::deque<Event> m_Pending;
    Rotation m_World;
    std::uintptr_t m_LastPortalKey = 0;
    Rotation m_LastRotation;
    std::uint64_t m_LastAppliedFrame = 0;
    std::uint64_t m_FrameSequence = 0;
};

} // namespace PortalOrientation
