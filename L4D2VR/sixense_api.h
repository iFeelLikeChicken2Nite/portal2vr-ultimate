#pragma once

// In-process contract between Portal2VR (d3d9.dll) and the sixense.dll proxy
// that replaces the Razer Hydra SDK in the Sixense MotionPack's bin folder.
// The proxy never talks to a VR runtime itself: it asks d3d9.dll for this
// table and serves the game from Portal2VR's own OpenVR session.

#include <cstdint>

#if defined(_MSC_VER)
#define P2VR_SIXENSE_CALL __cdecl
#else
#define P2VR_SIXENSE_CALL
#endif

#define P2VR_SIXENSE_API_EXPORT "Portal2VR_GetSixenseApi"
#define P2VR_SIXENSE_API_VERSION 1u

extern "C" {

// Data pointers use the 96-byte legacy sixenseControllerData layout and the
// 384-byte four-controller bundle that the MotionPack's game DLLs were built
// against (see SixenseBridge::LegacyControllerData).
struct Portal2VRSixenseApi {
    std::uint32_t version;
    std::uint32_t size;
    int (P2VR_SIXENSE_CALL *GetData)(int which, int indexBack, void *controllerData);
    int (P2VR_SIXENSE_CALL *GetAllData)(int indexBack, void *allControllerData);
    int (P2VR_SIXENSE_CALL *IsControllerEnabled)(int which);
    int (P2VR_SIXENSE_CALL *GetNumActiveControllers)();
    int (P2VR_SIXENSE_CALL *TriggerVibration)(int which, int duration100ms, int pattern);
};

typedef const Portal2VRSixenseApi *(P2VR_SIXENSE_CALL *Portal2VRGetSixenseApiFn)();

}
