#pragma once

// Balance Source +commands on OpenVR transitions, including tracking loss.
class DigitalButtonState
{
public:
    const char* HeldCommand(bool active, bool changed, bool pressed,
                            const char* pressCommand, const char* releaseCommand)
    {
        if (!active) {
            if (!m_Pressed) return nullptr;
            m_Pressed = false;
            return releaseCommand;
        }
        if (!changed || pressed == m_Pressed) return nullptr;
        m_Pressed = pressed;
        return pressed ? pressCommand : releaseCommand;
    }

    static bool PressEdge(bool active, bool changed, bool pressed)
    {
        return active && changed && pressed;
    }

private:
    bool m_Pressed = false;
};
