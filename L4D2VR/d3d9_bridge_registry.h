#pragma once

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace Portal2VRBridge {

// Entries are borrowed. The owning device must call ReleaseDevice for every
// public Release, which serializes the final release with AcquireSole.
template <typename Device, typename Bridge>
class Registry {
public:
    bool Register(Device* device, Bridge* bridge) {
        if (!device || !bridge)
            return false;
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        const auto found = std::find_if(m_entries.begin(), m_entries.end(),
            [device](const Entry& entry) { return entry.device == device; });
        if (found != m_entries.end())
            return false;
        m_entries.push_back({device, bridge});
        return true;
    }

    void Unregister(Device* device) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        UnregisterLocked(device);
    }

    bool AcquireSole(Device** device, Bridge** bridge) {
        if (device)
            *device = nullptr;
        if (bridge)
            *bridge = nullptr;
        if (!device || !bridge)
            return false;

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_entries.size() != 1)
            return false;

        Entry entry = m_entries.front();
        entry.device->AddRef();
        entry.bridge->AddRef();
        *device = entry.device;
        *bridge = entry.bridge;
        return true;
    }

    template <typename IsFinal, typename Release>
    auto ReleaseDevice(Device* device, IsFinal isFinal, Release release)
        -> decltype(release()) {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (isFinal())
            UnregisterLocked(device);
        return release();
    }

private:
    struct Entry {
        Device* device;
        Bridge* bridge;
    };

    void UnregisterLocked(Device* device) {
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
            [device](const Entry& entry) { return entry.device == device; }),
            m_entries.end());
    }

    std::recursive_mutex m_mutex;
    std::vector<Entry> m_entries;
};

} // namespace Portal2VRBridge
