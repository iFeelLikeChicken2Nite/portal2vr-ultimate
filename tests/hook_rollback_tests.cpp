#include <windows.h>
#include <cstdio>

#include "../L4D2VR/hook_startup_rollback.h"

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
} // namespace

int main()
{
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
