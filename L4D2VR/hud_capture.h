#pragma once

namespace HudCapture {

inline bool ShouldRedirectTarget(bool enabled, bool hooksReady, bool texturesReady,
                                 bool inVguiPaint, bool cursorVisible, bool alreadyRedirected)
{
    return enabled && hooksReady && texturesReady && inVguiPaint &&
        !cursorVisible && !alreadyRedirected;
}

} // namespace HudCapture
