#pragma once

// Razer Hydra (Sixense SDK) emulation fed from Portal2VR's tracked controllers.
// Pure logic: no Windows or OpenVR calls, so it is unit-tested off-target.
//
// The conversion mirrors the community Hydra-OpenVR shims that run the
// MotionPack on SteamVR controllers today: millimetres, a -45 degree X "coil"
// offset, column-major rotation storage and the legacy 96-byte data layout.

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace SixenseBridge {

constexpr int kMaxControllers = 4;
constexpr int kHistorySize = 50;
constexpr int kSuccess = 0;
constexpr int kFailure = -1;

// Sixense SDK button bits.
constexpr std::uint32_t kButtonStart = 1u << 0;
constexpr std::uint32_t kButton3 = 1u << 3;
constexpr std::uint32_t kButton4 = 1u << 4;
constexpr std::uint32_t kButton1 = 1u << 5;
constexpr std::uint32_t kButton2 = 1u << 6;
constexpr std::uint32_t kButtonBumper = 1u << 7;
constexpr std::uint32_t kButtonJoystick = 1u << 8;

// Sixense which_hand values.
constexpr std::uint8_t kHandLeft = 1;
constexpr std::uint8_t kHandRight = 2;

#pragma pack(push, 4)
struct LegacyControllerData {
    float pos[3];
    float rot_mat[3][3];
    std::uint8_t joystick_x;
    std::uint8_t joystick_y;
    std::uint8_t trigger;
    std::uint32_t buttons;
    std::uint8_t sequence_number;
    float rot_quat[4];
    std::uint16_t firmware_revision;
    std::uint16_t hardware_revision;
    std::uint16_t packet_type;
    std::uint16_t magnetic_frequency;
    std::int32_t enabled;
    std::int32_t controller_index;
    std::uint8_t is_docked;
    std::uint8_t which_hand;
};
#pragma pack(pop)

struct LegacyAllControllerData {
    LegacyControllerData controllers[kMaxControllers];
};

static_assert(sizeof(LegacyControllerData) == 96, "MotionPack expects 96-byte controller data");
static_assert(sizeof(LegacyAllControllerData) == 384, "MotionPack expects a 384-byte bundle");

// Where controller positions/rotations are expressed before conversion.
enum class HandSpace {
    // Raw tracking space, as the flat-screen Hydra-OpenVR shims do.
    Tracking,
    // Relative to the headset's position and yaw, so the Hydra "base" turns
    // with the player instead of staying fixed in the room.
    HeadYaw,
};

// Row-major 3x4 rigid transform in metres (OpenVR HmdMatrix34_t layout).
struct Pose34 {
    float m[3][4];
};

struct HandInput {
    bool valid = false;
    Pose34 pose{};
    float joystickX = 0.0f; // -1..1
    float joystickY = 0.0f; // -1..1
    float trigger = 0.0f;   // 0..1
    std::uint32_t buttons = 0; // Sixense button bits
};

struct FrameInput {
    HandInput left;
    HandInput right;
    bool headValid = false;
    Pose34 head{};
};

struct Mat3 {
    float m[3][3]; // m[row][col]
};

inline Mat3 Multiply(const Mat3 &a, const Mat3 &b)
{
    Mat3 r{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            r.m[row][col] = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col] + a.m[row][2] * b.m[2][col];
    return r;
}

inline Mat3 RotationX(float radians)
{
    const float c = std::cos(radians), s = std::sin(radians);
    return {{{1, 0, 0}, {0, c, -s}, {0, s, c}}};
}

inline Mat3 RotationY(float radians)
{
    const float c = std::cos(radians), s = std::sin(radians);
    return {{{c, 0, s}, {0, 1, 0}, {-s, 0, c}}};
}

inline Mat3 RotationOf(const Pose34 &pose)
{
    Mat3 r{};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            r.m[row][col] = pose.m[row][col];
    return r;
}

// Yaw (about +Y) of a pose whose forward axis is -Z, as in OpenVR.
inline float HeadYaw(const Pose34 &head)
{
    const float fx = -head.m[0][2], fz = -head.m[2][2];
    if (std::fabs(fx) < 1e-6f && std::fabs(fz) < 1e-6f) return 0.0f; // looking straight up/down
    return std::atan2(-fx, -fz);
}

// x, y, z, w quaternion of a rotation matrix.
inline std::array<float, 4> QuaternionOf(const Mat3 &r)
{
    const auto &m = r.m;
    const float trace = m[0][0] + m[1][1] + m[2][2];
    float x, y, z, w;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        w = 0.25f * s; x = (m[2][1] - m[1][2]) / s; y = (m[0][2] - m[2][0]) / s; z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        w = (m[2][1] - m[1][2]) / s; x = 0.25f * s; y = (m[0][1] + m[1][0]) / s; z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        w = (m[0][2] - m[2][0]) / s; x = (m[0][1] + m[1][0]) / s; y = 0.25f * s; z = (m[1][2] + m[2][1]) / s;
    } else {
        const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        w = (m[1][0] - m[0][1]) / s; x = (m[0][2] + m[2][0]) / s; y = (m[1][2] + m[2][1]) / s; z = 0.25f * s;
    }
    const float n = std::sqrt(x * x + y * y + z * z + w * w);
    if (n < 1e-8f) return {0.0f, 0.0f, 0.0f, 1.0f};
    return {x / n, y / n, z / n, w / n};
}

