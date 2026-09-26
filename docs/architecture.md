# Portal2VR architecture baseline (M0)

This describes the upstream `310ac49` implementation as inspected on 2026-09-26. M1 changes are described in the roadmap and commit history. It is a static source audit, not evidence that a particular Portal 2 build or headset works.

## Bootstrap and dependencies

The output is a 32-bit `d3d9.dll`, combining the Portal2VR code and a modified DXVK D3D9 implementation. Placing it in Portal 2's `bin` directory causes the game's normal D3D9 load to bring in the proxy. `DllMain` starts an initialization thread. The original thread checked `-insecure`, then constructed the global `Game`; absent that flag it called `ExitProcess(0)`. `Game` waited for `client.dll`, `engine.dll`, `materialsystem.dll`, `server.dll`, and `vgui2.dll`, acquired Source `CreateInterface` objects (also from `vguimatsurface.dll`), constructed `Offsets`, `VR`, and `Hooks`, then marked itself initialized. The thread and module waits originally lacked a shutdown or timeout.

The build is the `l4d2vr.sln` x86 configuration mapped to the Win32 DLL project. It uses C++17, MinHook, bundled OpenVR 2.5.1 Win32 binaries, the pinned modified DXVK submodule, Vulkan import library and Windows SDK. The Source interfaces and entity structures in `L4D2VR/sdk` are locally declared ABI assumptions, not an official runtime API.

## Hook and symbol resolution

`Offsets` defines known module-relative offsets and byte signatures. `SigScanner::VerifyOffset` first checks the known offset; if it differs, it scans the loaded image and returns a replacement. MinHook creates trampolines at resolved addresses; the detours in `hooks.cpp` redirect rendering, movement, portal gun behavior, user command serialization, and HUD behavior. Static offsets, signature uniqueness, engine vtables, object layouts and calling conventions are all sensitive to Portal 2 updates. M1 adds a required/optional decision and bounds checks; it does not remove these assumptions.

| Hook / symbol | Module | Purpose | M1 policy | Main risk if absent or wrong |
| --- | --- | --- | --- | --- |
| `RenderView` | client | Render left/right eye and optional desktop pass | Required | No stereo rendering; wrong detour can crash render thread |
| `CreateMove` | client | Inject view angle and analog movement into `CUserCmd` | Required | Head/locomotion input invalid |
| `PlayerPortalled` | client | Observe portal traversal angle delta | Required in current pipeline | Orientation discontinuity or invalid detour |
| `TraceFirePortalServer` | server | Use controller ray for portal placement | Required | Portal firing direction wrong |
| `CWeaponPortalgun_FirePortal` | server | Override eye angle during firing | Required | Portal gun aim wrong |
| `CalcViewModelView` | client | Place viewmodel at controller pose | Required | Gun viewmodel displaced |
| `ProcessUsercmds` | server | Identify player for command decoding | Required | Wrong player state / serialization |
| `ReadUsercmd` | server | Decode VR controller pose in user command | Required | Invalid pose data / command stream |
| `WriteUsercmd` | client | Encode controller pose in user command | Required | Server lacks controller pose |
| `EyeAngles` | server | Override angle for held object and firing paths | Required | Controller aim desynchronizes |
| `VGui_Paint`, `PushRenderTargetAndViewport`, `PopRenderTargetAndViewport` | engine / materialsystem | Redirect and draw HUD | Required in current pipeline | Missing or corrupted UI rendering |
| `DrawSelf`, `ClipTransform`, `CHudCrosshair_ShouldDraw` | client | HUD, crosshair and laser placement | Required in current pipeline | UI placement or crosshair state wrong |
| `CreatePingPointer`, `PrecacheParticleSystem`, `Precache` | client / server | Optional laser pointer beam | Optional | Laser pointer disabled; main stereo view should continue |

Additional hooks alter FOV, shooting position, object handling and splitscreen behavior. Some defined offsets and hook declarations are unused; a definition alone is not evidence that a hook is installed. The complete enabled set is in `Hooks::Hooks` and `Hooks::initSourceHooks`.

## VR and rendering pipeline

`Portal 2 / Source -> Direct3D 9 -> modified DXVK -> Vulkan shared textures -> OpenVR compositor`.

`VR` obtains the OpenVR system, compositor, input and overlay interfaces, recommended render size and eye projection. `RenderView` draws left and right `CViewSetup` views into separate Source textures. DXVK exposes shared Vulkan texture data to OpenVR for compositor submission. A third render to the desktop back buffer is controlled by `RenderWindow`. HUD painting is redirected to a separate render target. The main menu uses an OpenVR overlay; menu pointer events are sent to the Windows input queue. `SteamVRActionManifest/action_manifest.json` and the controller binding JSON files define the existing OpenVR action set and bindings. The original code assumed every startup call succeeded and used a detached config watcher.

