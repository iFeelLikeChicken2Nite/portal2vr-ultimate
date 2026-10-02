#pragma once

#include "tracking_space.h"
#include "roomscale_motion.h"
#include "portal_orientation.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct ConfigSnapshot {
    bool verboseDiagnostics = false;
    TrackingSpace::TrackingMode trackingMode = TrackingSpace::TrackingMode::Seated;
    TrackingSpace::MovementDirection movementDirection = TrackingSpace::MovementDirection::Hmd;
    RoomscaleMotion::Mode roomscaleMode = RoomscaleMotion::Mode::Off;
    PortalOrientation::Mode portalOrientationMode = PortalOrientation::Mode::LegacyYaw;
    bool experimentalHudOverlay = false;
    bool experimentalWorldAimMarker = false;
    bool experimentalViewmodelAlignment = false;
    bool aimFromViewmodelMuzzle = false;
    bool experimentalPortalShotHaptics = false;
    float portalShotHapticAmplitude = 0.35f;
    float portalShotHapticDurationSeconds = 0.05f;
    float hudDistanceMeters = 1.3f;
    float hudWidthMeters = 1.4f;
    float hudVerticalOffsetMeters = -0.15f;
    float heightOffsetMeters = 0.0f;
    float controllerPitchDegrees = -30.0f;
    float turnSpeed = 0.15f;
    bool snapTurning = false;
    float snapTurnAngle = 45.0f;
    bool leftHanded = false;
    float vrScale = 43.2f;
    float ipdScale = 1.0f;
    bool sixDof = true;
    int aimMode = 2;
    uint32_t antiAliasing = 0;
    uint32_t renderWindow = 0;
    std::array<float, 3> viewmodelPosOffset{};
    std::array<float, 3> viewmodelAngOffset{};
};

struct ConfigParseResult {
    ConfigSnapshot value;
    std::vector<std::string> errors;
};

