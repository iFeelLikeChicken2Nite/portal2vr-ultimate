# The 6DOF Perceptual Pack: Portal 2 Sixense Perceptual Pack in VR

Work in progress: running the Sixense MotionPack (the Razer Hydra DLC, with
the Perceptual Pack levels from the community preservation package) under
Portal2VR, with the headset rendering the game and your VR controllers
standing in for the Hydra.

Nothing here has been run in a headset yet. Read **Status** before trying it.

## How it fits together

The MotionPack ships its own game DLLs, `client_sixense.dll` and
`server_sixense.dll`, and reads the Hydra through `sixense.dll` in
`portal2_sixense\bin`. Its mechanics (free aim, one-to-one grabbing, portal
surfing, scaling cubes) are compiled into those DLLs, so they need
Hydra-shaped controller data to work.

- **Module aliasing** (`L4D2VR/game_modules.h`): Portal2VR detects
  `client_sixense.dll` and scans/hooks the MotionPack's DLLs wherever it used
  to name `client.dll`/`server.dll`. Hooks pinned to the stock build's
  SHA-256 (`verified_portal_symbols.h`) skip themselves with a log warning.
- **Hydra emulation** (`L4D2VR/sixense_bridge.h`, `sixense_mode.cpp`): each
  frame Portal2VR converts its own OpenVR controller poses and a dedicated
  `/actions/sixense` action set into the 96-byte legacy `sixenseControllerData`
  the MotionPack was built against, kept in the SDK's 50-deep history. The
  pose math matches the Hydra-OpenVR shims people already play the MotionPack
  with (millimetres, column-major rotation, -45 degree coil offset in the
  controller frame); `tests/sixense_bridge_tests.cpp` pins it.
- **sixense.dll proxy** (`sixense_proxy/`): replaces the Hydra SDK DLL and
  forwards every call to Portal2VR's `d3d9.dll`. It never opens its own VR
  session, which is what made stacking a Hydra shim on top of Portal2VR
  unstable. Without Portal2VR loaded the controllers read as disconnected.
- **Intel camera crash fix** (`sixense_proxy/intel_camera_patch.h`): the
  Steam build crashes at the Valve logo without its Creative Senz3D camera
  (`client_sixense.dll`+0x402B87, a null capture device). Before
  `client_sixense.dll` runs, the proxy makes `sixense_intel_enabled` default
  to `0` and, because the camera "reconnect" paths ignore that switch (they
  crashed the same way at +0x3FFAB0), turns the allocation check at every
  site that creates the camera object into the game's own "no camera"
  branch. A per-frame check that no convar reaches then raised "Connect
  Senz3D camera to computer" in game and kept maps paused behind it; the
  proxy skips that prompt too. Launch with `-p2vr_intel_camera` to leave the
  camera code alone.

While you are in gameplay (not in a menu), the Sixense action set outranks
Portal2VR's main set, so controls bound to Hydra actions go to the
MotionPack's own input code instead of Portal2VR's walk/turn/fire actions.

## Default controller layout

Follows the community Hydra-OpenVR shim so muscle memory carries over. Every
action can be rebound in SteamVR's controller binding UI under
"Sixense MotionPack (Razer Hydra)".

| Hydra | Touch / Cosmos | Index |
|---|---|---|
| Trigger | trigger | trigger |
| Joystick / click | thumbstick / click | thumbstick / click |
| Bumper | grip | grip (force) |
| Start | left Y | left B |
| Button 1 | right B | right B |
| Button 2 | X / A | A (either hand) |
| Buttons 3 / 4 | unbound | trackpad north / south click |

## Config

`VR\config.txt`:

- `SixenseMode=Auto` emulates the Hydra when `client_sixense.dll` is running;
  `Off` disables it (the MotionPack then sees no controllers).
