#pragma once

#include "openvr.h"
#include "shared_runtime_session.h"

namespace Portal2VROpenVR {
inline SharedRuntimeSession<vr::IVRSystem> g_Session;

class SessionLease
{
public:
    SessionLease() = default;
    SessionLease(const SessionLease &) = delete;
    SessionLease &operator=(const SessionLease &) = delete;
    ~SessionLease() { Reset(); }

    vr::IVRSystem *Acquire(vr::EVRInitError &error)
    {
        error = vr::VRInitError_None;
        if (!m_System) {
            m_System = g_Session.Acquire([&] {
                return vr::VR_Init(&error, vr::VRApplication_Scene);
            });
        }
        return m_System;
    }

    void Reset()
    {
        if (m_System) {
            g_Session.Release([] { vr::VR_Shutdown(); });
            m_System = nullptr;
        }
    }

private:
    vr::IVRSystem *m_System = nullptr;
};
} // namespace Portal2VROpenVR