## Coordinate spaces and debt

OpenVR tracking matrices use meters and the compositor's tracking origin. `GetPoseData` maps position to Source-like axes as `(-z, -x, y)` and similarly remaps velocities and angles. `VRScale` converts tracked meter differences into Source units (default `43.2`). `m_Center` is the tracked HMD position captured by recentering; `m_HmdPosRelativeRaw` is HMD minus that center in tracking units. `m_HmdPosRelative` rotates that delta by `m_RotationOffset.y` then applies `VRScale`. `m_SetupOrigin` is the most recent Source view setup origin, used to anchor controller and eye views. The right controller position is first expressed relative to the HMD, pivoted for artificial turning and scaled; `GetRightControllerAbsPos` adds the setup origin and optionally HMD displacement. `m_RotationOffset` also affects HMD and controller angles and receives a portal traversal yaw correction. Eye separation comes from the OpenVR eye-to-head transform and `IPDScale`.

The names `Abs`, `Rel`, `Raw`, `Center`, and `SetupOrigin` do not fully encode units, origin or handedness. Rendering, aiming and Source command serialization share these mutable coordinates. Tracking origin is implicit. Stale controller poses were previously retained when OpenVR invalidated them. These are constraints for M2 design, not a reason for an M1 coordinate rewrite.

## Issue map

These links identify reported symptoms and likely code areas. They are not proof of a shared root cause.

| Area | Representative reports | Relevant component |
| --- | --- | --- |
| Launch / install / uninstall | [#25](https://github.com/Gistix/portal2vr/issues/25), [#93](https://github.com/Gistix/portal2vr/issues/93), [#183](https://github.com/Gistix/portal2vr/issues/183), [#191](https://github.com/Gistix/portal2vr/issues/191) | DLL deployment, bootstrap, dependencies |
| SteamVR startup | [#101](https://github.com/Gistix/portal2vr/issues/101), [#129](https://github.com/Gistix/portal2vr/issues/129), [#173](https://github.com/Gistix/portal2vr/issues/173) | OpenVR initialization and compositor |
| Controller pose / input | [#42](https://github.com/Gistix/portal2vr/issues/42), [#89](https://github.com/Gistix/portal2vr/issues/89), [#110](https://github.com/Gistix/portal2vr/issues/110), [#111](https://github.com/Gistix/portal2vr/issues/111) | Pose roles, action state and bindings |
| HUD / subtitles | [#20](https://github.com/Gistix/portal2vr/issues/20), [#39](https://github.com/Gistix/portal2vr/issues/39), [#72](https://github.com/Gistix/portal2vr/issues/72), [#150](https://github.com/Gistix/portal2vr/issues/150) | VGUI, HUD render target |
| Roomscale | [#31](https://github.com/Gistix/portal2vr/issues/31), [#63](https://github.com/Gistix/portal2vr/issues/63), [#91](https://github.com/Gistix/portal2vr/issues/91) | `CreateMove`, tracking origin and collision |
| Traversal / comfort | [#36](https://github.com/Gistix/portal2vr/issues/36), [#55](https://github.com/Gistix/portal2vr/issues/55), [#107](https://github.com/Gistix/portal2vr/issues/107) | `PlayerPortalled`, rotation offset |
| Height | [#11](https://github.com/Gistix/portal2vr/issues/11), [#65](https://github.com/Gistix/portal2vr/issues/65), [#92](https://github.com/Gistix/portal2vr/issues/92), [#159](https://github.com/Gistix/portal2vr/issues/159) | Recenter and tracking origin |
| Haptics | [#44](https://github.com/Gistix/portal2vr/issues/44) | OpenVR action/haptic integration |
| Resolution / targets | [#119](https://github.com/Gistix/portal2vr/issues/119), [#165](https://github.com/Gistix/portal2vr/issues/165), [#174](https://github.com/Gistix/portal2vr/issues/174) | DXVK, render target sizing, AA |

## Reference work

[#45](https://github.com/Gistix/portal2vr/pull/45) proposes standing-height placement. [#103](https://github.com/Gistix/portal2vr/pull/103) explores pitch/roll after portal traversal and records edge cases. [#136](https://github.com/Gistix/portal2vr/pull/136) contains a digital-action transition fix mixed with Portal Reloaded `ThirdAttack`. [#40](https://github.com/Gistix/portal2vr/pull/40) replaces the missing-`-insecure` process exit with a warning, but also changes unrelated behavior. [portal_reloaded_vr](https://github.com/stsichler/portal_reloaded_vr) combines additional mod support and DXVK patches. [portal2vr-roomscale](https://github.com/Spencer0187/portal2vr-roomscale) demonstrates roomscale movement while documenting that physical portal passage remains blocked. M1 adopts only narrow concepts, with no copied fork code.
