#include "../L4D2VR/digital_input.h"
#include "../L4D2VR/tracked_device.h"
#include <cstring>
#include <iostream>

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

    if (failures) return 1;
    std::cout << "digital input tests passed\n";
    return 0;
}