inline std::string TrimConfigValue(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

inline ConfigParseResult ParseConfig(std::istream &stream, const ConfigSnapshot &previous)
{
    ConfigParseResult result{previous, {}};
    std::unordered_map<std::string, std::string> entries;
    std::string line;
    while (std::getline(stream, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        entries[TrimConfigValue(line.substr(0, separator))] =
            TrimConfigValue(line.substr(separator + 1));
    }

    const auto readFloat = [&](const std::string &key, float &target, float min, float max) {
        const auto it = entries.find(key);
        if (it == entries.end()) return;
        std::istringstream input(it->second);
        float value = 0;
        const bool parsed = static_cast<bool>(input >> value);
        char extra = 0;
        const bool trailing = static_cast<bool>(input >> extra);
        if (!parsed || trailing || !std::isfinite(value) || value < min || value > max)
            result.errors.push_back(key + " is invalid; keeping previous value");
        else target = value;
    };
    const auto readInt = [&](const std::string &key, int &target, int min, int max) {
        const auto it = entries.find(key);
        if (it == entries.end()) return;
        std::istringstream input(it->second);
        int value = 0;
        const bool parsed = static_cast<bool>(input >> value);
        char extra = 0;
        const bool trailing = static_cast<bool>(input >> extra);
        if (!parsed || trailing || value < min || value > max)
            result.errors.push_back(key + " is invalid; keeping previous value");
        else target = value;
    };
    const auto readBool = [&](const std::string &key, bool &target) {
        const auto it = entries.find(key);
        if (it == entries.end()) return;
        if (it->second == "true") target = true;
        else if (it->second == "false") target = false;
        else result.errors.push_back(key + " is invalid; keeping previous value");
    };

    readFloat("TurnSpeed", result.value.turnSpeed, 0.01f, 2.0f);
    const auto trackingMode = entries.find("TrackingMode");
    if (trackingMode != entries.end()) {
        if (trackingMode->second == "Seated")
            result.value.trackingMode = TrackingSpace::TrackingMode::Seated;
        else if (trackingMode->second == "Standing")
            result.value.trackingMode = TrackingSpace::TrackingMode::Standing;
        else
            result.errors.push_back("TrackingMode is invalid; keeping previous value");
    }
    const auto movementDirection = entries.find("MovementDirection");
    if (movementDirection != entries.end()) {
        if (movementDirection->second == "HMD")
            result.value.movementDirection = TrackingSpace::MovementDirection::Hmd;
        else if (movementDirection->second == "LeftController")
            result.value.movementDirection = TrackingSpace::MovementDirection::LeftController;
        else if (movementDirection->second == "RightController")
            result.value.movementDirection = TrackingSpace::MovementDirection::RightController;
        else
            result.errors.push_back("MovementDirection is invalid; keeping previous value");
    }
    const auto roomscaleMode = entries.find("RoomscaleMode");
    if (roomscaleMode != entries.end()) {
        if (roomscaleMode->second == "Off")
            result.value.roomscaleMode = RoomscaleMotion::Mode::Off;
        else if (roomscaleMode->second == "Observe")
            result.value.roomscaleMode = RoomscaleMotion::Mode::Observe;
        else if (roomscaleMode->second == "ActiveExperimental")
            result.value.roomscaleMode = RoomscaleMotion::Mode::ActiveExperimental;
        else
            result.errors.push_back("RoomscaleMode is invalid; expected Off, Observe or ActiveExperimental");
    }
    const auto portalMode = entries.find("PortalOrientationMode");
    if (portalMode != entries.end()) {
        if (portalMode->second == "LegacyYaw")
            result.value.portalOrientationMode = PortalOrientation::Mode::LegacyYaw;
        else if (portalMode->second == "FullRotation")
            result.value.portalOrientationMode = PortalOrientation::Mode::FullRotation;
        else if (portalMode->second == "YawOnly")
            result.value.portalOrientationMode = PortalOrientation::Mode::YawOnly;
        else if (portalMode->second == "PreserveHorizon")
            result.value.portalOrientationMode = PortalOrientation::Mode::PreserveHorizon;
        else
            result.errors.push_back("PortalOrientationMode is invalid; keeping previous value");
    }
    readBool("VerboseDiagnostics", result.value.verboseDiagnostics);
    readBool("ExperimentalHUDOverlay", result.value.experimentalHudOverlay);
    readBool("ExperimentalWorldAimMarker", result.value.experimentalWorldAimMarker);
    readBool("AimFromViewmodelMuzzle", result.value.aimFromViewmodelMuzzle);
    readBool("ExperimentalViewmodelAlignment", result.value.experimentalViewmodelAlignment);
    readBool("ExperimentalPortalShotHaptics", result.value.experimentalPortalShotHaptics);
    readFloat("PortalShotHapticAmplitude", result.value.portalShotHapticAmplitude, 0.0f, 1.0f);
    readFloat("PortalShotHapticDurationSeconds", result.value.portalShotHapticDurationSeconds,
              0.01f, 0.15f);
    readFloat("HUDDistanceMeters", result.value.hudDistanceMeters, 0.6f, 3.0f);
    readFloat("HUDWidthMeters", result.value.hudWidthMeters, 0.5f, 2.5f);
    readFloat("HUDVerticalOffsetMeters", result.value.hudVerticalOffsetMeters, -0.6f, 0.6f);
    if (entries.find("SeatedMode") != entries.end())
        result.errors.push_back("SeatedMode is unsupported; use TrackingMode");
    readFloat("HeightOffsetMeters", result.value.heightOffsetMeters, -0.5f, 0.5f);
    readFloat("ControllerPitchDegrees", result.value.controllerPitchDegrees, -60.0f, 60.0f);
    readBool("SnapTurning", result.value.snapTurning);
    readFloat("SnapTurnAngle", result.value.snapTurnAngle, 1.0f, 180.0f);
    readBool("LeftHanded", result.value.leftHanded);
    readFloat("VRScale", result.value.vrScale, 1.0f, 200.0f);
    readFloat("IPDScale", result.value.ipdScale, 0.5f, 1.5f);
    readBool("6DOF", result.value.sixDof);
    readInt("AimMode", result.value.aimMode, 0, 2);
    int aa = static_cast<int>(result.value.antiAliasing);
    readInt("AntiAliasing", aa, 0, 8);
    if (aa == 0 || aa == 2 || aa == 4 || aa == 8) result.value.antiAliasing = aa;
    else result.errors.push_back("AntiAliasing is invalid; keeping previous value");
    int window = static_cast<int>(result.value.renderWindow);
    readInt("RenderWindow", window, 0, 1);
    result.value.renderWindow = window;
    constexpr char axes[] = "XYZ";
    for (int i = 0; i < 3; ++i) {
        readFloat(std::string("ViewmodelPosCustomOffset") + axes[i], result.value.viewmodelPosOffset[i], -100.f, 100.f);
        readFloat(std::string("ViewmodelAngCustomOffset") + axes[i], result.value.viewmodelAngOffset[i], -180.f, 180.f);
    }
    return result;
}
