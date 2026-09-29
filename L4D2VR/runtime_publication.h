#pragma once
#include <atomic>

namespace Portal2VRRuntime
{
    template <typename GameType>
    bool IsPublished(const std::atomic<GameType*>& published, const GameType* candidate)
    {
        return candidate && published.load(std::memory_order_acquire) == candidate;
    }

    template <typename GameType>
    bool PublishInitialized(std::atomic<GameType*>& published, GameType* candidate)
    {
        if (!candidate || !candidate->m_Initialized)
            return false;
        published.store(candidate, std::memory_order_release);
        return true;
    }
}
