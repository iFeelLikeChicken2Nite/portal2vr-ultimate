#pragma once

#include <optional>

namespace HudCapture {

struct TextureCrop
{
    float uMax;
    float vMax;
};

inline std::optional<TextureCrop> WindowTextureCrop(int textureWidth, int textureHeight,
                                                    int windowWidth, int windowHeight)
{
    if (textureWidth <= 0 || textureHeight <= 0 || windowWidth <= 0 || windowHeight <= 0)
        return std::nullopt;
    const int visibleWidth = windowWidth < textureWidth ? windowWidth : textureWidth;
    const int visibleHeight = windowHeight < textureHeight ? windowHeight : textureHeight;
    return TextureCrop{static_cast<float>(visibleWidth) / textureWidth,
                       static_cast<float>(visibleHeight) / textureHeight};
}

class RouteState
{
public:
    bool AllowsRedirect() const { return m_Route == Route::Redirect; }

    bool ObserveRedirectPaint(bool sawPush, bool redirected)
    {
        if (m_Route != Route::Redirect || sawPush || redirected)
            return false;
        m_Route = Route::Explicit;
        return true;
    }

    bool ShouldCaptureExplicitly(bool eligible, bool uiPass, bool alreadyRendered) const
    {
        return m_Route == Route::Explicit && eligible && uiPass && !alreadyRendered;
    }

    bool ObserveExplicitPaint(bool unexpectedPush)
    {
        if (m_Route != Route::Explicit || !unexpectedPush)
            return false;
        m_Route = Route::Disabled;
        return true;
    }

private:
    enum class Route { Redirect, Explicit, Disabled };
    Route m_Route = Route::Redirect;
};

inline bool CanCapturePaint(bool enabled, bool hooksReady, bool texturesReady,
                            bool targetReady, bool stereoFrameReady, bool inGame,
                            bool cursorVisible)
{
    return enabled && hooksReady && texturesReady && targetReady &&
        stereoFrameReady && inGame && !cursorVisible;
}

inline bool ShouldRedirectTarget(bool enabled, bool hooksReady, bool texturesReady,
                                 bool inVguiPaint, bool cursorVisible, bool alreadyRedirected)
{
    return enabled && hooksReady && texturesReady && inVguiPaint &&
        !cursorVisible && !alreadyRedirected;
}

inline bool ShouldForwardPaintPop(bool explicitCaptureActive, unsigned nestedDepth)
{
    return !explicitCaptureActive || nestedDepth > 0;
}

// The explicit HUD target sits below any Source targets pushed during paint.
// Restore those targets first so the caller's final pop removes our target.
template <typename PopTarget>
void UnwindNestedTargets(unsigned &nestedDepth, PopTarget popTarget)
{
    while (nestedDepth > 0) {
        popTarget();
        --nestedDepth;
    }
}

} // namespace HudCapture
