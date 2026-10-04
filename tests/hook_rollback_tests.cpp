#include <windows.h>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <type_traits>

#include "../L4D2VR/hook_startup_rollback.h"
#include "../L4D2VR/compatibility_hook_policy.h"

namespace {
using TargetFn = int (__stdcall *)(int);

volatile LONG g_offset = 17;
volatile LONG g_callOriginal = 0;
HANDLE g_entered = nullptr;
HANDLE g_resume = nullptr;
TargetFn g_original = nullptr;
TargetFn volatile g_entry = nullptr;

__declspec(noinline) int __stdcall Target(int value)
{
	return value + g_offset;
}

int __stdcall Detour(int value)
{
	SetEvent(g_entered);
	if (WaitForSingleObject(g_resume, 10000) != WAIT_OBJECT_0)
		return -700;
	// The RED version frees the trampoline. Never call a freed address.
	if (!InterlockedCompareExchange(&g_callOriginal, 0, 0))
		return -701;
	return g_original(value) + 1;
}

DWORD WINAPI Worker(LPVOID)
{
	return static_cast<DWORD>(g_entry(3));
}

bool ExecutableTrampoline(LPCVOID address)
{
	MEMORY_BASIC_INFORMATION info{};
	if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
		info.State != MEM_COMMIT)
		return false;
	const DWORD protection = info.Protect & 0xff;
	return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
		protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

struct FixtureVector { float x, y, z; };
using VectorFn = FixtureVector *(__thiscall *)(void *, FixtureVector *);
using CallerFn = FixtureVector *(__cdecl *)(void *, FixtureVector *);
VectorFn g_vectorOriginal = nullptr;
CompatibilityHooks::ViewModelFov g_fovOriginal = nullptr;
std::uintptr_t g_shootCaller = 0;
bool g_published = false;
float g_sourceFov = 43.75f;

__declspec(noinline) FixtureVector *__fastcall SharedVector(void *, void *, FixtureVector *output)
{
    *output = {1.0f, 2.0f, 3.0f};
    return output;
}

FixtureVector *__fastcall ScopedVector(void *self, void *, FixtureVector *output)
{
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    auto *result = g_vectorOriginal(self, output);
    if (g_published && CompatibilityHooks::IsVerifiedShootCaller(caller, g_shootCaller))
        *result = {10.0f, 20.0f, 30.0f};
    return result;
}

__declspec(noinline) float __fastcall NativeFov(void *self, void *)
{
    return self == &g_sourceFov ? g_sourceFov : -22.5f;
}

float __fastcall ScopedFov(void *self, void *)
{
    return g_published ? 70.5f : g_fovOriginal(self);
}

bool RunCompatibilityTests()
{
    using ExpectedFov = float (__thiscall *)(void *);
    if (!std::is_same<CompatibilityHooks::ViewModelFov, ExpectedFov>::value) {
        std::fputs("FAIL: viewmodel FOV must use float thiscall with no stack arguments\n", stderr);
        return false;
    }
    // Two different call sites dispatch to the same hidden-vector-return ABI.
    // Only the first call site is allowed to replace the original result.
    auto *code = static_cast<unsigned char *>(VirtualAlloc(nullptr, 64,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!code) return false;
    unsigned char caller[] = {0x55, 0x8B, 0xEC, 0x8B, 0x4D, 0x08,
        0xFF, 0x75, 0x0C, 0xB8, 0, 0, 0, 0, 0xFF, 0xD0, 0x5D, 0xC3};
    const auto target = reinterpret_cast<std::uintptr_t>(&SharedVector);
    static_assert(sizeof(target) == 4, "These are x86 ABI tests");
    std::memcpy(caller + 10, &target, sizeof(target));
    std::memcpy(code, caller, sizeof(caller));
    std::memcpy(code + 32, caller, sizeof(caller));
    FlushInstructionCache(GetCurrentProcess(), code, 64);
    const auto shoot = reinterpret_cast<CallerFn>(code);
    const auto ear = reinterpret_cast<CallerFn>(code + 32);
    g_shootCaller = reinterpret_cast<std::uintptr_t>(code + 16);
    const auto fov = reinterpret_cast<ExpectedFov>(&NativeFov);
    bool installed = MH_Initialize() == MH_OK &&
        MH_CreateHook(reinterpret_cast<LPVOID>(&SharedVector),
            reinterpret_cast<LPVOID>(&ScopedVector),
            reinterpret_cast<LPVOID *>(&g_vectorOriginal)) == MH_OK &&
        MH_CreateHook(reinterpret_cast<LPVOID>(&NativeFov),
            reinterpret_cast<LPVOID>(&ScopedFov),
            reinterpret_cast<LPVOID *>(&g_fovOriginal)) == MH_OK &&
        MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
    bool good = installed;
    if (installed) {
        FixtureVector output{};
        g_published = true;
        good = shoot(nullptr, &output) == &output && output.x == 10 && output.y == 20 && output.z == 30;
        if (!good) std::fputs("FAIL: verified firing caller did not receive VR origin\n", stderr);
        good = ear(nullptr, &output) == &output && output.x == 1 && output.y == 2 && output.z == 3 && good;
        g_shootCaller = 0;
        good = shoot(nullptr, &output) == &output && output.x == 1 && good;
        g_shootCaller = reinterpret_cast<std::uintptr_t>(code + 16);
        g_published = false;
        good = shoot(nullptr, &output) == &output && output.x == 1 && good;
        for (int i = 0; i < 100; ++i) {
            good = fov(&g_sourceFov) == 43.75f && good;
            good = fov(nullptr) == -22.5f && good;
        }
        g_published = true;
        good = fov(&g_sourceFov) == 70.5f && good;
    }
    good = MH_DisableHook(MH_ALL_HOOKS) == MH_OK && good;
    good = MH_Uninitialize() == MH_OK && good;
    VirtualFree(code, 0, MEM_RELEASE);
    if (good) std::puts("PASS: shared vector hook preserves other callers; float FOV ABI and native fallback intact");
    else std::fputs("FAIL: compatibility detour regression\n", stderr);
    return good;
}
} // namespace

int main()
{
	if (!RunCompatibilityTests()) return 1;
	g_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	g_resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!g_entered || !g_resume) {
		std::fprintf(stderr, "FAIL: could not create synchronization events\n");
		return 1;
	}
	if (MH_Initialize() != MH_OK ||
		MH_CreateHook(reinterpret_cast<LPVOID>(&Target),
			reinterpret_cast<LPVOID>(&Detour),
			reinterpret_cast<LPVOID *>(&g_original)) != MH_OK ||
		MH_EnableHook(reinterpret_cast<LPVOID>(&Target)) != MH_OK) {
		std::fprintf(stderr, "FAIL: could not install real MinHook detour\n");
		return 1;
	}
	g_entry = &Target;
	HANDLE worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
	if (!worker) {
		std::fprintf(stderr, "FAIL: could not start detour worker\n");
		return 1;
	}
	const bool entered = WaitForSingleObject(g_entered, 5000) == WAIT_OBJECT_0;
	const MH_STATUS rollback = entered ? DisableUnpublishedHooks() : MH_UNKNOWN;
	const bool retained = entered && ExecutableTrampoline(g_original);
	InterlockedExchange(&g_callOriginal, retained ? 1 : 0);
	SetEvent(g_resume);
	const bool joined = WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0;
	DWORD result = 0;
	if (joined)
		GetExitCodeThread(worker, &result);
	const bool entryRestored = joined && Target(3) == 20;
	const MH_STATUS cleanup = MH_Uninitialize();
	CloseHandle(worker);
	CloseHandle(g_resume);
	CloseHandle(g_entered);
	if (!entered || rollback != MH_OK || !retained || !joined ||
		result != 21 || !entryRestored || cleanup != MH_OK) {
		std::fprintf(stderr,
			"FAIL: entered=%d rollback=%d retained=%d joined=%d result=%lu entry=%d cleanup=%d\n",
			entered, rollback, retained, joined, result, entryRestored, cleanup);
		return 1;
	}
	std::puts("PASS: in-flight detour called a retained original trampoline after rollback");
	return 0;
}
