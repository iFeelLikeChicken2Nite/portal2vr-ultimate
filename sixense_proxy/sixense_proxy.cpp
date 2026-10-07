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

// Call log (sixense_proxy.log beside this DLL): first call of each export,
// then every 5 s the call counts and what the last data call returned. Shows
// what the MotionPack asks for and where its Hydra setup stops.
struct CallStat { const char *name; std::atomic<unsigned> count; };
CallStat g_Calls[48];
std::atomic<int> g_CallTypes{0};
SRWLOCK g_LogLock = SRWLOCK_INIT;
FILE *g_Log = nullptr;
ULONGLONG g_NextSummary = 0;
HMODULE g_Self = nullptr;

void LogLine(const char *text)
{
    AcquireSRWLockExclusive(&g_LogLock);
    if (!g_Log) {
        char path[MAX_PATH];
        const DWORD n = g_Self ? GetModuleFileNameA(g_Self, path, MAX_PATH) : 0;
        if (n && n < MAX_PATH) {
            char *slash = strrchr(path, '\\');
            if (slash) {
                strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "sixense_proxy.log");
                fopen_s(&g_Log, path, "w");
            }
        }
    }
    if (g_Log) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(g_Log, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, text);
        fflush(g_Log);
    }
    ReleaseSRWLockExclusive(&g_LogLock);
}

void Trace(const char *name)
{
    const int types = g_CallTypes.load();
    for (int i = 0; i < types; ++i)
        if (g_Calls[i].name == name) { ++g_Calls[i].count; return; }
    AcquireSRWLockExclusive(&g_LogLock);
    int index = -1;
    const int now = g_CallTypes.load();
    for (int i = 0; i < now; ++i)
        if (g_Calls[i].name == name) index = i;
    if (index < 0 && now < 48) {
        g_Calls[now].name = name;
        g_Calls[now].count = 0;
        g_CallTypes.store(now + 1);
        index = now;
    }
    ReleaseSRWLockExclusive(&g_LogLock);
    if (index >= 0) {
        ++g_Calls[index].count;
        if (g_Calls[index].count == 1) {
            char line[128];
            sprintf_s(line, "first call: %s", name);
            LogLine(line);
        }
    }
}

void LogData(const char *what, int first, const SixenseBridge::LegacyControllerData *c, int count, int result)
{
    const auto now = GetTickCount64();
    if (now < g_NextSummary) return;
    g_NextSummary = now + 5000;
    char line[1024];
    int used = sprintf_s(line, "calls:");
    for (int i = 0; i < g_CallTypes.load() && used < 900; ++i)
        used += sprintf_s(line + used, sizeof(line) - used, " %s=%u", g_Calls[i].name, g_Calls[i].count.load());
    LogLine(line);
    for (int i = 0; i < count; ++i) {
        sprintf_s(line, "%s[%d] -> %d: enabled=%d index=%d hand=%d docked=%d buttons=0x%X trigger=%u stick=%u,%u seq=%u fw=%u hw=%u pos=%.0f,%.0f,%.0f",
            what, first + i, result, c[i].enabled, c[i].controller_index, c[i].which_hand, c[i].is_docked,
            c[i].buttons, c[i].trigger, c[i].joystick_x, c[i].joystick_y, c[i].sequence_number,
            c[i].firmware_revision, c[i].hardware_revision, c[i].pos[0], c[i].pos[1], c[i].pos[2]);
        LogLine(line);
    }
}

} // namespace

