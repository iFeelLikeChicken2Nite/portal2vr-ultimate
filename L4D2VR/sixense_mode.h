#pragma once

#include "sixense_api.h"
#include "sixense_bridge.h"

// Portal2VR's side of the Sixense MotionPack support: the VR thread publishes
// Hydra-shaped controller data here, and the sixense.dll proxy in the
// MotionPack's bin folder reads it through Portal2VR_GetSixenseApi().
namespace SixenseMode {

SixenseBridge::History &History();
SixenseBridge::VibrationQueue &Vibrations();

}

extern "C" __declspec(dllexport) const Portal2VRSixenseApi *P2VR_SIXENSE_CALL Portal2VR_GetSixenseApi();
