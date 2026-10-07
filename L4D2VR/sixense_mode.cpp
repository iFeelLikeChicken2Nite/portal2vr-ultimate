#include "sixense_mode.h"

namespace SixenseMode {

SixenseBridge::History &History()
{
    static SixenseBridge::History history;
    return history;
}

SixenseBridge::VibrationQueue &Vibrations()
{
    static SixenseBridge::VibrationQueue vibrations;
    return vibrations;
}

}

namespace {

int P2VR_SIXENSE_CALL GetData(int which, int indexBack, void *data)
{
    return SixenseMode::History().GetData(which, indexBack,
        static_cast<SixenseBridge::LegacyControllerData *>(data));
}

int P2VR_SIXENSE_CALL GetAllData(int indexBack, void *data)
{
    return SixenseMode::History().GetAllData(indexBack,
        static_cast<SixenseBridge::LegacyAllControllerData *>(data));
}

int P2VR_SIXENSE_CALL IsControllerEnabled(int which)
{
    return SixenseMode::History().IsEnabled(which) ? 1 : 0;
}

int P2VR_SIXENSE_CALL GetNumActiveControllers()
{
    return SixenseMode::History().ActiveControllers();
}

int P2VR_SIXENSE_CALL TriggerVibration(int which, int duration100ms, int)
{
    return SixenseMode::Vibrations().Request(which, duration100ms);
}

const Portal2VRSixenseApi kApi{
    P2VR_SIXENSE_API_VERSION, sizeof(Portal2VRSixenseApi),
    GetData, GetAllData, IsControllerEnabled, GetNumActiveControllers, TriggerVibration,
};

}

extern "C" __declspec(dllexport) const Portal2VRSixenseApi *P2VR_SIXENSE_CALL Portal2VR_GetSixenseApi()
{
    return &kApi;
}
