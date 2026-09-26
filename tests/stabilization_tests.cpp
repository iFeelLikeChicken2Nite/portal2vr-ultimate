#include "../L4D2VR/digital_input.h"
#include "../L4D2VR/tracked_device.h"
#include "../L4D2VR/sigscanner.h"
#include "../L4D2VR/config.h"
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

int main()
{
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

    if (failures) return 1;
    std::cout << "stabilization tests passed\n";
    return 0;
}
