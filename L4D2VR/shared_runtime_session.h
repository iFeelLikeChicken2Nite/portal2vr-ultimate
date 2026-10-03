#pragma once

#include <mutex>

// Serialize init/last-owner shutdown, including acquisition racing shutdown.
// No platform runtime is loaded until the first successful lease is acquired.
template <typename System>
class SharedRuntimeSession
{
public:
    template <typename Initialize>
    System *Acquire(Initialize initialize)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_System) m_System = initialize();
        if (m_System) ++m_References;
        return m_System;
    }

    template <typename Shutdown>
    void Release(Shutdown shutdown)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_References && --m_References == 0) {
            shutdown();
            m_System = nullptr;
        }
    }

private:
    std::mutex m_Mutex;
    System *m_System = nullptr;
    unsigned m_References = 0;
};
