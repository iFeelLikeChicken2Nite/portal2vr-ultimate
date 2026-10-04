#pragma once

#include "../thirdparty/minhook/include/MinHook.h"

// Startup failure may leave a callback inside a detour. Its fOriginal pointer
// remains callable until the callback exits, so rollback must retain MinHook's
// trampoline allocation until process exit.
inline MH_STATUS DisableUnpublishedHooks()
{
	return MH_DisableHook(MH_ALL_HOOKS);
}
