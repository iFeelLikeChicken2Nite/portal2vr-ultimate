#pragma once

#include <array>
#include <cstdint>
#include <optional>

// Low-cost, bounded diagnostics for Source's four portal-status layers.
// A DrawSelf call is a layer draw, not a status transition.
class ReticleTelemetry
{
public:
    enum class Eye { Left = 0, Right = 1 };
    enum class Icon { LeftInvalid = 0, LeftValid, RightInvalid, RightValid, Other };
    enum class Result { Drawn, ProjectedOutside, Failed };

    struct IconCounts
    {
        std::uint32_t zeroAlpha = 0;
        std::uint32_t partialAlpha = 0;
        std::uint32_t opaqueAlpha = 0;
    };

    struct EyeCounts
    {
        std::array<IconCounts, 5> icons{};
        std::uint32_t drawn = 0;
        std::uint32_t outside = 0;
        std::uint32_t failed = 0;
    };

    struct Window
    {
        std::array<EyeCounts, 2> eyes{};
    };

    std::optional<Window> Record(Eye eye, Icon icon, int alpha,
                                 Result result, std::uint64_t nowMilliseconds)
    {
        if (!m_Started) {
            m_Started = true;
            m_WindowStart = nowMilliseconds;
        }
        auto &counts = m_Current.eyes[static_cast<std::size_t>(eye)];
        if (result == Result::ProjectedOutside) {
            ++counts.outside;
        } else if (result == Result::Failed) {
            ++counts.failed;
        } else {
            ++counts.drawn;
            if (alpha >= 0 && alpha <= 255) {
                auto &iconCounts = counts.icons[static_cast<std::size_t>(icon)];
                if (alpha == 0)
                    ++iconCounts.zeroAlpha;
                else if (alpha == 255)
                    ++iconCounts.opaqueAlpha;
                else
                    ++iconCounts.partialAlpha;
            }
        }
        if (nowMilliseconds < m_WindowStart ||
            nowMilliseconds - m_WindowStart < 1000)
            return std::nullopt;
        m_Last = m_Current;
        m_Current = {};
        m_WindowStart = nowMilliseconds;
        return m_Last;
    }

    const Window &LastWindow() const { return m_Last; }
    const Window &CurrentWindow() const { return m_Current; }

private:
    bool m_Started = false;
    std::uint64_t m_WindowStart = 0;
    Window m_Current{};
    Window m_Last{};
};