- `SixenseHandSpace=Tracking` sends raw tracking-space poses like the
  flat-screen shims. `HeadYaw` re-expresses hands relative to the headset's
  position and heading, so the virtual Hydra base turns with you. Which one
  makes free aim line up best in the headset is still to be found out.

## Manual install (the launcher does not handle the MotionPack yet)

The Steam app "Portal 2 Sixense Perceptual Pack" (247120) installs to
`steamapps\common\Portal 2 Sixense Perceptual Pack` and contains both the
Perceptual Pack levels (`sp_a1_sx_perc_*`) and the MotionPack levels
(`sp_a2_sx_*` to `sp_a8_sx_*`), with the game DLLs in `portal2_sixense\bin`.

1. Install it from Steam. Unmodified, it crashes at startup on any PC
   without the Intel camera; the proxy from step 3 fixes that.
2. Build `l4d2vr.sln` Release/x86 (on Visual Studio 2026 add
   `/p:PlatformToolset=v145`). In the install folder:
   - copy `Release\d3d9.dll` and `thirdparty\openvr\bin\win32\openvr_api.dll`
     into `bin` (next to `engine.dll`);
   - copy `L4D2VR\config.txt`, `L4D2VR\portal2vr_capsule_main.png` and
     `L4D2VR\SteamVRActionManifest` into a new `VR` folder in the install
     root, and `L4D2VR\manifest_sixense.vrmanifest` there as
     `manifest.vrmanifest` (without it Portal2VR stops with
     `VRApplicationError_InvalidManifest`). It registers app 247120, so the
     stock `manifest.vrmanifest` (app 620) stays with your Portal 2 install.
3. Rename `portal2_sixense\bin\sixense.dll` to `sixense.dll.orig`, then copy
   `Release\sixense\sixense.dll` in its place.
4. Start SteamVR, then launch from Steam with launch options
   `-insecure -window -novid +mat_motion_blur_percent_of_screen_max 0 +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0 +mat_grain_scale_override 0 -width 1280 -height 720`
   (add `-game portal2_sixense` if the stock Portal 2 menu appears).

To undo: delete `bin\d3d9.dll`, `bin\openvr_api.dll` and `VR`, and rename
`sixense.dll.orig` back.

## Status

Done: module aliasing, Hydra data conversion, proxy, action set and default
bindings, config keys, unit tests (pass as a 32-bit Windows build). The
changed Portal2VR files pass a MinGW syntax check; the MSVC build and every
in-game behaviour are unverified.

Open questions, in the order a first test session should answer them:

1. Does Portal2VR start inside the MotionPack? `bin\portal2vr.log` lists the
   game variant and each signature. On the Steam 247120 build the stock
   patterns missed `TraceFirePortalServer`, `CWeaponPortalgun_FirePortal` and
   `UpdateObject`, and `UpdateObjectVM`'s matched `ComputeError` instead;
   `offsets.h` now carries a second RVA and pattern for those four, used only
   when `client_sixense.dll` is running. Every other accepted pattern was
   checked against that build; the ones far from their stock RVA were
   confirmed by their callers. Still unverified in game.
2. Does the MotionPack see two controllers and pass its calibration screen?
3. Which `SixenseHandSpace` lines free aim up with what you see?
4. Portal2VR's controller-aimed portal firing and the MotionPack's free aim
   both want the gun. Decide which owns aiming in Sixense mode.
5. The Perceptual Pack levels were built for camera gestures; the
   preservation package already remaps them to Hydra controls, so they ride
   on the same path.
6. **Non-Emotional Manipulation**, the MotionPack's six-map co-op campaign
   (`mp_coop_sx_*`), is not in the Steam 247120 install (no co-op maps
   there), so it has to come from elsewhere. It runs on the same
   `client_sixense.dll`, so the Hydra
   emulation covers it. Co-op itself is the risk: portal2vr-ultimate makes no
   co-op claim, and both players need the MotionPack. Test it with two
   machines after single player works, starting with one VR player and one
   flat-screen player.
