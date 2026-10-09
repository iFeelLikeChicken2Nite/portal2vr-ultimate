#include "game.h"
#include <Windows.h>
#include <iostream>
#include "sdk.h"
#include "vr.h"
#include "hooks.h"
#include "offsets.h"
#include "sigscanner.h"
#include "logger.h"
#include "game_modules.h"
#include "../sixense_proxy/intel_camera_patch.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <winver.h>

#pragma comment(lib, "Version.lib")

static std::string ModuleVersion(const char *path)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeA(path, &ignored);
    if (!size) return "version unavailable";
    std::vector<char> data(size);
    if (!GetFileVersionInfoA(path, 0, size, data.data())) return "version unavailable";
    VS_FIXEDFILEINFO *info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueA(data.data(), "\\", reinterpret_cast<void **>(&info), &infoSize) ||
        !info || infoSize < sizeof(VS_FIXEDFILEINFO)) return "version unavailable";
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
           std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
           std::to_string(LOWORD(info->dwFileVersionLS));
}

Game::Game() = default;

Game::~Game()
{
    delete m_Hooks;
    delete m_VR;
    delete m_Offsets;
}

// sixense_intel_enabled is replicated, and server_sixense.dll registers its
// own copy defaulting to "1"; once a map loads the client takes the server's
// value, which skips the MotionPack's Hydra input frame (and re-arms the
// Intel camera code). The sixense.dll proxy only reaches the client copy, so
// switch the server's off here, after its constructors have run.
static void DisableServerIntelCamera(uintptr_t serverBase)
{
    const auto *image = reinterpret_cast<const std::uint8_t *>(serverBase);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(image);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(image + dos->e_lfanew);
    const std::size_t size = nt->OptionalHeader.SizeOfImage;
    const std::size_t at = IntelCameraPatch::FindEnabledDefaultOperand(image, size, static_cast<std::uint32_t>(serverBase));
    // push flags; push <default>; push <name>; mov ecx, <ConVar>
    if (at == IntelCameraPatch::kNotFound || at + 14 > size || image[at + 9] != 0xB9) {
        Logger::Write("Sixense: server sixense_intel_enabled not found; Hydra input may stay off in maps");
        return;
    }
    std::uint32_t object, name;
    std::memcpy(&object, image + at + 10, 4);
    std::memcpy(&name, image + at + 5, 4);
    auto *conVar = reinterpret_cast<std::uint8_t *>(static_cast<uintptr_t>(object));
    std::uint32_t storedName;
    std::memcpy(&storedName, conVar + 0x0C, 4);
    if (storedName != name) {
        Logger::Write("Sixense: server sixense_intel_enabled layout unexpected; left unchanged");
        return;
    }
    std::uint8_t *parent = *reinterpret_cast<std::uint8_t **>(conVar + 0x1C);
    if (!parent) parent = conVar;
    static const char kOff[] = "0";
    *reinterpret_cast<const char **>(parent + 0x20) = kOff; // m_pszDefaultValue
    if (char *value = *reinterpret_cast<char **>(parent + 0x24))   // m_pszString
        if (value[0] && !value[1]) value[0] = '0';
    *reinterpret_cast<float *>(parent + 0x2C) = 0.0f;               // m_fValue
    *reinterpret_cast<int *>(parent + 0x30) = 0;                    // m_nValue
    Logger::Write("Sixense: server sixense_intel_enabled set to 0 (Hydra input instead of Intel camera)");
}

