#pragma once

#include "tracking_space.h"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

namespace RoomscaleMotion {

enum class Mode { Off, Observe, ActiveExperimental };

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
        if (poseSequence <= m_PreviousSequence)
            return Observation::NoStep;
        if (yawDegrees != m_PreviousYawDegrees || scale != m_PreviousScale) {
            m_PreviousMeters = hmdMeters;
            m_PreviousSequence = poseSequence;
            m_PreviousYawDegrees = yawDegrees;
            m_PreviousScale = scale;
            m_PendingUnits = {0.0f, 0.0f, 0.0f};
            return Observation::MappingChanged;
        }
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

// Observe-only bridge. The render/pose and CreateMove hooks may have different cadences.
struct Summary {
    std::uint64_t steps = 0;
    float distanceUnits = 0.0f;
};

class Observer {
public:
    bool SetMode(Mode mode)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Mode == mode)
            return false;
        m_Mode = mode;
        m_Steps.Reset();
        m_Eligible = false;
        m_Summary = {};
        return true;
    }

    void Reset()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Steps.Reset();
        m_Eligible = false;
    }

    Observation OnPose(bool valid, const Vector &hmdMeters, std::uint64_t poseSequence,
                       float yawDegrees, float scale, bool gameplayEligible)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Mode == Mode::Off)
            return Observation::NoStep;
        if (!gameplayEligible) {
            m_Steps.Reset();
            m_Eligible = false;
            return Observation::NoStep;
        }
        const auto status = m_Steps.Observe(valid, hmdMeters, poseSequence, yawDegrees, scale);
        m_Eligible = valid && status != Observation::InvalidSample;
        return status;
    }

    std::optional<Vector> OnCommand(int commandNumber, bool gameplayEligible)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Mode == Mode::Off)
            return std::nullopt;
        if (!gameplayEligible) {
            m_Steps.Reset();
            m_Eligible = false;
            return std::nullopt;
        }
        if (!m_Eligible)
            return std::nullopt;
        const auto step = m_Steps.Consume(commandNumber);
        if (step) {
            ++m_Summary.steps;
            m_Summary.distanceUnits += std::sqrt(step->x * step->x + step->y * step->y);
        }
        return step;
    }

    Summary TakeSummary()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const Summary summary = m_Summary;
        m_Summary = {};
        return summary;
    }

private:
    std::mutex m_Mutex;
    Mode m_Mode = Mode::Off;
    bool m_Eligible = false;
    StepAccumulator m_Steps;
    Summary m_Summary;
};

} // namespace RoomscaleMotion
