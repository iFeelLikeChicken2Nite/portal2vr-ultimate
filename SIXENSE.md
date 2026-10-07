# Portal 2 Sixense MotionPack in VR

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

1. Install the MotionPack preservation package next to Portal 2 and check it
   starts on its own.
2. Build `l4d2vr.sln` Release/x86. Copy `Release\d3d9.dll` into the
   MotionPack's `bin` folder (next to its `engine.dll`) and the `VR` folder
   contents (config and `SteamVRActionManifest`) into a `VR` folder in the
   MotionPack root, as the launcher does for Portal 2.
3. Back up `portal2_sixense\bin\sixense.dll`, then copy
   `Release\sixense\sixense.dll` over it.
4. Start SteamVR, then launch the MotionPack's `portal2.exe` the way its own
   shortcut does (for example `-game portal2_sixense`), adding `-insecure`
   and your usual Portal2VR video options.

## Status

Done: module aliasing, Hydra data conversion, proxy, action set and default
bindings, config keys, unit tests (pass as a 32-bit Windows build). The
changed Portal2VR files pass a MinGW syntax check; the MSVC build and every
in-game behaviour are unverified.

Open questions, in the order a first test session should answer them:

1. Does Portal2VR start inside the MotionPack? `bin\portal2vr.log` lists the
   game variant and each signature. The MotionPack's DLLs are an older build
   than stock Portal 2, so required signatures may miss; those need new
   patterns from the MotionPack's own `client_sixense.dll`,
   `server_sixense.dll` and `engine.dll`.
2. Does the MotionPack see two controllers and pass its calibration screen?
3. Which `SixenseHandSpace` lines free aim up with what you see?
4. Portal2VR's controller-aimed portal firing and the MotionPack's free aim
   both want the gun. Decide which owns aiming in Sixense mode.
5. The Perceptual Pack levels were built for camera gestures; the
   preservation package already remaps them to Hydra controls, so they ride
   on the same path.
