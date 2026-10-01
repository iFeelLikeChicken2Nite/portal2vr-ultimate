#pragma once

#include "tracking_space.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace RoomscaleMotion {

struct MotorSummary {
    std::uint64_t requestedCommands = 0;
    std::uint64_t manualCommands = 0;
    float maximumErrorUnits = 0.0f;
};

// Experimental feedback against RenderView's unmodified Source eye anchor.
// This is a view-anchor proxy, not proof of accepted collision-hull movement.
// Pose/render and command hooks communicate only through these locked snapshots.
class Motor {
public:
    using Clock = std::chrono::steady_clock;

    void Reset(bool recenter = false)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ResetLocked();
        if (recenter)
            m_HasVisualOffset = false;
    }

    Vector OnRender(const Vector &sourceAnchor, const Vector &hmdOffset,
                    std::uint64_t poseSequence, std::uintptr_t playerKey,
                    float scale, Clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!Finite(sourceAnchor) || !Finite(hmdOffset) || !poseSequence || !playerKey ||
            !std::isfinite(scale) || scale <= 0.0f) {
            ResetLocked();
            return SafeView(hmdOffset);
        }
        if (!m_HasVisualOffset) {
            m_VisualOffset = ClampVisual({hmdOffset.x, hmdOffset.y, 0.0f});
            m_HasVisualOffset = true;
        }
        const bool changedPlayer = playerKey != m_PlayerKey;
        const bool changedStream = poseSequence < m_PoseSequence;
        const bool anchorJump = m_HasRender && HorizontalLength(sourceAnchor - m_SourceAnchor) > 35.0f;
        const bool poseJump = m_HasRender && HorizontalLength(hmdOffset - m_HmdOffset) > 0.35f * scale;
        const bool externalMotion = m_HasRender && !m_LastRequested &&
            HorizontalLength(sourceAnchor - m_SourceAnchor) > kDeadzoneUnits &&
            HorizontalLength(hmdOffset - m_HmdOffset) <= kDeadzoneUnits;

        m_SourceAnchor = sourceAnchor;
        m_HmdOffset = hmdOffset;
        m_PlayerKey = playerKey;
        m_PoseSequence = poseSequence;
        m_RenderTime = now;
        if (!m_HasRender || changedPlayer || changedStream || anchorJump || poseJump ||
            externalMotion || m_ManualMovement) {
            ReanchorLocked();
            m_LastRequested = false;
        }
        m_HasRender = true;
        m_Error = m_OriginAnchor + (m_HmdOffset - m_HmdAnchor) - m_SourceAnchor;
        m_Error.z = 0.0f;
        const float errorLength = HorizontalLength(m_Error);
        if (!std::isfinite(errorLength)) {
            ResetLocked();
            return SafeView(hmdOffset);
        }
        m_Summary.maximumErrorUnits = (std::max)(m_Summary.maximumErrorUnits, errorLength);
        const float fraction = errorLength > kMaximumLeanUnits ? kMaximumLeanUnits / errorLength : 1.0f;
        m_VisualOffset = ClampVisual(m_BaseVisualOffset + m_Error * fraction);
        return SafeView(hmdOffset);
    }

    std::optional<TrackingSpace::MoveAxes> OnCommand(int commandNumber,
        const Vector &viewForward, bool manualMovement, Clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (commandNumber <= 0 || commandNumber == m_LastCommand)
            return std::nullopt;
        if (commandNumber < m_LastCommand) {
            // Level/replay command streams may restart. Do not replay an old target.
            m_LastCommand = commandNumber;
            ResetLocked();
            return std::nullopt;
        }
        m_LastCommand = commandNumber;
        if (!m_HasRender)
            return std::nullopt;
        if (now < m_RenderTime || now - m_RenderTime > std::chrono::milliseconds(250)) {
            ResetLocked();
            return std::nullopt;
        }
        if (manualMovement || m_ManualMovement) {
            // A manual-input release also establishes a fresh physical target.
            ReanchorLocked();
            m_ManualMovement = manualMovement;
            m_LastRequested = false;
            if (manualMovement)
                ++m_Summary.manualCommands;
            return std::nullopt;
        }
        const auto heading = TrackingSpace::HorizontalDirection({true, viewForward});
        const float errorLength = HorizontalLength(m_Error);
        if (!heading || !std::isfinite(errorLength) || errorLength <= kDeadzoneUnits) {
            m_LastRequested = false;
            return std::nullopt;
        }
        const float speed = (std::min)(175.0f, errorLength * 16.0f);
        const Vector velocity = m_Error * (speed / errorLength);
        const Vector right{heading->y, -heading->x, 0.0f};
        m_LastRequested = true;
        ++m_Summary.requestedCommands;
        return TrackingSpace::MoveAxes{
            velocity.x * heading->x + velocity.y * heading->y,
            velocity.x * right.x + velocity.y * right.y};
    }

    MotorSummary TakeSummary()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const auto summary = m_Summary;
        m_Summary = {};
        return summary;
    }

private:
    static constexpr float kDeadzoneUnits = 0.5f;
    static constexpr float kMaximumLeanUnits = 8.0f;

    static bool Finite(const Vector &value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    static float HorizontalLength(const Vector &value)
    {
        return std::hypot(value.x, value.y);
    }

    static Vector ClampVisual(const Vector &offset)
    {
        const float length = HorizontalLength(offset);
        return length > kMaximumLeanUnits ? offset * (kMaximumLeanUnits / length) : offset;
    }

    Vector SafeView(const Vector &hmdOffset) const
    {
        return {m_HasVisualOffset ? m_VisualOffset.x : 0.0f,
                m_HasVisualOffset ? m_VisualOffset.y : 0.0f,
                std::isfinite(hmdOffset.z) ? hmdOffset.z : 0.0f};
    }

    void ReanchorLocked()
    {
        m_OriginAnchor = m_SourceAnchor;
        m_HmdAnchor = m_HmdOffset;
        m_BaseVisualOffset = m_VisualOffset;
        m_Error = {0.0f, 0.0f, 0.0f};
    }

    void ResetLocked()
    {
        m_HasRender = false;
        m_ManualMovement = false;
        m_LastRequested = false;
        m_Error = {0.0f, 0.0f, 0.0f};
        // Preserve compensated XY across portals/tracking loss. Explicit recenter
        // clears it; forgetting it would re-add all previously accepted HMD motion.
    }

    std::mutex m_Mutex;
    bool m_HasRender = false;
    bool m_HasVisualOffset = false;
    bool m_ManualMovement = false;
    bool m_LastRequested = false;
    int m_LastCommand = 0;
    std::uint64_t m_PoseSequence = 0;
    std::uintptr_t m_PlayerKey = 0;
    Clock::time_point m_RenderTime{};
    Vector m_SourceAnchor{0, 0, 0};
    Vector m_HmdOffset{0, 0, 0};
    Vector m_OriginAnchor{0, 0, 0};
    Vector m_HmdAnchor{0, 0, 0};
    Vector m_VisualOffset{0, 0, 0};
    Vector m_BaseVisualOffset{0, 0, 0};
    Vector m_Error{0, 0, 0};
    MotorSummary m_Summary;
};

} // namespace RoomscaleMotion
