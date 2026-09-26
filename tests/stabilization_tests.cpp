#include "../L4D2VR/digital_input.h"
#include "../L4D2VR/tracked_device.h"
#include "../L4D2VR/sigscanner.h"
#include "../L4D2VR/config.h"
#include "../L4D2VR/tracking_space.h"
#include "../L4D2VR/roomscale_motion.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
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
    RoomscaleMotion::StepAccumulator roomscale;
    roomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    expect(roomscale.Consume(100).has_value(), false, "first HMD sample establishes baseline");
    roomscale.Observe(true, {0.1f, 0.0f, 1.7f}, 2, 0.0f, 43.2f);
    const auto roomscaleStep = roomscale.Consume(101);
    expect(roomscaleStep.has_value(), true, "small fresh HMD step becomes one move intent");
    if (roomscaleStep)
        expectVectorNear(*roomscaleStep, {4.32f, 0.0f, 0.0f}, "physical step is horizontal Source units");
    expect(roomscale.Consume(101).has_value(), false, "same command does not repeat physical step");
    expect(roomscale.Consume(102).has_value(), false, "new command without pose does not repeat step");

    RoomscaleMotion::StepAccumulator recoveredRoomscale;
    recoveredRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    recoveredRoomscale.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(recoveredRoomscale.Observe(false, {}, 3, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::TrackingLost, true, "tracking loss is reported once");
    expect(recoveredRoomscale.Consume(103).has_value(), false, "tracking loss drops unconsumed step");
    expect(recoveredRoomscale.Observe(false, {}, 4, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::NoStep, true, "repeated invalid pose is quiet");
    expect(recoveredRoomscale.Observe(true, {2.0f, 0.0f, 1.6f}, 5, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::TrackingRecovered, true, "recovery rebases without teleporting");
    expect(recoveredRoomscale.Consume(104).has_value(), false, "recovery sample has no movement");

    RoomscaleMotion::StepAccumulator discontinuousRoomscale;
    discontinuousRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    discontinuousRoomscale.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(discontinuousRoomscale.Observe(true, {1.1f, 0.0f, 1.6f}, 3, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::Discontinuity, true, "large tracking jump is rejected");
    expect(discontinuousRoomscale.Consume(1).has_value(), false, "jump clears earlier queued step");
    discontinuousRoomscale.Observe(true, {1.2f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    const auto postJumpStep = discontinuousRoomscale.Consume(2);
    expect(postJumpStep.has_value(), true, "fresh step after discontinuity is accepted");
    if (postJumpStep)
        expectVectorNear(*postJumpStep, {4.32f, 0.0f, 0.0f}, "post-jump baseline is fresh");

    RoomscaleMotion::StepAccumulator changedMapping;
    changedMapping.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    changedMapping.Observe(true, {0.1f, 0.0f, 1.6f}, 2, 0.0f, 43.2f);
    expect(changedMapping.Observe(true, {0.1f, 0.0f, 1.6f}, 3, 90.0f, 43.2f) ==
           RoomscaleMotion::Observation::MappingChanged, true, "artificial turn rebases pending movement");
    expect(changedMapping.Consume(1).has_value(), false, "turn is not a physical step");
    changedMapping.Observe(true, {0.1f, 0.1f, 1.6f}, 4, 90.0f, 43.2f);
    const auto turnedStep = changedMapping.Consume(2);
    expect(turnedStep.has_value(), true, "physical step after turn remains available");
    if (turnedStep)
        expectVectorNear(*turnedStep, {-4.32f, 0.0f, 0.0f}, "post-turn step uses new mapping");
    changedMapping.Reset();
    changedMapping.Observe(true, {9.0f, 9.0f, 1.6f}, 5, 90.0f, 43.2f);
    expect(changedMapping.Consume(3).has_value(), false, "recenter never generates catch-up step");

    RoomscaleMotion::StepAccumulator invalidRoomscale;
    invalidRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    expect(invalidRoomscale.Observe(true, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.6f},
                                    2, 0.0f, 43.2f) ==
           RoomscaleMotion::Observation::InvalidSample, true, "non-finite pose is rejected");
    expect(invalidRoomscale.Consume(1).has_value(), false, "invalid pose has no movement");

    RoomscaleMotion::StepAccumulator accumulatedRoomscale;
    accumulatedRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 1, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.05f, 0.0f, 1.65f}, 2, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.1f, 0.2f, 1.7f}, 3, 0.0f, 43.2f);
    accumulatedRoomscale.Observe(true, {0.2f, 0.2f, 1.7f}, 3, 0.0f, 43.2f);
    expect(accumulatedRoomscale.Consume(0).has_value(), false, "zero command does not consume physical step");
    const auto accumulatedStep = accumulatedRoomscale.Consume(1);
    expect(accumulatedStep.has_value(), true, "pose samples accumulate before command");
    if (accumulatedStep)
        expectVectorNear(*accumulatedStep, {4.32f, 8.64f, 0.0f},
                         "stale pose sequence ignored and vertical motion excluded");
    accumulatedRoomscale.Observe(true, {0.0f, 0.0f, 1.6f}, 4, 0.0f, 43.2f);
    const auto returnStep = accumulatedRoomscale.Consume(2);
    expect(returnStep.has_value(), true, "return walk produces opposite physical intent");
    if (returnStep)
        expectVectorNear(*returnStep, {-4.32f, -8.64f, 0.0f},
                         "return walk cancels open-space intent mathematically");

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
    const auto leftOnly = seated.ControllerRelativeOffsetUnits(true,
        {3.0f, 4.0f, 1.2f}, seatedHmd, 70.0f);
    const auto rightLost = seated.ControllerRelativeOffsetUnits(false,
        {4.0f, 3.0f, 1.2f}, seatedHmd, 70.0f);
    expect(leftOnly.has_value(), true, "left position valid without right pose");
    if (leftOnly) expectVectorNear(*leftOnly, {-50.0f, 0.0f, 0.0f}, "left relative playspace offset");
    expect(rightLost.has_value(), false, "lost right position invalid independently");
    const auto rightReconnected = seated.ControllerRelativeOffsetUnits(true,
        {4.0f, 3.0f, 1.2f}, seatedHmd, 70.0f);
    expect(rightReconnected.has_value(), true, "right position recovers from fresh pose");
    if (rightReconnected) expectVectorNear(*rightReconnected,
        {0.0f, 50.0f, 0.0f}, "right relative offset after reconnect");

    TrackingSpace::PlayspaceState standing;
    standing.mode = TrackingSpace::TrackingMode::Standing;
    standing.scale = 50.0f;
    standing.heightOffsetMeters = 0.1f;
    standing.Recenter({2.0f, 3.0f, 1.7f});
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.7f}, 70.0f),
                     {0.0f, 0.0f, 20.0f}, "standing floor height uses measured eye height");
    expectVectorNear(standing.HmdOffsetUnits({2.0f, 3.0f, 1.8f}, 70.0f),
                     {0.0f, 0.0f, 25.0f}, "standing HMD height change");
    const auto measuredEyeHeight = TrackingSpace::EyeHeightUnits(72.0f, 5.0f);
    expect(measuredEyeHeight.has_value(), true, "finite Source eye height available");
    if (measuredEyeHeight) expectNear(*measuredEyeHeight, 67.0f, "Source eye height difference");
    expect(TrackingSpace::EyeHeightUnits(std::numeric_limits<float>::quiet_NaN(), 5.0f).has_value(),
           false, "NaN eye position rejected");
    expect(TrackingSpace::EyeHeightUnits(72.0f, std::numeric_limits<float>::infinity()).has_value(),
           false, "infinite player origin rejected");

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
    const Vector lastRenderedOffset{12.0f, -8.0f, 5.0f};
    turning.PreserveOffsetOnRecovery({0.8f, -0.3f, 1.1f}, lastRenderedOffset, 70.0f);
    expectVectorNear(turning.HmdOffsetUnits({0.8f, -0.3f, 1.1f}, 70.0f),
                     lastRenderedOffset, "tracking recovery holds last rendered offset");
    turning.Recenter({0.8f, -0.3f, 1.1f});
    expectVectorNear(turning.HmdOffsetUnits({0.8f, -0.3f, 1.1f}, 70.0f),
                     {0.0f, 0.0f, 0.0f}, "recenter clears recovery compensation");
    expectNear(standing.centerMeters.z, 0.0f, "standing recenter keeps floor origin");

    const TrackingSpace::DeviceDirection hmdDirection{true, {1.0f, 0.0f, 0.0f}};
    const TrackingSpace::DeviceDirection leftDirection{true, {0.0f, 1.0f, 0.5f}};
    const TrackingSpace::DeviceDirection rightDirection{true, {-1.0f, 0.0f, 0.0f}};
    const TrackingSpace::DeviceDirection lostDirection{false, {0.0f, -1.0f, 0.0f}};
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::Hmd,
                     hmdDirection, leftDirection, rightDirection), {1.0f, 0.0f, 0.0f},
                     "default HMD movement direction");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, leftDirection, rightDirection), {0.0f, 1.0f, 0.0f},
                     "left movement ignores pitch");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, leftDirection, rightDirection), {-1.0f, 0.0f, 0.0f},
                     "right controller movement direction");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, lostDirection, rightDirection), {-1.0f, 0.0f, 0.0f},
                     "right direction does not require left controller");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, lostDirection, rightDirection), {1.0f, 0.0f, 0.0f},
                     "lost left controller falls back to HMD");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::RightController,
                     hmdDirection, leftDirection, {true, {0.001f, 0.0f, 1.0f}}),
                     {1.0f, 0.0f, 0.0f}, "near vertical controller falls back");
    expectVectorNear(TrackingSpace::SelectMovementForward(TrackingSpace::MovementDirection::LeftController,
                     hmdDirection, leftDirection, lostDirection), {0.0f, 1.0f, 0.0f},
                     "left controller reconnect uses fresh pose");
    const auto defaultAxes = TrackingSpace::RebaseAnalogToView(0.25f, 0.8f,
                                                               hmdDirection.forward, hmdDirection.forward);
    expectNear(defaultAxes.forward, 0.8f, "HMD forward magnitude unchanged");
    expectNear(defaultAxes.side, 0.25f, "HMD side magnitude unchanged");
    const auto leftAxes = TrackingSpace::RebaseAnalogToView(0.25f, 0.8f,
                                                             leftDirection.forward, hmdDirection.forward);
    expectNear(leftAxes.forward, 0.25f, "left facing stick side becomes forward");
    expectNear(leftAxes.side, -0.8f, "left facing stick forward becomes side");

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
    expect(m2Defaults.roomscaleMode == RoomscaleMotion::Mode::Off, true,
           "roomscale observation defaults off");
    std::istringstream roomscaleObserve("RoomscaleMode=Observe\n");
    const auto observedConfig = ParseConfig(roomscaleObserve, m2Defaults);
    expect(observedConfig.errors.empty(), true, "roomscale observe config is valid");
    expect(observedConfig.value.roomscaleMode == RoomscaleMotion::Mode::Observe, true,
           "roomscale observe config selects no-motion diagnostics");
    std::istringstream roomscaleBad("RoomscaleMode=Active\n");
    const auto badRoomscaleConfig = ParseConfig(roomscaleBad, observedConfig.value);
    expect(badRoomscaleConfig.value.roomscaleMode == RoomscaleMotion::Mode::Observe, true,
           "invalid roomscale config retains previous mode");
    expect(badRoomscaleConfig.errors.size() == 1, true,
           "unsupported roomscale activation is diagnosed");
    RoomscaleMotion::Observer diagnostic;
    diagnostic.OnPose(true, {0.0f, 0.0f, 1.6f}, 0.0f, 43.2f);
    diagnostic.OnPose(true, {0.1f, 0.0f, 1.6f}, 0.0f, 43.2f);
    expect(diagnostic.OnCommand(1).has_value(), false, "off roomscale mode ignores poses and commands");
    expect(diagnostic.SetMode(RoomscaleMotion::Mode::Observe), true, "observe mode transition reported");
    diagnostic.OnPose(true, {5.0f, 0.0f, 1.6f}, 0.0f, 43.2f);
    diagnostic.OnPose(true, {5.1f, 0.0f, 1.6f}, 0.0f, 43.2f);
    const auto observedStep = diagnostic.OnCommand(2);
    expect(observedStep.has_value(), true, "observe mode computes diagnostic intent");
    if (observedStep)
        expectVectorNear(*observedStep, {4.32f, 0.0f, 0.0f}, "observe mode reports unmoved intent");
    const auto observedSummary = diagnostic.TakeSummary();
    expect(observedSummary.steps == 1, true, "diagnostic summary counts consumed steps");
    expectNear(observedSummary.distanceUnits, 4.32f, "diagnostic summary measures intended distance");
    expect(diagnostic.TakeSummary().steps == 0, true, "diagnostic summary drains once");
    expect(diagnostic.SetMode(RoomscaleMotion::Mode::Off), true, "disabling observation reported");
    diagnostic.OnPose(true, {5.2f, 0.0f, 1.6f}, 0.0f, 43.2f);
    expect(diagnostic.OnCommand(3).has_value(), false, "disabled observer emits no movement intent");
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
