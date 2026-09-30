#pragma once

namespace HudCapture {

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

} // namespace HudCapture
