#pragma once
#include <cmath>

namespace ViewmodelAlignment {
struct Eligibility {
    bool published;
    bool enabled;
    bool ready;
    bool trackingValid;
    bool handValid;
    bool inGame;
    bool cursorVisible;
    bool Allowed() const {
        return published && enabled && ready && trackingValid && handValid &&
            inGame && !cursorVisible;
    }
};

inline float AspectOr(float overrideAspect, float nativeAspect) {
    return std::isfinite(overrideAspect) && overrideAspect > 0.0f ?
        overrideAspect : nativeAspect;
}

// Restore this thread's aspect override across nested portal views and exits.
class ProjectionScope {
public:
    ProjectionScope(float &slot, float aspect) : m_Slot(slot), m_Previous(slot) {
        m_Slot = aspect;
    }
    ~ProjectionScope() { m_Slot = m_Previous; }
    ProjectionScope(const ProjectionScope &) = delete;
    ProjectionScope &operator=(const ProjectionScope &) = delete;
private:
    float &m_Slot;
    float m_Previous;
};
}
