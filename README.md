<div align="center">
  <p>
    <a align="center" href="https://ultralytics.com/yolov5" target="_blank">
      <img width="auto" src="https://raw.githubusercontent.com/Gistix/portal2vr/main/imgs/logo.png"></a>
  </p>
</div>

# ![Portal 2 icon](imgs/icon.jpg "Portal 2 icon") Portal 2 VR
Portal2VR expects Portal 2 to be started with `-insecure`. If it is missing, VR hooks are skipped and the game is not terminated by Portal2VR. This is a compatibility requirement of this mod, not a statement about anti-cheat policy.
This game contains flashing lights and fast motion sequences.

## Portal 2 VR Mod First 20 Minutes (Youtube Video)
[![IMAGE ALT TEXT HERE](https://img.youtube.com/vi/nQZ601kEDFI/0.jpg)](https://www.youtube.com/watch?v=nQZ601kEDFI)

## Things that work
* Singleplayer
* 6DoF VR view
* Motion controls for portal gun and grabbable objects
* Workshop content

## Things that need fixing
* Use the game's own haptic feedback
* In-game UI and pause menu are broken
* 6DoF and Roomscale needs to be reimplemented
* CPU is underutilized

## How to use
1. Download [Portal2VR.zip](https://github.com/Gistix/portal2vr/releases) and extract the files to your Portal 2 directory (steamapps\common\Portal 2)
2. Connect your headset, then launch Portal 2 with these launch options:
   
   ``` -insecure -window -novid +mat_motion_blur_percent_of_screen_max 0 +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0 +mat_grain_scale_override 0 -width 1280 -height 720 ```

3. At the menu, feel free to change [these video settings](https://i.imgur.com/yYQMXs6.jpg).
4. Load into a chapter. 
5. To recenter the camera height, press down on the left stick. To see the HUD, aim the controller up or down.

## Troubleshooting
If you have no audio:
* Go to ```steamapps\common\Portal 2\portal2_dlc3``` and execute ```UpdateSoundCache.cmd```
  
If the game isn't loading in VR:
* Try opening SteamVR before the game
* Disable SteamVR theater in [Steam settings](https://external-preview.redd.it/1WdLExouo_YKhTGT6C5GGrOjeWO7qNdIdDRvIRBhw-0.png?auto=webp&s=0d4447a9d954e1ec15b2c010cf50eeabd51f4197)

If the game is stuttering, try: 
* Steam Settings -> Shader Pre-Caching -> Allow background processing of Vulkan shaders

If the game is crashing, try:
* Lowering video settings
* Disabling all add-ons then verifying integrity of game files
* Re-installing the game

## Build instructions
1. In this checkout, initialize dependencies with `git submodule update --init --recursive` (or clone with `--recurse-submodules`).
2. Install Visual Studio 2022 C++ build tools (MSVC v143 and the Windows 10/11 SDK). Open `l4d2vr.sln` and build the **x86** Release configuration; this is a 32-bit DLL. The solution's `x86` configuration maps to the project's `Win32` platform.
3. From a VS developer terminal, an equivalent command is `msbuild l4d2vr.sln /m /p:Configuration=Release /p:Platform=x86`.
4. The build normally leaves `d3d9.dll` under `Release/` and does **not** deploy to a game installation. To opt into deployment, set the MSBuild property `PORTAL2_DIR` to your own Portal 2 root, e.g. `/p:PORTAL2_DIR="D:\Games\Portal 2"`; the build then copies the DLL to its `bin` directory. The `VR` manifests, bindings and config still need to be present alongside the installation as expected by the existing release layout.
5. Isolated tests: `msbuild tests\stabilization_tests.vcxproj /p:Configuration=Release /p:Platform=Win32`, then run `tests\bin\stabilization_tests.exe`.

Portal2VR appends startup and error diagnostics to `portal2vr.log` beside its loaded `d3d9.dll`. The config file `VR/config.txt` is checked once per second while VR is running. Invalid values retain their last valid value and are logged. Accepted ranges are `TurnSpeed` 0.01–2, `SnapTurnAngle` 1–180 degrees, `VRScale` 1–200, `IPDScale` 0.5–1.5, `AimMode` 0–2, and `AntiAliasing` 0/2/4/8. An `AntiAliasing` change requires a game restart. To uninstall the mod, remove only the Portal2VR files that you installed, especially its `bin/d3d9.dll`; preserve any files that predated this mod.

The architecture audit and milestone boundaries are in [docs/architecture.md](docs/architecture.md) and [docs/roadmap.md](docs/roadmap.md). A build and host-side tests do not establish game or headset compatibility; those still require real Portal 2 and SteamVR testing.

## Based on
* [l4d2vr](https://github.com/sd805/l4d2vr)
  
## Utilizes code from
* [VirtualFortress2](https://github.com/PinkMilkProductions/VirtualFortress2)
* [gmcl_openvr](https://github.com/Planimeter/gmcl_openvr/)
* [dxvk](https://github.com/TheIronWolfModding/dxvk/tree/vr-dx9-rel)
* [source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013/)

## Support me
<a href="https://www.paypal.com/donate/?business=YL7TGWKPCC9H8&no_recurring=0&currency_code=USD"><img src="https://pics.paypal.com/00/s/MDAwNDljNmUtZWZiZS00ZTI1LWFiMTMtZTdhZmQ5NmU5ZDUx/file.PNG" alt="Donate Button" style="width:auto;height:100px;"></a>

