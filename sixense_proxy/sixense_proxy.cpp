// sixense.dll replacement for the Portal 2 Sixense MotionPack under Portal2VR.
//
// The MotionPack's game DLLs talk to the Razer Hydra through this API. Instead
// of opening a second VR session (as the flat-screen Hydra-OpenVR shims do),
// every call is served from Portal2VR's d3d9.dll, which already owns the
// OpenVR session and publishes Hydra-shaped controller data each frame.
// Until Portal2VR is ready the controllers simply read as disconnected.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "intel_camera_patch.h"
#include "../L4D2VR/sixense_api.h"
#include "../L4D2VR/sixense_bridge.h"

namespace {

constexpr int kSuccess = SixenseBridge::kSuccess;
constexpr int kFailure = SixenseBridge::kFailure;

std::atomic<const Portal2VRSixenseApi *> g_Api{nullptr};

const Portal2VRSixenseApi *Api()
{
    if (const auto *api = g_Api.load(std::memory_order_acquire))
        return api;
    const HMODULE vr = GetModuleHandleA("d3d9.dll");
    if (!vr) return nullptr;
    const auto get = reinterpret_cast<Portal2VRGetSixenseApiFn>(
        reinterpret_cast<void *>(GetProcAddress(vr, P2VR_SIXENSE_API_EXPORT)));
    if (!get) return nullptr; // plain DXVK or another d3d9.dll: no Portal2VR here
    const auto *api = get();
    if (!api || api->version != P2VR_SIXENSE_API_VERSION || api->size < sizeof(Portal2VRSixenseApi))
        return nullptr;
    g_Api.store(api, std::memory_order_release);
    OutputDebugStringA("sixense.dll proxy: serving controllers from Portal2VR\n");
    return api;
}

} // namespace

extern "C" {

int sixenseInit() { Api(); return kSuccess; }
int sixenseExit() { return kSuccess; }

int sixenseGetMaxBases() { return 1; }
int sixenseSetActiveBase(int) { return kSuccess; }
int sixenseIsBaseConnected(int) { return 1; }

int sixenseGetMaxControllers() { return SixenseBridge::kMaxControllers; }
int sixenseGetHistorySize() { return SixenseBridge::kHistorySize; }

int sixenseIsControllerEnabled(int which)
{
    const auto *api = Api();
    return api ? api->IsControllerEnabled(which) : 0;
}

int sixenseGetNumActiveControllers()
{
    const auto *api = Api();
    return api ? api->GetNumActiveControllers() : 0;
}

int sixenseGetData(int which, int indexBack, void *data)
{
    if (!data) return kFailure;
    if (const auto *api = Api())
        return api->GetData(which, indexBack, data);
    std::memset(data, 0, sizeof(SixenseBridge::LegacyControllerData));
    return kFailure;
}

int sixenseGetAllData(int indexBack, void *data)
{
    if (!data) return kFailure;
    if (const auto *api = Api())
        return api->GetAllData(indexBack, data);
    std::memset(data, 0, sizeof(SixenseBridge::LegacyAllControllerData));
    return kSuccess;
}

int sixenseGetNewestData(int which, void *data) { return sixenseGetData(which, 0, data); }
int sixenseGetAllNewestData(void *data) { return sixenseGetAllData(0, data); }

int sixenseTriggerVibration(int which, int duration100ms, int pattern)
{
    const auto *api = Api();
    return api ? api->TriggerVibration(which, duration100ms, pattern) : kSuccess;
}

int sixenseSetHemisphereTrackingMode(int, int) { return kSuccess; }
int sixenseGetHemisphereTrackingMode(int, int *state) { if (state) *state = 1; return kSuccess; }
int sixenseAutoEnableHemisphereTracking(int) { return kSuccess; }

int sixenseSetHighPriorityBindingEnabled(int) { return kSuccess; }
int sixenseGetHighPriorityBindingEnabled(int *on) { if (on) *on = 0; return kSuccess; }

int sixenseSetFilterEnabled(int) { return kSuccess; }
int sixenseGetFilterEnabled(int *on) { if (on) *on = 0; return kSuccess; }
int sixenseSetFilterParams(float, float, float, float) { return kSuccess; }
int sixenseGetFilterParams(float *nearRange, float *nearVal, float *farRange, float *farVal)
{
    if (nearRange) *nearRange = 0.0f;
    if (nearVal) *nearVal = 0.0f;
    if (farRange) *farRange = 0.0f;
    if (farVal) *farVal = 0.0f;
    return kSuccess;
}

int sixenseSetBaseColor(unsigned char, unsigned char, unsigned char) { return kSuccess; }
int sixenseGetBaseColor(unsigned char *r, unsigned char *g, unsigned char *b)
{
    if (r) *r = 0;
    if (g) *g = 0;
    if (b) *b = 0;
    return kSuccess;
}

// Undocumented SDK exports the MotionPack may import; accepted and ignored.
int sixenseSetDebugParam() { return kSuccess; }
int sixenseGetDebugParam() { return kSuccess; }
int sixenseSetCalibrationEnabled() { return kSuccess; }
int sixenseGetCalibrationEnabled() { return kSuccess; }
int sixenseSetHemisphereVector() { return kSuccess; }
int sixenseGetHemisphereVector() { return kSuccess; }
int sixenseGetRawData() { return kSuccess; }
int sixenseGetRawDataSingle() { return kSuccess; }
int sixenseGetSignalMatrix() { return kSuccess; }
int sixenseGetSignalQuality() { return kSuccess; }
int sixenseSetTestMode() { return kSuccess; }
int sixenseGetTestMode() { return kSuccess; }
int sixensePlaybackLogFile() { return kSuccess; }
int sixenseSendTestCommand() { return kSuccess; }

}

namespace {

const char kIntelDisabled[] = "0";

// See intel_camera_patch.h. Runs under the loader lock, before
// client_sixense.dll's static constructors; only touches already-mapped memory.
void DisableIntelCameraByDefault()
{
    if (std::strstr(GetCommandLineA(), "-p2vr_intel_camera")) {
        OutputDebugStringA("sixense.dll proxy: -p2vr_intel_camera set, leaving Intel camera code on\n");
        return;
    }
    const HMODULE client = GetModuleHandleA("client_sixense.dll");
    if (!client) return;

    const auto *image = reinterpret_cast<const std::uint8_t *>(client);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(image + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) return;

    const std::size_t offset = IntelCameraPatch::FindEnabledDefaultOperand(
        image, nt->OptionalHeader.SizeOfImage, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image)));
    if (offset == IntelCameraPatch::kNotFound) {
        OutputDebugStringA("sixense.dll proxy: sixense_intel_enabled registration not found; Intel camera code unchanged\n");
        return;
    }

    void *target = const_cast<std::uint8_t *>(image + offset);
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, sizeof(std::uint32_t), PAGE_EXECUTE_READWRITE, &oldProtect)) return;
    const auto value = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kIntelDisabled));
    std::memcpy(target, &value, sizeof(value));
    VirtualProtect(target, sizeof(std::uint32_t), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(std::uint32_t));
    OutputDebugStringA("sixense.dll proxy: sixense_intel_enabled now defaults to 0 (no Intel camera)\n");
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        DisableIntelCameraByDefault();
    }
    return TRUE;
}
