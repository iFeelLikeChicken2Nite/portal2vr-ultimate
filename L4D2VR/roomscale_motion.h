#pragma once

#include "tracking_space.h"

#include <cmath>
#include <cstdint>
#include <optional>

namespace RoomscaleMotion {

enum class Observation {
    Baseline, StepQueued, NoStep, TrackingLost, TrackingRecovered,
    Discontinuity, MappingChanged, InvalidSample
};

class StepAccumulator {
public:
    void Reset()
    {
        m_HasBaseline = false;
        m_TrackingLost = false;
        m_PreviousSequence = 0;
        m_LastCommand = 0;
        m_PendingUnits = {0.0f, 0.0f, 0.0f};
    }

    Observation Observe(bool valid, const Vector &hmdMeters, std::uint64_t poseSequence,
                        float yawDegrees, float scale)
    {
        if (!valid) {
            const bool wasTracking = m_HasBaseline;
            m_HasBaseline = false;
            m_TrackingLost = true;
            m_PendingUnits = {0.0f, 0.0f, 0.0f};
            m_LastCommand = 0;
            return wasTracking ? Observation::TrackingLost : Observation::NoStep;
        }
        if (!std::isfinite(hmdMeters.x) || !std::isfinite(hmdMeters.y) ||
            !std::isfinite(hmdMeters.z) || !std::isfinite(yawDegrees) ||
            !std::isfinite(scale) || scale <= 0.0f) {
            Reset();
            m_TrackingLost = true;
            return Observation::InvalidSample;
        }
        if (!m_HasBaseline) {
            m_PreviousMeters = hmdMeters;
            m_PreviousSequence = poseSequence;
            m_PreviousYawDegrees = yawDegrees;
            m_PreviousScale = scale;
            m_HasBaseline = true;
            const bool recovered = m_TrackingLost;
            m_TrackingLost = false;
            return recovered ? Observation::TrackingRecovered : Observation::Baseline;
        }
        if (yawDegrees != m_PreviousYawDegrees || scale != m_PreviousScale) {
            m_PreviousMeters = hmdMeters;
            m_PreviousSequence = poseSequence;
            m_PreviousYawDegrees = yawDegrees;
            m_PreviousScale = scale;
            m_PendingUnits = {0.0f, 0.0f, 0.0f};
            return Observation::MappingChanged;
        }
        if (poseSequence <= m_PreviousSequence)
            return Observation::NoStep;
        Vector delta = hmdMeters - m_PreviousMeters;
        // A larger single-pose jump is treated as tracking relocalization, not walking.
        constexpr float kMaxPoseStepMeters = 0.35f;
        const float lengthSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (!std::isfinite(lengthSquared) || lengthSquared > kMaxPoseStepMeters * kMaxPoseStepMeters) {
            m_PreviousMeters = hmdMeters;
            m_PreviousSequence = poseSequence;
            m_PendingUnits = {0.0f, 0.0f, 0.0f};
            return Observation::Discontinuity;
        }
        delta.z = 0.0f;
        m_PendingUnits += TrackingSpace::RotateYawDegrees(delta, yawDegrees) * scale;
        m_PreviousMeters = hmdMeters;
        m_PreviousSequence = poseSequence;
        return (delta.x == 0.0f && delta.y == 0.0f) ?
            Observation::NoStep : Observation::StepQueued;
    }

    std::optional<Vector> Consume(int commandNumber)
    {
        if (commandNumber <= 0 || commandNumber <= m_LastCommand)
            return std::nullopt;
        m_LastCommand = commandNumber;
        if (m_PendingUnits.x == 0.0f && m_PendingUnits.y == 0.0f)
            return std::nullopt;
        const Vector step = m_PendingUnits;
        m_PendingUnits = {0.0f, 0.0f, 0.0f};
        return step;
    }

private:
    bool m_HasBaseline = false;
    bool m_TrackingLost = false;
    Vector m_PreviousMeters{0.0f, 0.0f, 0.0f};
    Vector m_PendingUnits{0.0f, 0.0f, 0.0f};
    std::uint64_t m_PreviousSequence = 0;
    float m_PreviousYawDegrees = 0.0f;
    float m_PreviousScale = 43.2f;
    int m_LastCommand = 0;
};

} // namespace RoomscaleMotion
