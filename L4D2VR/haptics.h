#pragma once

#include <chrono>
#include <mutex>

namespace Haptics {

enum class Hand { Left, Right };

inline Hand OutputHand(bool leftHanded)
{
    return leftHanded ? Hand::Left : Hand::Right;
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

    bool Consume()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const bool pending = m_Pending;
        m_Pending = false;
        return pending;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pending = false;
    }

private:
    std::mutex m_Mutex;
    bool m_Pending = false;
    bool m_HasLast = false;
    std::chrono::steady_clock::time_point m_LastQueued{};
};

} // namespace Haptics
