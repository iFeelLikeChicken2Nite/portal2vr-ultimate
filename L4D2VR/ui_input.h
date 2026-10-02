#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace UiInput {

enum class MouseTransition { None, Press, Release };

class MenuPointerState {
public:
    MouseTransition Press()
    {
        if (m_Down || m_ReleasePending)
            return MouseTransition::None;
        m_Down = true;
        return MouseTransition::Press;
    }

    MouseTransition Release()
    {
        if (m_Down) {
            m_Down = false;
            m_ReleasePending = true;
        }
        return PendingRelease();
    }

    MouseTransition LoseFocus() { return Release(); }

    MouseTransition PendingRelease() const
    {
        return m_ReleasePending ? MouseTransition::Release : MouseTransition::None;
    }

    void ConfirmSent(MouseTransition transition, bool succeeded)
    {
        if (transition == MouseTransition::Press && !succeeded)
            m_Down = false;
        else if (transition == MouseTransition::Release && succeeded)
            m_ReleasePending = false;
    }

private:
    bool m_Down = false;
    bool m_ReleasePending = false;
};

struct PixelPoint {
    int x;
    int y;
};

inline std::optional<PixelPoint> MapMenuPointer(float overlayX, float overlayY,
                                                 int renderWidth, int renderHeight,
                                                 int windowWidth, int windowHeight,
                                                 bool inGame)
{
    if (!std::isfinite(overlayX) || !std::isfinite(overlayY) ||
        renderWidth <= 0 || renderHeight <= 0 || windowWidth <= 0 || windowHeight <= 0)
        return std::nullopt;
    // The in-game menu occupies the window-sized crop of a render-sized backbuffer.
    const float x = inGame ? overlayX : overlayX * windowWidth / renderWidth;
    const float y = inGame ? renderHeight - overlayY :
        (renderHeight - overlayY) * windowHeight / renderHeight;
    return PixelPoint{
        static_cast<int>(std::clamp(x, 0.0f, static_cast<float>(windowWidth - 1))),
        static_cast<int>(std::clamp(y, 0.0f, static_cast<float>(windowHeight - 1)))};
}

} // namespace UiInput
