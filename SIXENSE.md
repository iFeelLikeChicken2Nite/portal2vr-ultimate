# The 6DOF Perceptual Pack: Portal 2 Sixense Perceptual Pack in VR

Work in progress: running the Steam app "Portal 2 Sixense Perceptual Pack"
(247120) under Portal2VR, with the headset rendering the game and the VR
controllers driving its motion mechanics.

Two different Sixense releases share this install and its game DLLs
(`client_sixense.dll`, `server_sixense.dll`):

- **Perceptual Pack** (2013, Intel Perceptual Computing camera): the seven
  `sp_a1_sx_perc_*` levels, played with hand gestures and head tracking.
- **MotionPack** (Razer Hydra): the `sp_a2_sx_*` to `sp_a8_sx_*` levels,
  played with the Hydra; some of them also carry Intel-camera hints in this
  build.

Below, "Hydra emulation" means feeding the shared Sixense input code fake
Hydra data; the `native-vr` branch replaces that with VR-native mechanics.

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
`steamapps\common\Portal 2 Sixense Perceptual Pack` and ships the levels of
both releases (Perceptual Pack `sp_a1_sx_perc_*`, MotionPack `sp_a2_sx_*`
to `sp_a8_sx_*`), with the shared game DLLs in `portal2_sixense\bin`.

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

Played in a Quest 2 (Steam Link, RTX 2060) on 2026-10-07: sp_a2_sx_intro
renders in stereo, Portal2VR's controller aim places portals where the right
controller points, and the portal gun sits in the hand
(`kSixenseViewmodelPositionOffset` in `vr.cpp`). This uses `SixenseMode=Off`:
with `Auto` and the game's `sixense_mode 1`, the trigger reached the emulated
Hydra but the game did not react (no calibration screen either), so the
MotionPack's own free aim is still open.

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
7. The portal gun's blue glow effects do not line up with the gun model
   after the viewmodel offset (low priority; `ExperimentalViewmodelAlignment`
   is the existing option aimed at model/effect alignment, untested here).

## native-vr branch: rebuilding the mechanics on VR controllers

Decision (2026-10-09): drop the Hydra emulation as the way to play and
rebuild the pack's mechanics directly on the VR controllers. The emulation
state is preserved on branch `sixense-vr-hydra-backup` / tag
`hydra-backup-2026-10-09` (beb406a). On this branch the game runs with
`SixenseMode=Off` and `sixense_mode 0`; the `sixense.dll` proxy stays only
for the Intel-camera startup patches, and Portal2VR's own aim, fire and
grab are active.

### Mechanics inventory

One mod covers both packs' levels. Each mechanic is tagged with where it is
used: **P** = Perceptual Pack levels (`sp_a1_sx_perc_*`), **M** = MotionPack
levels (`sp_a2_sx_*` to `sp_a8_sx_*`). Sources: the server's `sixense_*`
convars and each map's instructor hints and `sixense_*` commands.

| Mechanic | Used in | What the pack does | Native VR plan |
|---|---|---|---|
| Distance grab | P, M (all) | Held object floats ahead; Hydra reach or hand depth sets distance (`SIXENSE_HINT_1TO1_FURTHER_*`, `_1TO1_DEPTH`) | **Done:** Portal2VR aims the held object along the right controller; right stick up/down changes `player_held_object_distance` while holding (`NativeHoldDistance*`) |
| 1:1 manipulation | P (`INTEL_1TO1`, `_1TO1_ROTATE`, `1TO1_STOP`), M (sp_a2_sx_reaching, tutorials) | Move/rotate the held object with the hand (`sixense_one_to_one*`, `sixense_auto_one_to_one_*`) | Held object follows the controller's rotation (grab controller target angles) |
| Ratcheting | M (`SIXENSE_HINT_RATCHET`, ratchet lessons) | Hold a button to re-grip without turning the object (`sixense_ratchet*`) | Ignore wrist rotation while a button is held |
| Cube scaling | P (sp_a1_sx_perc_scaling, `INTEL_SCALING`, `_MULTI_AXIS`), M (sp_a8_sx_mass, `SCALING_REACH`, `_RAISE`) | Hold SCALE and move the other hand to grow/shrink, per axis; double-tap resets (`sixense_scaling_*`) | Two-hand gesture; investigate the Intel path first (`sixense_intel_scale_key`, `sixense_intel_reset_scale_key` were keyboard-driven) |
| Turret scaling | M (sp_a8_sx_turrets) | Same for turrets (`sixense_scaling_turret_*`) | Same as cube scaling |
| Portal tweaking / surfing | P (sp_a1_sx_perc_surfing, `INTEL_SURF_GRAB`, `_SURF_DROP`), M (sp_a8_sx_tb_surf, sp_a4_sx_thru_portals) | Grab a placed portal, move/rotate it (`sixense_portal_tweaking_*`) | Investigate `sixense_intel_portal_tweak_key` and the tweaking server code |
| Throwing | M | Release with hand velocity (`sixense_throw_*`) | Controller velocity on release |
| Hand calibration, look/spin with the hand | P (`INTEL_CALIBRATION_INSTRUCTION`, "Align Hands", `HINT_LOOK*`) | Camera calibration; aiming the view with the hand | Not needed in VR (tracked controllers, headset view, stick turn); skip or auto-complete those lessons |
| Portal device pickup, posture, door, jump, crouch, gel and firing hints | P, M | Standard actions | Standard Portal2VR controls |

Map logic in both sets toggles the game's lessons and gestures
(`sixense_disable_gestures`, `sixense_disable_lessons`,
`sixense_hide_video_hint`); with Hydra input off these are inert.

Order: distance grab (done), then 1:1 rotation and ratchet, then scaling and
portal tweaking once their server-side entry points are mapped.
