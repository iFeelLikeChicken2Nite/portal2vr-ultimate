#pragma once

#include <array>
#include <cstddef>

enum class RenderDiagnosticEvent : std::size_t
{
    TrackingBypass,
    RenderTargetBypass,
    CursorBypass,
    StereoRendered,
    MenuSubmission,
    StereoSubmission,
    OverlayPlacementSucceeded,
    OverlayPlacementFailed,
    Count
};

class RenderDiagnosticGate
{
public:
    bool First(RenderDiagnosticEvent event)
    {
        const auto index = static_cast<std::size_t>(event);
        if (m_Seen[index])
            return false;
        m_Seen[index] = true;
        return true;
    }

private:
    std::array<bool, static_cast<std::size_t>(RenderDiagnosticEvent::Count)> m_Seen{};
};