extern "C" {

int sixenseInit() { Trace(__func__); Api(); return kSuccess; }
int sixenseExit() { Trace(__func__); return kSuccess; }

int sixenseGetMaxBases() { Trace(__func__); return 1; }
int sixenseSetActiveBase(int) { Trace(__func__); return kSuccess; }
int sixenseIsBaseConnected(int) { Trace(__func__); return 1; }

int sixenseGetMaxControllers() { Trace(__func__); return SixenseBridge::kMaxControllers; }
int sixenseGetHistorySize() { Trace(__func__); return SixenseBridge::kHistorySize; }

int sixenseIsControllerEnabled(int which)
{
    Trace(__func__);
    const auto *api = Api();
    return api ? api->IsControllerEnabled(which) : 0;
}

int sixenseGetNumActiveControllers()
{
    Trace(__func__);
    const auto *api = Api();
    return api ? api->GetNumActiveControllers() : 0;
}

int sixenseGetData(int which, int indexBack, void *data)
{
    Trace(__func__);
    if (!data) return kFailure;
    if (const auto *api = Api())
    {
        const int result = api->GetData(which, indexBack, data);
        LogData("GetData", which, static_cast<const SixenseBridge::LegacyControllerData *>(data), 1, result);
        return result;
    }
    std::memset(data, 0, sizeof(SixenseBridge::LegacyControllerData));
    return kFailure;
}

int sixenseGetAllData(int indexBack, void *data)
{
    Trace(__func__);
    if (!data) return kFailure;
    if (const auto *api = Api())
    {
        const int result = api->GetAllData(indexBack, data);
        const auto *all = static_cast<const SixenseBridge::LegacyAllControllerData *>(data);
        LogData("GetAllData", 0, all->controllers, SixenseBridge::kMaxControllers, result);
        return result;
    }
    std::memset(data, 0, sizeof(SixenseBridge::LegacyAllControllerData));
    return kSuccess;
}

int sixenseGetNewestData(int which, void *data) { Trace(__func__); return sixenseGetData(which, 0, data); }
int sixenseGetAllNewestData(void *data) { Trace(__func__); return sixenseGetAllData(0, data); }

int sixenseTriggerVibration(int which, int duration100ms, int pattern)
{
    Trace(__func__);
    const auto *api = Api();
    return api ? api->TriggerVibration(which, duration100ms, pattern) : kSuccess;
}

int sixenseSetHemisphereTrackingMode(int, int) { Trace(__func__); return kSuccess; }
int sixenseGetHemisphereTrackingMode(int, int *state) { Trace(__func__); if (state) *state = 1; return kSuccess; }
int sixenseAutoEnableHemisphereTracking(int) { Trace(__func__); return kSuccess; }

int sixenseSetHighPriorityBindingEnabled(int) { Trace(__func__); return kSuccess; }
int sixenseGetHighPriorityBindingEnabled(int *on) { Trace(__func__); if (on) *on = 0; return kSuccess; }

int sixenseSetFilterEnabled(int) { Trace(__func__); return kSuccess; }
int sixenseGetFilterEnabled(int *on) { Trace(__func__); if (on) *on = 0; return kSuccess; }
int sixenseSetFilterParams(float, float, float, float) { Trace(__func__); return kSuccess; }
int sixenseGetFilterParams(float *nearRange, float *nearVal, float *farRange, float *farVal)
{
    Trace(__func__);
    if (nearRange) *nearRange = 0.0f;
    if (nearVal) *nearVal = 0.0f;
    if (farRange) *farRange = 0.0f;
    if (farVal) *farVal = 0.0f;
    return kSuccess;
}

int sixenseSetBaseColor(unsigned char, unsigned char, unsigned char) { Trace(__func__); return kSuccess; }
int sixenseGetBaseColor(unsigned char *r, unsigned char *g, unsigned char *b)
{
    Trace(__func__);
    if (r) *r = 0;
    if (g) *g = 0;
    if (b) *b = 0;
    return kSuccess;
}

// Undocumented SDK exports the MotionPack may import; accepted and ignored.
int sixenseSetDebugParam() { Trace(__func__); return kSuccess; }
int sixenseGetDebugParam() { Trace(__func__); return kSuccess; }
int sixenseSetCalibrationEnabled() { Trace(__func__); return kSuccess; }
int sixenseGetCalibrationEnabled() { Trace(__func__); return kSuccess; }
int sixenseSetHemisphereVector() { Trace(__func__); return kSuccess; }
int sixenseGetHemisphereVector() { Trace(__func__); return kSuccess; }
int sixenseGetRawData() { Trace(__func__); return kSuccess; }
int sixenseGetRawDataSingle() { Trace(__func__); return kSuccess; }
int sixenseGetSignalMatrix() { Trace(__func__); return kSuccess; }
int sixenseGetSignalQuality() { Trace(__func__); return kSuccess; }
int sixenseSetTestMode() { Trace(__func__); return kSuccess; }
int sixenseGetTestMode() { Trace(__func__); return kSuccess; }
int sixensePlaybackLogFile() { Trace(__func__); return kSuccess; }
int sixenseSendTestCommand() { Trace(__func__); return kSuccess; }

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

    const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
    auto patch = [](const std::uint8_t *at, std::size_t length, auto &&write) {
        void *target = const_cast<std::uint8_t *>(at);
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
        write(static_cast<std::uint8_t *>(target));
        VirtualProtect(target, length, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), target, length);
        return true;
    };

    const std::size_t offset = IntelCameraPatch::FindEnabledDefaultOperand(
        image, imageSize, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(image)));
    if (offset == IntelCameraPatch::kNotFound) {
        OutputDebugStringA("sixense.dll proxy: sixense_intel_enabled registration not found\n");
    } else if (patch(image + offset, sizeof(std::uint32_t), [](std::uint8_t *at) {
                   const auto value = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(kIntelDisabled));
                   std::memcpy(at, &value, sizeof(value));
               })) {
        OutputDebugStringA("sixense.dll proxy: sixense_intel_enabled now defaults to 0 (no Intel camera)\n");
    }

    std::size_t sites[IntelCameraPatch::kMaxCameraSites];
    const std::size_t count = IntelCameraPatch::FindCameraAllocationChecks(image, imageSize, sites);
    std::size_t patched = 0;
    for (std::size_t i = 0; i < count; ++i)
        patched += patch(image + sites[i], 6, IntelCameraPatch::JeToJmp) ? 1 : 0;
    char message[96];
    wsprintfA(message, "sixense.dll proxy: Intel camera creation disabled at %u of %u sites\n",
              static_cast<unsigned>(patched), static_cast<unsigned>(count));
    OutputDebugStringA(message);

    const std::size_t prompt = IntelCameraPatch::FindCameraPromptBranch(image, imageSize);
    if (prompt == IntelCameraPatch::kNotFound)
        OutputDebugStringA("sixense.dll proxy: camera prompt check not found\n");
    else if (patch(image + prompt, 1, [](std::uint8_t *at) { *at = 0xEB; }))
        OutputDebugStringA("sixense.dll proxy: \"Connect camera\" prompt disabled\n");
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_Self = module;
        DisableIntelCameraByDefault();
    }
    return TRUE;
}
