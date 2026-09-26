#pragma once

#include "sdk/vector.h"
#include <cmath>

// Tracking positions are Source-oriented meters until converted at this boundary.
namespace TrackingSpace {

inline Vector OpenVrToSourceMeters(const Vector &openVrMeters)
{
    return {-openVrMeters.z, -openVrMeters.x, openVrMeters.y};
}

inline Vector SourceToOpenVrMeters(const Vector &sourceMeters)
{
    return {-sourceMeters.y, sourceMeters.z, -sourceMeters.x};
}

inline Vector RotateYawDegrees(const Vector &sourceVector, float degrees)
{
    const float radians = DEG2RAD(degrees);
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    return {sourceVector.x * cosine - sourceVector.y * sine,
            sourceVector.x * sine + sourceVector.y * cosine,
            sourceVector.z};
}

enum class TrackingMode { Seated, Standing };

struct PlayspaceState {
    Vector centerMeters{0.0f, 0.0f, 0.0f};
    Vector translationUnits{0.0f, 0.0f, 0.0f};
    float yawDegrees = 0.0f;
    float scale = 43.2f;
    float heightOffsetMeters = 0.0f;
    TrackingMode mode = TrackingMode::Seated;

    void Recenter(const Vector &hmdMeters)
    {
        centerMeters.x = hmdMeters.x;
        centerMeters.y = hmdMeters.y;
        if (mode == TrackingMode::Seated)
            centerMeters.z = hmdMeters.z;
        translationUnits = {0.0f, 0.0f, 0.0f};
    }

    Vector HmdOffsetUnits(const Vector &hmdMeters, float eyeHeightUnits) const
    {
        const Vector rotated = RotateYawDegrees(hmdMeters - centerMeters, yawDegrees);
        Vector offset = rotated * scale + translationUnits;
        if (mode == TrackingMode::Standing)
            offset.z = (hmdMeters.z + heightOffsetMeters) * scale - eyeHeightUnits + translationUnits.z;
        return offset;
    }

    Vector ControllerOffsetUnits(const Vector &controllerMeters, const Vector &hmdMeters,
                                 float eyeHeightUnits) const
    {
        return HmdOffsetUnits(hmdMeters, eyeHeightUnits) +
               RotateYawDegrees(controllerMeters - hmdMeters, yawDegrees) * scale;
    }

    void TurnAboutHmd(float deltaDegrees, const Vector &hmdMeters, float eyeHeightUnits)
    {
        const Vector before = HmdOffsetUnits(hmdMeters, eyeHeightUnits);
        yawDegrees += deltaDegrees;
        const Vector after = HmdOffsetUnits(hmdMeters, eyeHeightUnits);
        translationUnits.x += before.x - after.x;
        translationUnits.y += before.y - after.y;
    }
};

} // namespace TrackingSpace
