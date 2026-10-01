#pragma once
#include <cmath>
#include <optional>

class MenuOverlayPlacement
{
public:
    struct Pose { float m[3][4]; };
    // Opening heading only; translation is updated on every valid frame.
    bool ShouldAttempt(bool visible, bool hmdPoseValid) const
    {
        return hmdPoseValid && (!visible || !m_Positioned);
    }

    void RecordResult(bool succeeded)
    {
        m_Positioned = succeeded;
    }

    void Invalidate() { m_Positioned = false; }

    // Raw OpenVR meters, never Source/player coordinates. Lock heading while
    // visible, but keep translation relative to the current physical HMD.
    std::optional<Pose> UpdatePose(const float (&hmd)[3][4], bool visible) {
        for (const auto &row : hmd)
            for (float value : row)
                if (!std::isfinite(value)) {
                    Invalidate();
                    return std::nullopt;
                }
        if (ShouldAttempt(visible, true)) {
            const float length = std::hypot(hmd[0][2], hmd[2][2]);
            // A nearly vertical gaze must not generate a degenerate heading.
            m_SinYaw = length > 0.001f ? hmd[0][2] / length : 0.0f;
            m_CosYaw = length > 0.001f ? hmd[2][2] / length : 1.0f;
        }
        return Pose{{{m_CosYaw, 0, m_SinYaw, hmd[0][3] - 3.0f * m_SinYaw},
                     {0, 1, 0, hmd[1][3] - 0.25f},
                     {-m_SinYaw, 0, m_CosYaw, hmd[2][3] - 3.0f * m_CosYaw}}};
    }

private:
    bool m_Positioned = false;
    float m_SinYaw = 0.0f;
    float m_CosYaw = 1.0f;
};