inline std::uint8_t EncodeAxis(float value) // -1..1 -> 0..255
{
    if (!(value >= -1.0f)) value = -1.0f; // also catches NaN
    if (value > 1.0f) value = 1.0f;
    return static_cast<std::uint8_t>((value + 1.0f) * 127.5f);
}

inline std::uint8_t EncodeTrigger(float value) // 0..1 -> 0..255
{
    if (!(value >= 0.0f)) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return static_cast<std::uint8_t>(value * 255.0f);
}

constexpr float kCoilOffsetRadians = -0.78539816339744830962f; // -45 degrees about X

inline LegacyControllerData Convert(const HandInput &hand, HandSpace space, bool headValid,
                                    const Pose34 &head, int controllerIndex,
                                    std::uint8_t whichHand, std::uint8_t sequence)
{
    LegacyControllerData out{};
    out.controller_index = controllerIndex;
    out.which_hand = whichHand;
    out.sequence_number = sequence;
    out.packet_type = 1;
    if (!hand.valid) return out;

    Mat3 rotation = RotationOf(hand.pose);
    float pos[3] = {hand.pose.m[0][3], hand.pose.m[1][3], hand.pose.m[2][3]};
    if (space == HandSpace::HeadYaw && headValid) {
        const Mat3 unYaw = RotationY(-HeadYaw(head));
        const float rel[3] = {pos[0] - head.m[0][3], pos[1], pos[2] - head.m[2][3]};
        for (int row = 0; row < 3; ++row)
            pos[row] = unYaw.m[row][0] * rel[0] + unYaw.m[row][1] * rel[1] + unYaw.m[row][2] * rel[2];
        rotation = Multiply(unYaw, rotation);
    }

    // The offset tilts the controller's own frame (post-multiplied), matching
    // what the working Hydra-OpenVR shims hand the game.
    const Mat3 coil = Multiply(rotation, RotationX(kCoilOffsetRadians));
    for (int i = 0; i < 3; ++i)
        out.pos[i] = pos[i] * 1000.0f; // metres -> millimetres
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            out.rot_mat[col][row] = coil.m[row][col]; // Sixense stores columns
    const auto q = QuaternionOf(coil);
    for (int i = 0; i < 4; ++i)
        out.rot_quat[i] = q[i];

    out.joystick_x = EncodeAxis(hand.joystickX);
    out.joystick_y = EncodeAxis(hand.joystickY);
    out.trigger = EncodeTrigger(hand.trigger);
    out.buttons = hand.buttons;
    out.enabled = 1;
    return out;
}

// Slot 0 is the left hand and slot 1 the right; slots 2 and 3 stay disabled.
inline LegacyAllControllerData BuildFrame(const FrameInput &frame, HandSpace space, std::uint8_t sequence)
{
    LegacyAllControllerData all{};
    all.controllers[0] = Convert(frame.left, space, frame.headValid, frame.head, 0, kHandLeft, sequence);
    all.controllers[1] = Convert(frame.right, space, frame.headValid, frame.head, 1, kHandRight, sequence);
    for (int i = 2; i < kMaxControllers; ++i) {
        all.controllers[i].controller_index = i;
        all.controllers[i].sequence_number = sequence;
    }
    return all;
}

// The SDK keeps a 50-deep history of samples; the game reads newest-first.
class History {
public:
    void Publish(const LegacyAllControllerData &frame)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Head = (m_Head + kHistorySize - 1) % kHistorySize;
        m_Ring[m_Head] = frame;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Ring = {};
    }

    int GetData(int which, int indexBack, LegacyControllerData *out) const
    {
        if (!out || which < 0 || which >= kMaxControllers || indexBack < 0 || indexBack >= kHistorySize)
            return kFailure;
        std::lock_guard<std::mutex> lock(m_Mutex);
        *out = m_Ring[(m_Head + indexBack) % kHistorySize].controllers[which];
        return out->enabled ? kSuccess : kFailure;
    }

    int GetAllData(int indexBack, LegacyAllControllerData *out) const
    {
        if (!out || indexBack < 0 || indexBack >= kHistorySize)
            return kFailure;
        std::lock_guard<std::mutex> lock(m_Mutex);
        *out = m_Ring[(m_Head + indexBack) % kHistorySize];
        return kSuccess;
    }

    bool IsEnabled(int which) const
    {
        if (which < 0 || which >= kMaxControllers) return false;
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Ring[m_Head].controllers[which].enabled != 0;
    }

    int ActiveControllers() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        int count = 0;
        for (const auto &controller : m_Ring[m_Head].controllers)
            count += controller.enabled ? 1 : 0;
        return count;
    }

private:
    mutable std::mutex m_Mutex;
    std::array<LegacyAllControllerData, kHistorySize> m_Ring{};
    int m_Head = 0;
};

// sixenseTriggerVibration requests, consumed by the VR thread. The SDK takes
// a duration in 100 ms units; OpenVR pulses are capped, so clamp to 1 s.
class VibrationQueue {
public:
    int Request(int which, int duration100ms)
    {
        if (which < 0 || which >= 2) return kFailure;
        const int ms = duration100ms <= 0 ? 0 : (duration100ms >= 10 ? 1000 : duration100ms * 100);
        m_PendingMs[which].store(ms);
        return kSuccess;
    }

    // Returns the requested pulse in milliseconds, or 0 when none is pending.
    int Take(int which)
    {
        if (which < 0 || which >= 2) return 0;
        return m_PendingMs[which].exchange(0);
    }

private:
    std::atomic<int> m_PendingMs[2]{};
};

} // namespace SixenseBridge
