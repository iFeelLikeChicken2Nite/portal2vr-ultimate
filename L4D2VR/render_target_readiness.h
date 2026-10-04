#pragma once

#include <cstdint>
#include <limits>

struct SharedRenderTargetReadiness
{
    bool texture = false;
    bool surface = false;
    bool shared = false;

    bool Ready() const { return texture && surface && shared; }
};

struct RenderTargetReadiness
{
    SharedRenderTargetReadiness left;
    SharedRenderTargetReadiness right;
    SharedRenderTargetReadiness blank;

    bool Ready() const { return left.Ready() && right.Ready() && blank.Ready(); }
};

// Three attempts per working-device lifecycle, separated by at least a second.
// A missing/lost bridge does not consume attempts or trigger allocations.
class RenderTargetRetryState
{
public:
    bool CanAttempt(uint64_t nowMilliseconds, bool deviceReady) const
    {
        return deviceReady && m_Failures < 3 && nowMilliseconds >= m_NextAttempt;
    }
    void RecordFailure(uint64_t nowMilliseconds)
    {
        if (m_Failures < 3) ++m_Failures;
        constexpr uint64_t delay = 1000;
        const auto maximum = (std::numeric_limits<uint64_t>::max)();
        m_NextAttempt = nowMilliseconds > maximum - delay ? maximum : nowMilliseconds + delay;
    }
    void RecordSuccess() { ResetForDevice(); }
    void ResetForDevice() { m_Failures = 0; m_NextAttempt = 0; }
    bool Exhausted() const { return m_Failures >= 3; }

private:
    unsigned m_Failures = 0;
    uint64_t m_NextAttempt = 0;
};