// Native VR grab: player_held_object_distance sets how far ahead a held
// object floats. Keep a pointer to its live float so the right stick can
// change it while holding (ConVar m_fValue at +0x2C of the parent).
void Game::FindHeldObjectDistance()
{
    const auto *image = reinterpret_cast<const std::uint8_t *>(m_BaseServer);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(
        image + reinterpret_cast<const IMAGE_DOS_HEADER *>(image)->e_lfanew);
    const std::size_t size = nt->OptionalHeader.SizeOfImage;
    const auto find = [&](const char *name) -> float * {
        const std::size_t at = IntelCameraPatch::FindConVarRegistration(
            image, size, static_cast<std::uint32_t>(m_BaseServer), name);
        if (at == IntelCameraPatch::kNotFound) return nullptr;
        std::uint32_t object;
        std::memcpy(&object, image + at + 10, 4);
        auto *conVar = reinterpret_cast<std::uint8_t *>(static_cast<uintptr_t>(object));
        std::uint8_t *parent = *reinterpret_cast<std::uint8_t **>(conVar + 0x1C);
        if (!parent) parent = conVar;
        return reinterpret_cast<float *>(parent + 0x2C);
    };
    // Portal 2 holds objects on the viewmodel path (player_held_object_
    // use_view_model -1), which uses the _vm distance; drive both.
    m_HeldObjectDistance = find("player_held_object_distance");
    m_HeldObjectDistanceVM = find("player_held_object_distance_vm");
    if (!m_HeldObjectDistance || !m_HeldObjectDistanceVM) {
        m_HeldObjectDistance = m_HeldObjectDistanceVM = nullptr;
        Logger::Write("Native grab: held object distance convars not found; stick distance disabled");
        return;
    }
    m_HeldObjectDistanceDefault = *m_HeldObjectDistance;
    m_HeldObjectDistanceVMDefault = *m_HeldObjectDistanceVM;
    Logger::Write("Native grab: held object distance found (default " +
        std::to_string(m_HeldObjectDistanceDefault) + ", viewmodel " +
        std::to_string(m_HeldObjectDistanceVMDefault) + "); right stick up/down sets it while holding");
}

bool Game::Initialize()
{
    // The Sixense MotionPack loads client_sixense.dll instead of client.dll;
    // settle which game variant is running before any module is named.
    const auto variantStart = GetTickCount64();
    GameModules::Variant variant = GameModules::Variant::Unknown;
    const auto sixenseBesideClient = [] {
        const HMODULE client = GetModuleHandleA(GameModules::kStockClient);
        char path[MAX_PATH];
        const DWORD length = client ? GetModuleFileNameA(client, path, MAX_PATH) : 0;
        if (!length || length >= MAX_PATH) return false;
        std::string sibling(path, length);
        sibling = sibling.substr(0, sibling.find_last_of("\\/") + 1) + GameModules::kSixenseClient;
        return GetFileAttributesA(sibling.c_str()) != INVALID_FILE_ATTRIBUTES;
    };
    while ((variant = GameModules::DetectVariant([](const char *name) {
               return GetModuleHandleA(name) != nullptr; }, sixenseBesideClient())) == GameModules::Variant::Unknown) {
        if (GetTickCount64() - variantStart > 30000) {
            errorMsg("Timed out waiting for client.dll or client_sixense.dll");
            return false;
        }
        Sleep(50);
    }
    GameModules::SetVariant(variant);
    Logger::Write(variant == GameModules::Variant::Sixense ?
        "Game variant: Sixense MotionPack (client_sixense.dll/server_sixense.dll)" :
        "Game variant: stock Portal 2 (client.dll/server.dll)");

    const struct Module { const char *name; uintptr_t *base; } modules[] = {
        { GameModules::Resolve("client.dll"), &m_BaseClient }, { "engine.dll", &m_BaseEngine },
        { "materialsystem.dll", &m_BaseMaterialSystem }, { GameModules::Resolve("server.dll"), &m_BaseServer },
        { "vgui2.dll", &m_BaseVgui2 }
    };
    for (const auto &module : modules) {
        const auto start = GetTickCount64();
        while (!(*module.base = reinterpret_cast<uintptr_t>(GetModuleHandleA(module.name)))) {
            if (GetTickCount64() - start > 30000) {
                errorMsg((std::string("Timed out waiting for ") + module.name).c_str());
                return false;
            }
            Sleep(50);
        }
        char path[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(*module.base), path, MAX_PATH);
        Logger::Write(std::string("Loaded ") + module.name + ": " + path +
                      " (" + ModuleVersion(path) + ")");
    }
    if (variant == GameModules::Variant::Sixense)
        DisableServerIntelCamera(m_BaseServer);
    FindHeldObjectDistance();
    const auto surfaceStart = GetTickCount64();
    while (!GetModuleHandleA("vguimatsurface.dll")) {
        if (GetTickCount64() - surfaceStart > 30000) {
            errorMsg("Timed out waiting for vguimatsurface.dll");
            return false;
        }
        Sleep(50);
    }
    char surfacePath[MAX_PATH]{};
    GetModuleFileNameA(GetModuleHandleA("vguimatsurface.dll"), surfacePath, MAX_PATH);
    Logger::Write(std::string("Loaded vguimatsurface.dll: ") + surfacePath +
                  " (" + ModuleVersion(surfacePath) + ")");

    m_ClientEntityList = (IClientEntityList *)GetInterface("client.dll", "VClientEntityList003");
    m_EngineTrace = (IEngineTrace *)GetInterface("engine.dll", "EngineTraceClient004");
    m_EngineClient = (IEngineClient *)GetInterface("engine.dll", "VEngineClient015");
    m_DebugOverlay = (IVDebugOverlay *)GetInterface("engine.dll", "VDebugOverlay004");
    m_MaterialSystem = (IMaterialSystem *)GetInterface("MaterialSystem.dll", "VMaterialSystem080");
    IMaterialSystem::slotShift = variant == GameModules::Variant::Sixense ? -1 : 0;
    m_EngineViewRender = (IViewRender *)GetInterface("engine.dll", "VEngineRenderView013");
    m_ModelInfo = (IModelInfo *)GetInterface("engine.dll", "VModelInfoClient004");
    m_ModelRender = (IModelRender *)GetInterface("engine.dll", "VEngineModel016");
    m_VguiInput = (IInput *)GetInterface("vgui2.dll", "VGUI_InputInternal001");
    m_VguiSurface = (ISurface *)GetInterface("vguimatsurface.dll", "VGUI_Surface031");

    if (!m_ClientEntityList || !m_EngineTrace || !m_EngineClient || !m_MaterialSystem ||
        !m_EngineViewRender || !m_ModelInfo || !m_ModelRender ||
        !m_VguiInput || !m_VguiSurface) {
        errorMsg("A required Portal 2 Source interface is unavailable; see portal2vr.log.");
        return false;
    }

    m_Offsets = new Offsets();
    if (!m_Offsets->Validate()) {
        errorMsg("A required Portal 2 symbol was not resolved; see portal2vr.log.");
        return false;
    }

    m_VR = new VR(this);
    if (!m_VR->m_IsInitialized)
        return false;
    m_Hooks = new Hooks(this);
    m_Hooks->Initialize();
    if (!m_Hooks->m_Ready) {
        errorMsg("A required Portal 2 hook could not be installed; see portal2vr.log.");
        return false;
    }

    m_Initialized = true;
    Logger::Write("Portal2VR initialization complete.");
    return true;
}

