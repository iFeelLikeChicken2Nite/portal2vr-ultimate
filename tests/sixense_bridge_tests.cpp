#include "../L4D2VR/sixense_bridge.h"
#include "../L4D2VR/game_modules.h"
#include "../L4D2VR/config.h"
#include "../sixense_proxy/intel_camera_patch.h"
#include "../L4D2VR/view_setup_layout.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace SixenseBridge;

namespace {

int failures = 0;

void Expect(bool ok, const char *name)
{
    if (!ok) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}

bool Near(float a, float b, float epsilon = 1e-4f) { return std::fabs(a - b) <= epsilon; }

Pose34 Identity(float x, float y, float z)
{
    Pose34 pose{};
    pose.m[0][0] = pose.m[1][1] = pose.m[2][2] = 1.0f;
    pose.m[0][3] = x; pose.m[1][3] = y; pose.m[2][3] = z;
    return pose;
}

Pose34 Yawed(float radians, float x, float y, float z)
{
    const Mat3 r = RotationY(radians);
    Pose34 pose = Identity(x, y, z);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            pose.m[row][col] = r.m[row][col];
    return pose;
}

constexpr float kHalfSqrt2 = 0.70710678f;

// The Source SDK headers redefine offsetof, so field placement is checked at run time.
void TestLegacyLayout()
{
    LegacyControllerData data{};
    const auto at = [&](const void *field) {
        return static_cast<const char *>(field) - reinterpret_cast<const char *>(&data);
    };
    Expect(at(&data.joystick_x) == 48 && at(&data.buttons) == 52 && at(&data.sequence_number) == 56,
           "legacy input field offsets");
    Expect(at(&data.rot_quat) == 60 && at(&data.enabled) == 84 && at(&data.controller_index) == 88,
           "legacy pose/state field offsets");
    Expect(at(&data.which_hand) == 93, "legacy which_hand offset");
}

void TestTrackingConversion()
{
    HandInput hand;
    hand.valid = true;
    hand.pose = Identity(0.1f, 1.2f, -0.3f);
    hand.joystickX = 1.0f;
    hand.joystickY = -1.0f;
    hand.trigger = 0.5f;
    hand.buttons = kButtonBumper | kButton1;
    const auto out = Convert(hand, HandSpace::Tracking, false, Pose34{}, 1, kHandRight, 7);

    Expect(out.enabled == 1 && out.controller_index == 1 && out.which_hand == kHandRight,
           "identity fields");
    Expect(out.sequence_number == 7 && out.packet_type == 1, "sequence and packet type");
    Expect(Near(out.pos[0], 100.0f) && Near(out.pos[1], 1200.0f) && Near(out.pos[2], -300.0f),
           "metres become millimetres");
    // Identity controller -> pure -45 degree X coil offset, stored by column.
    Expect(Near(out.rot_mat[0][0], 1.0f) && Near(out.rot_mat[1][1], kHalfSqrt2) &&
           Near(out.rot_mat[2][2], kHalfSqrt2), "coil diagonal");
    Expect(Near(out.rot_mat[1][2], -kHalfSqrt2) && Near(out.rot_mat[2][1], kHalfSqrt2),
           "column-major coil off-diagonal");
    Expect(Near(out.rot_quat[0], -0.38268343f) && Near(out.rot_quat[1], 0.0f) &&
           Near(out.rot_quat[2], 0.0f) && Near(out.rot_quat[3], 0.92387953f), "coil quaternion");
    Expect(out.joystick_x == 255 && out.joystick_y == 0 && out.trigger == 127, "legacy axis encoding");
    Expect(out.buttons == (kButtonBumper | kButton1), "buttons pass through");
}

void TestCoilIsInControllerFrame()
{
    // A controller yawed 90 degrees keeps its yaw; the coil tilt applies about
    // the controller's own X axis (rotation * Rx(-45)), as the working shims do.
    HandInput hand;
    hand.valid = true;
    hand.pose = Yawed(1.5707963f, 0, 0, 0);
    const auto out = Convert(hand, HandSpace::Tracking, false, Pose34{}, 0, kHandLeft, 0);
    const Mat3 expected = Multiply(RotationY(1.5707963f), RotationX(kCoilOffsetRadians));
    bool same = true;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            same = same && Near(out.rot_mat[col][row], expected.m[row][col]);
    Expect(same, "coil offset post-multiplies the controller rotation");
}

void TestHeadYawSpace()
{
    const float yaw = 1.5707963f; // facing -X
    const Pose34 head = Yawed(yaw, 1.0f, 1.7f, 2.0f);
    HandInput hand;
    hand.valid = true;
    hand.pose = Yawed(yaw, 0.5f, 1.2f, 2.0f); // half a metre in front of the face
    const auto out = Convert(hand, HandSpace::HeadYaw, true, head, 0, kHandLeft, 0);
    Expect(Near(out.pos[0], 0.0f, 0.05f) && Near(out.pos[1], 1200.0f, 0.05f) &&
           Near(out.pos[2], -500.0f, 0.05f), "head-yaw position is straight ahead");
    Expect(Near(out.rot_mat[0][0], 1.0f) && Near(out.rot_mat[1][1], kHalfSqrt2),
           "head-yaw removes the shared yaw");
    Expect(Near(HeadYaw(head), yaw), "head yaw from -Z forward");

    const auto fallback = Convert(hand, HandSpace::HeadYaw, false, head, 0, kHandLeft, 0);
    Expect(Near(fallback.pos[0], 500.0f), "head-yaw without a head pose falls back to tracking");
}

void TestInvalidHand()
{
    HandInput hand;
    hand.buttons = kButtonStart;
    const auto out = Convert(hand, HandSpace::Tracking, false, Pose34{}, 0, kHandLeft, 3);
    Expect(out.enabled == 0 && out.buttons == 0 && out.which_hand == kHandLeft, "untracked hand is disabled");
}

void TestEncoding()
{
    Expect(EncodeAxis(0.0f) == 127 && EncodeAxis(-2.0f) == 0 && EncodeAxis(2.0f) == 255, "axis clamps");
    Expect(EncodeAxis(std::nanf("")) == 0 && EncodeTrigger(std::nanf("")) == 0, "NaN is rejected");
    Expect(EncodeTrigger(1.0f) == 255 && EncodeTrigger(-1.0f) == 0, "trigger clamps");
}

void TestHistory()
{
    History history;
    LegacyControllerData data{};
    Expect(history.GetData(0, 0, &data) == kFailure, "empty history reports no controller");
    Expect(history.ActiveControllers() == 0, "empty history has no active controllers");

    FrameInput frame;
    frame.left.valid = true;
    frame.left.pose = Identity(0, 0, 0);
    for (int i = 0; i < 60; ++i)
        history.Publish(BuildFrame(frame, HandSpace::Tracking, static_cast<std::uint8_t>(i)));

    Expect(history.GetData(0, 0, &data) == kSuccess && data.sequence_number == 59, "newest sample first");
    Expect(history.GetData(0, 49, &data) == kSuccess && data.sequence_number == 10, "oldest kept sample");
    Expect(history.GetData(0, 50, &data) == kFailure, "history is 50 deep");
    Expect(history.GetData(4, 0, &data) == kFailure && history.GetData(-1, 0, &data) == kFailure,
           "controller index is bounds-checked");
    Expect(history.GetData(1, 0, &data) == kFailure && data.which_hand == kHandRight,
           "untracked right hand reads as disabled");
    Expect(history.IsEnabled(0) && !history.IsEnabled(1) && history.ActiveControllers() == 1,
           "enabled state follows the newest frame");

    LegacyAllControllerData all{};
    Expect(history.GetAllData(0, &all) == kSuccess && all.controllers[3].controller_index == 3,
           "bundle fills every slot");
    Expect(history.GetAllData(0, nullptr) == kFailure, "null bundle rejected");
}

void TestVibration()
{
    VibrationQueue queue;
    Expect(queue.Take(0) == 0, "no pending pulse");
    Expect(queue.Request(1, 3) == kSuccess && queue.Take(1) == 300 && queue.Take(1) == 0,
           "pulse is consumed once");
    Expect(queue.Request(0, 600) == kSuccess && queue.Take(0) == 1000, "long pulses are capped");
    Expect(queue.Request(2, 1) == kFailure, "only two hands vibrate");
}

void TestGameModules()
{
    using namespace GameModules;
    const auto sixense = [](const char *name) { return std::string(name) == kSixenseClient; };
    const auto stock = [](const char *name) { return std::string(name) == kStockClient; };
    const auto both = [](const char *) { return true; };
    const auto none = [](const char *) { return false; };
    Expect(DetectVariant(sixense) == Variant::Sixense, "sixense client detected");
    Expect(DetectVariant(stock) == Variant::Stock, "stock client detected");
    Expect(DetectVariant(both) == Variant::Sixense, "sixense client wins");
    Expect(DetectVariant(none) == Variant::Unknown, "nothing loaded yet");
    Expect(DetectVariant(stock, true) == Variant::Unknown, "sixense stub client.dll waits for client_sixense.dll");
    Expect(DetectVariant(both, true) == Variant::Sixense, "sixense client after its stub");

    Expect(std::string(ResolveName("client.dll", Variant::Sixense)) == "client_sixense.dll" &&
           std::string(ResolveName("SERVER.DLL", Variant::Sixense)) == "server_sixense.dll",
           "game DLLs map to the sixense variants");
    Expect(std::string(ResolveName("engine.dll", Variant::Sixense)) == "engine.dll", "engine is shared");
    Expect(std::string(ResolveName("client.dll", Variant::Stock)) == "client.dll", "stock names unchanged");

    SetVariant(Variant::Sixense);
    Expect(SameName(ResolveWide(L"client.dll"), L"client_sixense.dll"), "wide names resolve");
    SetVariant(Variant::Unknown);
}

void TestConfig()
{
    std::istringstream defaults("");
    const auto unchanged = ParseConfig(defaults, ConfigSnapshot{});
    Expect(unchanged.value.sixenseEmulation &&
           unchanged.value.sixenseHandSpace == HandSpace::Tracking, "sixense defaults");

    std::istringstream options("SixenseMode=Off\nSixenseHandSpace=HeadYaw\n");
    const auto parsed = ParseConfig(options, ConfigSnapshot{});
    Expect(parsed.errors.empty() && !parsed.value.sixenseEmulation &&
           parsed.value.sixenseHandSpace == HandSpace::HeadYaw, "sixense options parse");

    Expect(!unchanged.value.keepWindowResolution, "window resolution diagnostic off by default");
    std::istringstream keep("KeepWindowResolution=true # diagnostic\n");
    Expect(ParseConfig(keep, ConfigSnapshot{}).value.keepWindowResolution, "window resolution diagnostic parses");

    std::istringstream submit("MenuSubmitMode=None\n");
    Expect(ParseConfig(submit, ConfigSnapshot{}).value.menuSubmitMode == 2 &&
           unchanged.value.menuSubmitMode == 0, "menu submit diagnostic parses");

    std::istringstream bad("SixenseMode=On\nSixenseHandSpace=Room\n");
    const auto rejected = ParseConfig(bad, parsed.value);
    Expect(rejected.errors.size() == 2 && !rejected.value.sixenseEmulation &&
           rejected.value.sixenseHandSpace == HandSpace::HeadYaw, "invalid sixense options keep previous values");
}

void TestViewSetupLayout()
{
    std::uint8_t sixense[ViewSetupLayout::kSixenseSize];
    for (std::size_t i = 0; i < sizeof(sixense); ++i) sixense[i] = static_cast<std::uint8_t>(i * 7 + 1);
    std::uint8_t stock[ViewSetupLayout::kStockSize];
    ViewSetupLayout::FromSixense(sixense, stock);
    auto int32At = [](const std::uint8_t *p, std::size_t at) { std::int32_t v; std::memcpy(&v, p + at, 4); return v; };
    Expect(int32At(stock, 0x10) == int32At(sixense, 0x08) && int32At(stock, 0x14) == int32At(sixense, 0x08) &&
           int32At(stock, 0x18) == int32At(sixense, 0x0C), "view width/height land on stock offsets");
    Expect(std::memcmp(stock + 0x68, sixense + 0x58, 4) == 0, "view fov lands on stock offset");
    Expect(std::memcmp(stock + 0x98, sixense + 0x88, 4) == 0, "view aspect lands on stock offset");
    std::uint8_t back[ViewSetupLayout::kSixenseSize];
    ViewSetupLayout::ToSixense(stock, back);
    Expect(std::memcmp(back, sixense, sizeof(back)) == 0, "view setup round-trips");
}

// Builds a fake image laid out like client_sixense.dll's registration.
std::vector<std::uint8_t> ConVarImage(const char *name, const char *defaultValue, std::uint32_t base)
{
    std::vector<std::uint8_t> image(0x100, 0xCC);
    auto put32 = [&](std::size_t at, std::uint32_t value) { std::memcpy(&image[at], &value, 4); };
    std::memcpy(&image[0x80], name, std::strlen(name) + 1);
    std::memcpy(&image[0xC0], defaultValue, std::strlen(defaultValue) + 1);
    image[0x10] = 0x68; put32(0x11, 0x2000);
    image[0x15] = 0x68; put32(0x16, base + 0xC0);
    image[0x1A] = 0x68; put32(0x1B, base + 0x80);
    image[0x1F] = 0xB9; put32(0x20, base + 0xF0);
    return image;
}

void TestIntelCameraPatch()
{
    using IntelCameraPatch::FindEnabledDefaultOperand;
    using IntelCameraPatch::kNotFound;
    const std::uint32_t base = 0x66180000;

    const auto image = ConVarImage("sixense_intel_enabled", "1", base);
    Expect(FindEnabledDefaultOperand(image.data(), image.size(), base) == 0x16, "intel convar default operand found");

    const auto off = ConVarImage("sixense_intel_enabled", "0", base);
    Expect(FindEnabledDefaultOperand(off.data(), off.size(), base) == kNotFound, "already-off default left alone");

    const auto other = ConVarImage("sixense_intel_enabled_toggle", "1", base);
    Expect(FindEnabledDefaultOperand(other.data(), other.size(), base) == kNotFound, "similar convar name ignored");

    Expect(FindEnabledDefaultOperand(image.data(), image.size(), base + 0x1000) == kNotFound, "pointers outside image ignored");
    Expect(FindEnabledDefaultOperand(nullptr, 0, base) == kNotFound, "empty image");

    // push 2948h; mov [x],esi; call new; add esp,4; cmp eax,esi; je +0x95; mov ecx,eax; call ctor
    const std::uint8_t site[] = {
        0x68, 0x48, 0x29, 0x00, 0x00, 0x89, 0x35, 0x9C, 0x3E, 0xA6, 0x10,
        0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x04, 0x3B, 0xC6,
        0x0F, 0x84, 0x95, 0x00, 0x00, 0x00, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
    std::vector<std::uint8_t> code(site, site + sizeof(site));
    std::size_t sites[IntelCameraPatch::kMaxCameraSites];
    const std::size_t found = IntelCameraPatch::FindCameraAllocationChecks(code.data(), code.size(), sites);
    Expect(found == 1 && sites[0] == 21, "camera allocation check found");
    if (found == 1) {
        IntelCameraPatch::JeToJmp(&code[sites[0]]);
        const std::uint8_t jmp[] = { 0xE9, 0x96, 0x00, 0x00, 0x00, 0x90 };
        Expect(std::memcmp(&code[21], jmp, sizeof(jmp)) == 0, "je becomes jmp to the same target");
    }

    const std::uint8_t prompt[] = {
        0x90, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x84, 0xC0, 0x75, 0x22,
        0x39, 0x9E, 0x38, 0x08, 0x00, 0x00, 0x75, 0x1A,
        0xC6, 0x86, 0x3D, 0x08, 0x00, 0x00, 0x01, 0xCC };
    Expect(IntelCameraPatch::FindCameraPromptBranch(prompt, sizeof(prompt)) == 8, "camera prompt branch found");
    std::vector<std::uint8_t> twice(prompt, prompt + sizeof(prompt));
    twice.insert(twice.end(), prompt, prompt + sizeof(prompt));
    Expect(IntelCameraPatch::FindCameraPromptBranch(twice.data(), twice.size()) == IntelCameraPatch::kNotFound,
           "ambiguous camera prompt left alone");

    code[1] = 0x50; // a different allocation size
    Expect(IntelCameraPatch::FindCameraAllocationChecks(code.data(), code.size(), sites) == 0,
           "other allocations ignored");
}

} // namespace

int main()
{
    TestLegacyLayout();
    TestTrackingConversion();
    TestCoilIsInControllerFrame();
    TestHeadYawSpace();
    TestInvalidHand();
    TestEncoding();
    TestHistory();
    TestVibration();
    TestGameModules();
    TestConfig();
    TestIntelCameraPatch();
    TestViewSetupLayout();
    if (failures) {
        std::cerr << failures << " sixense bridge test(s) failed\n";
        return 1;
    }
    std::cout << "sixense bridge tests passed\n";
    return 0;
}
