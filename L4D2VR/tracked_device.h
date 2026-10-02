#pragma once
#include <cstdint>
#include <cmath>

constexpr bool IsUsableTrackedDeviceIndex(uint32_t index, uint32_t count, uint32_t invalid)
{
    return index != invalid && index < count;
}

inline bool IsUsableTrackedPose(const float (&matrix)[3][4],
                                const float (&velocity)[3],
                                const float (&angularVelocity)[3])
{
    for (const auto &row : matrix)
        for (float value : row)
            if (!std::isfinite(value)) return false;
    for (float value : velocity)
        if (!std::isfinite(value)) return false;
    for (float value : angularVelocity)
        if (!std::isfinite(value)) return false;
    // Tolerate only tiny numerical drift before clamping the asin input.
    return std::fabs(matrix[1][2]) <= 1.0001f;
}
