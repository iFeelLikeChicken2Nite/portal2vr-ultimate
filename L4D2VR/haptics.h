#pragma once

#include <chrono>
#include <mutex>

namespace Haptics {

enum class Hand { Left, Right };

inline Hand OutputHand(bool leftHanded)
{
    return leftHanded ? Hand::Left : Hand::Right;
}

inline bool CanDeliverShot(bool enabled, bool actionsReady, bool gameplay,
                           bool trackingValid, bool controllerValid, bool outputAvailable)
{
    return enabled && actionsReady && gameplay && trackingValid &&
        controllerValid && outputAvailable;
}

inline bool IsLocalShot(int localIndex, int ownerIndex)
{
    return localIndex > 0 && ownerIndex == localIndex;
}

// The server-side fire hook can run independently of the render/update thread.
// Keep only one notification, and never call the VR runtime from that hook.
class ShotGate {
public:
    bool Queue(std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Pending || (m_HasLast && now - m_LastQueued < std::chrono::milliseconds(100)))
            return false;
        m_LastQueued = now;
        m_HasLast = true;
        m_Pending = true;
        return true;
    }

    bool Consume(std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const bool pending = m_Pending;
        m_Pending = false;
        if (!pending || (m_HasAttempted &&
            now - m_LastAttempted < std::chrono::milliseconds(100)))
            return false;
        m_LastAttempted = now;
        m_HasAttempted = true;
        return true;
    }

    bool Consume() { return Consume(std::chrono::steady_clock::now()); }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pending = false;
        m_HasLast = false;
    }

private:
    std::mutex m_Mutex;
    bool m_Pending = false;
    bool m_HasLast = false;
    std::chrono::steady_clock::time_point m_LastQueued{};
    bool m_HasAttempted = false;
    std::chrono::steady_clock::time_point m_LastAttempted{};
};

} // namespace Haptics