void *Game::GetInterface(const char *dllname, const char *interfacename)
{
    dllname = GameModules::Resolve(dllname);
    HMODULE module = GetModuleHandleA(dllname);
    if (!module) return nullptr;
    tCreateInterface CreateInterface = (tCreateInterface)GetProcAddress(module, "CreateInterface");
    if (!CreateInterface) return nullptr;

    int returnCode = 0;
    void *createdInterface = CreateInterface(interfacename, &returnCode);

    Logger::Write(std::string("Interface ") + dllname + "/" + interfacename +
                  (createdInterface ? " OK" : " MISSING"));

    return createdInterface;
}

void Game::errorMsg(const char *msg)
{
    Logger::Write(std::string("Initialization error: ") + msg);
    MessageBox(0, msg, "Portal2VR", MB_ICONERROR | MB_OK);
}

CBaseEntity *Game::GetClientEntity(int entityIndex)
{
    return (CBaseEntity *)(m_ClientEntityList->GetClientEntity(entityIndex));
}

char *Game::getNetworkName(uintptr_t *entity)
{
    uintptr_t *IClientNetworkableVtable = (uintptr_t *)*(entity + 0x8);
    uintptr_t *GetClientClassPtr = (uintptr_t *)*(IClientNetworkableVtable + 0x8);
    uintptr_t *ClientClassPtr = (uintptr_t *)*(GetClientClassPtr + 0x1);
    char *m_pNetworkName = (char *)*(ClientClassPtr + 0x8);
    int classID = (int)*(ClientClassPtr + 0x10);
    std::cout << "ClassID: " << classID << std::endl;
    return m_pNetworkName;
}

void Game::ClientCmd(const char *szCmdString)
{
    m_EngineClient->ClientCmd(szCmdString);
}

void Game::ClientCmd_Unrestricted(const char *szCmdString)
{
    m_EngineClient->ClientCmd_Unrestricted(szCmdString);
}


