// dllmain.cpp : Defines the entry point for the DLL application.
#include <Windows.h>
#include <iostream>
#include "game.h"
#include "hooks.h"
#include "vr.h"
#include "sdk.h"
#include "logger.h"
#include "runtime_publication.h"
#include <exception>
#include <string>

DWORD WINAPI InitL4D2VR(HMODULE hModule)
{
    Logger::Initialize(hModule);
// Release if buggy, so we'll be releasing the debug binary
#ifdef _DEBUG
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
#endif

    // Make sure -insecure is used
    LPWSTR *szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (!szArglist) {
        Game::errorMsg("Unable to read Portal 2 launch options; Portal2VR initialization stopped.");
        return 0;
    }
    bool insecureEnabled = false;
    for (int i = 0; i < nArgs; ++i)
    {
        if (wcscmp(szArglist[i], L"-insecure") == 0)
            insecureEnabled = true;
    }
    LocalFree(szArglist);

    if (!insecureEnabled) {
        Logger::Write("Missing -insecure launch option; VR hooks disabled for this launch.");
        MessageBoxA(nullptr,
            "Portal2VR expects the -insecure launch option. VR initialization has been skipped; Portal 2 will continue without Portal2VR hooks.",
            "Portal2VR launch option", MB_OK | MB_ICONWARNING);
        return 0;
    }

    Game *candidate = nullptr;
    try {
        candidate = new Game();
        if (candidate->Initialize() &&
            Portal2VRRuntime::PublishInitialized(g_Game, candidate))
            return 0;
    } catch (const std::exception &error) {
        Game::errorMsg((std::string("Portal2VR initialization failed: ") + error.what()).c_str());
    } catch (...) {
        Game::errorMsg("Portal2VR initialization failed with an unexpected error.");
    }
    delete candidate;
    Logger::Write("Portal2VR initialization stopped; DXVK proxy remains loaded.");

    return 0;
}



BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
            if (HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitL4D2VR, hModule, 0, NULL))
                CloseHandle(thread);
            else
                OutputDebugStringA("Portal2VR: failed to start initialization thread\n");
            break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


