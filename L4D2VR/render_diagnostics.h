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
    HudPaintEntered,
    HudPaintInGame,
    HudPaintEligible,
    HudPaintNoRedirect,
    HudPushOutsidePaint,
    HudPushInPaint,
    HudRedirected,
    HudExplicitCapture,
    HudExplicitUnexpectedPush,
    HudExplicitContextUnavailable,
    HudOverlayShown,
    CrosshairShouldDrawTrue,
    CrosshairShouldDrawFalse,
    CrosshairTransformTrue,
    CrosshairTransformFalse,
    CrosshairHudDraw,
    CrosshairWorldLeftEye,
    CrosshairWorldRightEye,
    CrosshairWorldProjectionSkipped,
    CrosshairWorldUnknownIcon,
    CrosshairWorldOutsideStereo,
    CrosshairWorldPortalStatus,
    CrosshairWorldPortalStatusOutsideStereo,
    CrosshairCanvasInside,
    CrosshairCanvasOutsideX,
    CrosshairCanvasOutsideY,
    CrosshairCanvasOutsideBoth,
    CrosshairCanvasInvalid,
    LaserControlPoints,
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
