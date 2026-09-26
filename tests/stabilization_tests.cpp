#include "../L4D2VR/digital_input.h"
#include "../L4D2VR/tracked_device.h"
#include "../L4D2VR/sigscanner.h"
#include "../L4D2VR/config.h"
#include "../L4D2VR/tracking_space.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

static int failures = 0;

static void expectCommand(const char* actual, const char* expected, const char* caseName)
{
    if ((actual == nullptr) != (expected == nullptr) ||
        (actual && std::strcmp(actual, expected) != 0)) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

static void expect(bool actual, bool expected, const char* caseName)
{
    if (actual != expected) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

static void expectNear(float actual, float expected, const char* caseName)
{
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.0001f) {
        std::cerr << caseName << " failed: " << actual << " != " << expected << "\n";
        ++failures;
    }
}

static void expectVectorNear(const Vector &actual, const Vector &expected, const char* caseName)
{
    expectNear(actual.x, expected.x, caseName);
    expectNear(actual.y, expected.y, caseName);
    expectNear(actual.z, expected.z, caseName);
}

int main()
{
    const Vector openVrPosition{1.0f, 2.0f, 3.0f};
    const Vector sourceMeters = TrackingSpace::OpenVrToSourceMeters(openVrPosition);
    expectVectorNear(sourceMeters, {-3.0f, -1.0f, 2.0f}, "OpenVR to Source axes");
    expectVectorNear(TrackingSpace::SourceToOpenVrMeters(sourceMeters), openVrPosition,
                     "Source to OpenVR inverse axes");
    expectVectorNear(TrackingSpace::RotateYawDegrees({1.0f, 0.0f, 2.0f}, 90.0f),
                     {0.0f, 1.0f, 2.0f}, "yaw 90 degrees");
    expectVectorNear(TrackingSpace::RotateYawDegrees({1.0f, 0.0f, 2.0f}, 180.0f),
                     {-1.0f, 0.0f, 2.0f}, "yaw 180 degrees");

    TrackingSpace::PlayspaceState seated;
    seated.scale = 50.0f;
    seated.yawDegrees = 90.0f;
    seated.Recenter({2.0f, 3.0f, 1.0f});
    expectVectorNear(seated.HmdOffsetUnits({2.0f, 3.0f, 1.0f}, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "seated recenter at nonzero yaw");
    const Vector seatedHmd{3.0f, 3.0f, 1.2f};
    expectVectorNear(seated.HmdOffsetUnits(seatedHmd, 70.0f),
                     {0.0f, 50.0f, 10.0f}, "seated HMD displacement at scale 50");
    expectVectorNear(seated.ControllerOffsetUnits({3.0f, 4.0f, 1.2f}, seatedHmd, 70.0f),
                     {-50.0f, 50.0f, 10.0f}, "controller uses HMD playspace transform");

    TrackingSpace::PlayspaceState standing;
    standing.mode = TrackingSpace::TrackingMode::Standing;
    standing.scale = 50.0f;
    standing.heightOffsetMeters = 0.1f;
    standing.Recenter({2.0f, 3.0f, 1.7f});
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.7f}, 70.0f),
                     {0.0f, 0.0f, 20.0f}, "standing floor height uses measured eye height");
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.8f}, 70.0f),
                     {0.0f, 0.0f, 25.0f}, "standing HMD height change");

    TrackingSpace::PlayspaceState turning;
    turning.scale = 50.0f;
    const Vector turnHmd{0.4f, 0.2f, 1.2f};
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "turn baseline head offset");
    turning.TurnAboutHmd(90.0f, turnHmd, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "first turn holds head pivot");
    expectVectorNear(turning.ControllerOffsetUnits({0.6f, 0.2f, 1.2f}, turnHmd, 70.0f),
                     {20.0f, 20.0f, 60.0f}, "controller rotates around head");
    turning.TurnAboutHmd(90.0f, turnHmd, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {20.0f, 10.0f, 60.0f}, "repeated turns hold head pivot");
    turning.Recenter(turnHmd);
    expectVectorNear(turning.HmdOffsetUnits(turnHmd, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "recenter clears turn translation");

    DigitalButtonState attack;
    expectCommand(attack.HeldCommand(true, false, false, "+attack", "-attack"), nullptr, "released to released");
    expectCommand(attack.HeldCommand(true, true, true, "+attack", "-attack"), "+attack", "released to pressed");
    expectCommand(attack.HeldCommand(true, false, true, "+attack", "-attack"), nullptr, "pressed to pressed");
    expectCommand(attack.HeldCommand(true, true, false, "+attack", "-attack"), "-attack", "pressed to released");
    expectCommand(attack.HeldCommand(true, false, false, "+attack", "-attack"), nullptr, "released stays released");
    expectCommand(attack.HeldCommand(true, true, true, "+attack", "-attack"), "+attack", "press before tracking loss");
    expectCommand(attack.HeldCommand(false, false, false, "+attack", "-attack"), "-attack", "inactive action releases held command");
    expectCommand(attack.HeldCommand(false, false, false, "+attack", "-attack"), nullptr, "inactive action does not repeat release");

    expect(DigitalButtonState::PressEdge(true, false, false), false, "one shot released");
    expect(DigitalButtonState::PressEdge(true, true, true), true, "one shot press");
    expect(DigitalButtonState::PressEdge(true, false, true), false, "one shot hold");
    expect(DigitalButtonState::PressEdge(true, true, false), false, "one shot release");
    expect(DigitalButtonState::PressEdge(false, true, true), false, "inactive one shot");

    expect(IsUsableTrackedDeviceIndex(0, 64, 0xffffffffu), true, "HMD index is in range");
    expect(IsUsableTrackedDeviceIndex(63, 64, 0xffffffffu), true, "last tracked index is in range");
    expect(IsUsableTrackedDeviceIndex(64, 64, 0xffffffffu), false, "out of range tracked index");
    expect(IsUsableTrackedDeviceIndex(0xffffffffu, 64, 0xffffffffu), false, "invalid controller role index");

    const uint8_t image[] = { 0x11, 0x22, 0x33, 0x44 };
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x33, 0x44 }) == 2, true, "signature at image end");
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x33, 0x55 }) == -1, true, "signature mismatch");
    expect(SigScanner::FindPattern(image, sizeof(image), { 0x44, 0x55 }) == -1, true, "no scan beyond image");

    ConfigSnapshot previous;
    previous.turnSpeed = 0.3f;
    previous.antiAliasing = 4;
    std::istringstream malformed("TurnSpeed=bogus\nVRScale=0\nIPDScale=nan\nAimMode=9\nAntiAliasing=3\nSnapTurning=maybe\nLeftHanded=true\n");
    const auto parsed = ParseConfig(malformed, previous);
    expect(parsed.value.turnSpeed == 0.3f, true, "bad turn speed preserves previous");
    expect(parsed.value.vrScale == 43.2f, true, "out of range scale preserves previous");
    expect(parsed.value.ipdScale == 1.0f, true, "nonfinite IPD preserves previous");
    expect(parsed.value.aimMode == 2, true, "bad aim mode preserves previous");
    expect(parsed.value.antiAliasing == 4, true, "unsupported AA preserves previous");
    expect(parsed.value.snapTurning == false, true, "bad boolean preserves previous");
    expect(parsed.value.leftHanded == true, true, "other valid field accepted");
    expect(parsed.errors.size() == 6, true, "all malformed config fields reported");
    std::istringstream good("TurnSpeed = 0.5 # comment\nSnapTurning=true\nAntiAliasing=8\nViewmodelPosCustomOffsetX=-2.5\n");
    const auto valid = ParseConfig(good, previous);
    expect(valid.errors.empty(), true, "valid config has no errors");
    expect(valid.value.turnSpeed == 0.5f, true, "trimmed float accepted");
    expect(valid.value.antiAliasing == 8, true, "valid AA accepted");
    expect(valid.value.viewmodelPosOffset[0] == -2.5f, true, "viewmodel offset accepted");
    std::istringstream trailing("TurnSpeed=0.5junk\nRenderWindow=2\n");
    const auto rejected = ParseConfig(trailing, previous);
    expect(rejected.value.turnSpeed == previous.turnSpeed, true, "trailing numeric garbage rejected");
    expect(rejected.value.renderWindow == previous.renderWindow, true, "invalid render toggle rejected");

    ConfigSnapshot m2Defaults;
    expect(m2Defaults.trackingMode == TrackingSpace::TrackingMode::Seated, true,
           "legacy config defaults to seated tracking");
    expect(m2Defaults.movementDirection == TrackingSpace::MovementDirection::Hmd, true,
           "legacy config defaults to HMD locomotion");
    expectNear(m2Defaults.heightOffsetMeters, 0.0f, "default height offset");
    expectNear(m2Defaults.controllerPitchDegrees, -30.0f, "default controller pitch");
    std::istringstream m2Options("TrackingMode=Standing\nMovementDirection=LeftController\n"
                                 "HeightOffsetMeters=0.25\nControllerPitchDegrees=15\n");
    const auto m2Valid = ParseConfig(m2Options, m2Defaults);
    expect(m2Valid.errors.empty(), true, "valid M2 config has no errors");
    expect(m2Valid.value.trackingMode == TrackingSpace::TrackingMode::Standing, true,
           "standing mode parsed");
    expect(m2Valid.value.movementDirection == TrackingSpace::MovementDirection::LeftController,
           true, "left controller movement parsed");
    expectNear(m2Valid.value.heightOffsetMeters, 0.25f, "height offset parsed");
    expectNear(m2Valid.value.controllerPitchDegrees, 15.0f, "controller pitch parsed");
    std::istringstream m2Right("MovementDirection=RightController\nHeightOffsetMeters=-0.5\n"
                               "ControllerPitchDegrees=-60\n");
    const auto m2Boundary = ParseConfig(m2Right, m2Defaults);
    expect(m2Boundary.errors.empty(), true, "M2 lower bounds valid");
    expect(m2Boundary.value.movementDirection == TrackingSpace::MovementDirection::RightController,
           true, "right controller movement parsed");
    std::istringstream m2Bad("TrackingMode=Floor\nMovementDirection=Neither\n"
                             "HeightOffsetMeters=nan\nControllerPitchDegrees=61\nSeatedMode=true\n");
    const auto m2Rejected = ParseConfig(m2Bad, m2Valid.value);
    expect(m2Rejected.value.trackingMode == m2Valid.value.trackingMode, true,
           "invalid tracking mode retains previous");
    expect(m2Rejected.value.movementDirection == m2Valid.value.movementDirection, true,
           "invalid movement direction retains previous");
    expectNear(m2Rejected.value.heightOffsetMeters, 0.25f,
               "invalid height offset retains previous");
    expectNear(m2Rejected.value.controllerPitchDegrees, 15.0f,
               "invalid controller pitch retains previous");
    expect(m2Rejected.errors.size() == 5, true, "invalid M2 entries and legacy key reported");

    if (failures) return 1;
    std::cout << "stabilization tests passed\n";
    return 0;
}
