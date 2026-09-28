#pragma once

class MenuOverlayPlacement
{
public:
    bool ShouldAttempt(bool visible, bool hmdPoseValid) const
    {
        return hmdPoseValid && (!visible || !m_Positioned);
    }

    void RecordResult(bool succeeded)
    {
        m_Positioned = succeeded;
    }

private:
    bool m_Positioned = false;
};
