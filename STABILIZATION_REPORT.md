# Independent stabilization — 2026-10-03

The fixes and package automation build and pass available automated tests on `experimental`. This is **not a hardware-validated VR release**. There is a concrete compatibility blocker: three required signatures are ambiguous in the local Portal 2 DLLs, and the hardened scanner refuses them. Their semantic identity must be established before this build is promoted as compatible with those binaries. No Portal 2, SteamVR, or headset session was performed; installed game files were only read for signature verification.

## Scope

Repository: `mmadej20/portal2vr-ultimate`. Starting HEAD: `a7f1f15` on `experimental`. The reference `a7b41e8707fee7ad8cea4c8123e9b3304fa11a80` and starting HEAD have identical tree `0dc756aea4c5bbb19f06bf0d95dc1b7f26d8524e`. The remote was fetched before work and checked again before push. The modified DXVK/OpenVR architecture and x86 game runtime remain.

The user's later release request supersedes the initial no-auto-publication requirement: main pushes publish incremental releases; experimental builds upload artifacts only. This task does not integrate `main`, create a PR, or publish a release. **A PR to main must wait for the user's hardware tests.**

## Verification matrix

| Finding | Classification | Evidence, affected code, action |
|---|---|---|
| A1: direct Ex creation/global bridge | **CONFIRMED** | Only `CreateDevice` assigned the global bridge, while direct `CreateDeviceEx` and `PresentEx` could bypass it; replacement/multiple-device ownership and failed `InitialReset` cleanup were unsafe. `d3d9_interface.cpp` centralizes creation in Ex with scoped `Com` cleanup. Each `D3D9DeviceEx` owns a bridge; VR acquires paired strong device/bridge refs. Registry acquisition and last-public-reference removal share a lock. Native registry lifetime/concurrency tests pass; real D3D/VR creation remains untested. |
| A2: reset leaves stale image descriptor | **CONFIRMED** | VR cached a raw backbuffer descriptor once, while swapchain reset recreates images. `Reset`/`ResetEx` invalidate owned surfaces, overlays and bridge snapshots before recreation. The bridge pins the just-rendered image before Present rotates buffers; `SubmitVRTextures` reacquires every frame. The existing device lock covers reset through submission for multithreaded devices. Actual GPU reset correctness needs hardware testing. |
| A3: full GPU wait/latency | **POTENTIAL** optimization | Flush, command-stream synchronization and Vulkan device-idle create a stall, but avoidable latency and an equivalent external-image completion contract were not measured. `WaitDeviceIdle` remains for the bound VR device. Other devices do not drive its update/wait. No FPS or latency improvement is claimed. |
| B1: unchecked menu dimensions | **CONFIRMED** | Old bounds/aspect calculations divided unchecked dimensions and could exceed valid texture bounds. `hud_capture.h::MenuTextureMapping` reuses clamped `WindowTextureCrop`, rejects unavailable dimensions, and uses actual submitted image dimensions. Invalid geometry hides the overlay/releases input; mouse scale and mapping follow the current descriptor. Boundary fixtures cover zero, negative, oversized, asymmetric, full-menu and extreme dimensions. |
| B2: permanent render-target failure | **CONFIRMED** | A single failure permanently set `m_RenderTargetsFailed`. `render_target_readiness.h::RenderTargetRetryState` caps failures at three, spaces attempts by at least one second, and rearms on device reset. Partial owned surfaces are released; unavailable devices do not allocate. Borrowed Source textures are not speculatively destroyed. Real Source cache/driver allocation recovery remains untested. |
| C: crash-inconsistent installation | **CONFIRMED** | A terminated fixture subprocess left the first modified file without committed ownership, making restore ineffective. `test_launcher_core.py` now persists validated preimages/journal before writes and recovers under the existing process lock. Pre-commit interruption rolls back idempotently; post-commit state is verified/finalized. Corruption/external edits preserve evidence and stop recovery. Abrupt-process install/upgrade/restore/recovery tests pass. |
| D1: scanner ambiguity/image safety | **HIGH-CONFIDENCE**, with **confirmed on-disk compatibility blocker** | Old scanning chose the first whole-image match and accepted malformed tokens. `sigscanner.h` validates PE32 headers, readable executable sections, arithmetic and uniqueness, including configured anchors. Actual scanner tests use a synthetic mapped PE. Three required local-game patterns are non-unique and do not match their old anchors, so startup fails closed. No target was guessed; compatibility is blocked as detailed below. |
| D2: partial native hook startup | **HIGH-CONFIDENCE** | Required create results were ignored and detours could enable before complete initialization. `RequiredHooks` stages all mandatory creation before enable, checks results, and blocks publication on failure. Unpublished modifying detours forward originals. Failed startup disables entry detours but retains Game/VR and trampoline storage through process exit. A real MinHook late-callback regression uses the same production helper. |
| E: config dependencies/reload | **CONFIRMED** dependency/reload gaps; **FALSE POSITIVE** missing numeric validation | Native and Python parsers already enforced finite numeric ranges. Native roomscale/muzzle dependencies and effective reload constraints differed. `ApplyRuntimeConfig` rejects malformed reloads as a whole, preserves restart-only fields, then validates roomscale/6DOF/LegacyYaw and muzzle/alignment dependencies. Native startup/reload fixtures pass. This establishes semantic consistency, not whole-runtime thread safety. |

## Implemented commits

| Commit | Change |
|---|---|
| `21438b6` | Native scanner safety, required hook staging/publication, retained-trampoline rollback, dependency validation and player-slot bounds; scanner/config and actual MinHook fixtures. |
| `e84120c` | Device-specific resource ownership, strong paired registry acquisition, shared OpenVR leases, reset invalidation/image pinning, bounded retry, menu geometry/input guards and effective config reload integration. |
| `6fecd62` | Launcher durable preimages/journal and recovery, lost/incomplete ownership rejection, developer-launcher sequencing and termination fixtures. |
| `095da96` | Standalone launcher/package, frozen paths/DLL search, UI recovery, version/publisher tools, tests and Windows Actions workflow. |

The user's concurrent wording commit `4183c33` is preserved separately. This report/release-guide refinement is a later documentation commit. Subsystem rollback should include dependent integration changes. For game-file rollback, use launcher recovery/restore: keep its state, journal and original backups. There is no supported live native unload; failed/published hook state stays alive until process exit.

## Additional defects and decisions

- Shared OpenVR leases prevent duplicate initialization/context replacement and premature shutdown by a different owner. Failed acquisition owns no shutdown; the last successful lease shuts down once.
- Texture capture no longer leaks an extra `GetSurfaceLevel` reference; replacing an owned surface releases its prior reference.
- Reset sizing, viewport overrides, texture/depth MSAA and capture are gated to the VR-owning device. An ambiguous registry does not choose an arbitrary device.
- The bound backbuffer's MSAA resolve is queued before capture. Window presentation resolves separately and does not populate that texture's shared resolve image. The retained device-idle wait completes it before submission.
- Null material render contexts are guarded in allocation, placement, input and menu update. Held menu input is released when mapping is unavailable.
- Startup rollback formerly could free a trampoline before an already-entered detour called its original. The actual MinHook test fails safely with freeing rollback and passes with retained storage.
- Incomplete `{}` state could resnapshot mod bytes as originals. Existing state must contain consistent ownership metadata. Missing state with retained same-game backups blocks staging when targets no longer match recorded originals; completed restore and pre-write interruption remain retryable.
- Three existing HUD count assertions used `bool` conversion; integer comparisons now verify exact nesting counts and remove two test warnings.
- Frozen payload/state paths use the executable folder. Windows DLL search is restored before external system programs, following [PyInstaller's external-program guidance](https://pyinstaller.org/en/stable/common-issues-and-pitfalls.html#launching-external-programs-from-the-frozen-application).

## Verification

Baseline: 57 Python tests, native stabilization executable, and clean Release/x86 rebuild passed. Final local commands/results:

```powershell
rtk proxy "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" l4d2vr.sln /t:Rebuild /m /p:Configuration=Release /p:Platform=x86 /p:PORTAL2_DIR= /nologo
rtk proxy "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tests\stabilization_tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /nologo
rtk proxy "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tests\d3d9_bridge_tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /nologo
rtk proxy "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tests\native_safety_tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /nologo
rtk proxy "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tests\hook_rollback_tests.vcxproj /t:Build /p:Configuration=Release /p:Platform=Win32 /nologo
rtk proxy tests\bin\stabilization_tests.exe
rtk proxy tests\bin\d3d9_bridge_tests.exe
rtk proxy tests\bin\native_safety_tests.exe
rtk proxy tests\bin\hook_rollback_tests.exe
rtk proxy python -m unittest discover -s tests -p test_*.py
rtk proxy .\.package-venv\Scripts\python.exe -m PyInstaller --noconfirm --onefile --windowed --name "Portal2VR Launcher" --paths . --distpath dist/launcher --workpath build/launcher --specpath build/spec tools/launcher_entry.py
rtk proxy git diff --check
```

All builds/native executables exited 0; the full Python suite passed **100 tests**. Local VS 18 MSBuild uses the installed VS 2022 v143 toolchain; CI selects VS 2022 on `windows-2022`. Normalized compiler warning fingerprints changed from 56 to 55, with no new fingerprint; the removed diagnostic was the bridge class/struct mismatch. Vendored warnings were not suppressed. Parallel MSBuild repeats diagnostics in its summary, so raw occurrence counts are not directly comparable.

The actual ZIP self-test ran `Portal2VR Launcher.exe --self-test <absolute report.json>` outside the source tree with Python removed from `PATH`. It created Tk widgets, installed nine payloads into temporary fake game files and restored the original DLL. Report: `ok`, `frozen`, `tkinter`, `restored` all true, `payload_count=9`. No Steam/game launch occurred. The host still has Python installed; this was not a clean-machine test. The package includes x86 game DLLs and an x64 standalone launcher.

Observed RED/GREEN checks included abrupt first-file termination, interrupted UI restore, incomplete/lost-state resnapshotting, frozen DLL search, incremental version selection and actual MinHook trampoline lifetime. Fault injection covers before/during/after backups, first/multiple/all writes, before/during/after state commit, upgrades, restores, repeat recovery termination, corrupted preimages/journals and external edits. Publisher/snapshot tests use a fake GitHub boundary, including incomplete drafts, immutable/conflicting assets, tag-creation races, integrity and repackaging; they do not prove remote publication.

Actionlint 1.7.12 passes with `-ignore queue`; unfiltered it reports only the newer `queue: max` key supported by [current GitHub documentation](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency). All three publish Bash blocks pass `bash -n`. [Actions records hosted experimental build results](https://github.com/mmadej20/portal2vr-ultimate/actions?query=branch%3Aexperimental). The main-only publish job is intentionally unexecuted here.

## Releases and performance

The workflow builds/tests a `v0.0.0` snapshot and verifies the extracted standalone launcher. Only a main push can enter the write-scoped publish job, which verifies snapshot SHA-256/manifest, assigns `v0.1.0` then increments the highest stable patch, repackages the exact payloads, and publishes a draft only after tag/assets agree with the exact commit. Drafts reserve versions; retries with different existing asset bytes stop instead of overwriting. Main runs queue serially. Experimental/PR/manual runs upload downloadable ZIP/checksum/report artifacts. See [RELEASES.md](RELEASES.md).

The retained frame pipeline is Source eye rendering → capture/pin rendered backbuffer → DXVK Present → eye resolve → flush/command-stream/device-idle wait → OpenVR submit and poses/actions. No real CPU/GPU timings, FPS or latency were measured. A replacement synchronization experiment must measure each wait/resolve/Submit/WaitGetPoses separately, compare median/tail times on the same scene/headset/settings, and establish compositor completion/layout and image-lifetime guarantees through buffer rotation, reset, HUD/menu submission and disconnect. No unverified fence/semaphore optimization is enabled.

## Blocker and remaining experiments

Read-only executable/readable PE section scans found `EyePosition` (server: 3 matches), `Weapon_ShootPosition` (server: 4), and `GetViewModelFOV` (client: 4); none matches its configured old RVA. Examined SHA-256: client `1b22a5008c10c91215f5c66061ff24dd84e3afb626d60b9d74c600974536f66d`, server `deb9fc303fdf4b0b0edcf58c4e1b17fbf20276f047728180f7ba7c8bf510f6b6`. This is on-disk evidence, not loaded-game execution. The old first-match scanner is not evidence those targets were correct. Existing binary-analysis/decompilation artifacts remain local/ignored.

Resolve D1 by independently identifying each function from call sites, vtable/data-flow and argument/return ABI on the exact build; only then extend its signature or use an exact-build guard for a proven target. Rescan supported binaries and perform real initialization/game testing. Until then, refusing unsafe hooks is the technical blocker.

Other required hardware/runtime experiments:

- Direct/ordinary device creation, repeated resets, minimize/resize, MSAA and image correctness. Two simultaneous devices remain a conditional startup limitation until an explicit game-device identity signal exists.
- Allocation failures and loading/workshop-map transitions: bounded retry tests prove scheduling, not recovery from every Source named-texture cache/driver failure.
- Thread traces around Source callbacks while the recursive multithreaded device lock is held. No reverse lock order was demonstrated, but cross-thread waits and broader existing render/tracking/command state sharing are not proved safe.
- Live config edits/restart-only settings/recenter with hooks active. Semantic validation does not introduce a thread-safe shared-state framework.
- Stereo, aiming, controller menu clicks, haptics, roomscale/collision, portals, player/map replacement, HMD/controller disconnect/reconnect. No physical compatibility claim follows from these unit/fixture results.
- Real power loss and filesystem rename/directory persistence. File data is flushed and process termination tested; retain journals/backups rather than deleting recovery evidence.

## Delivery

All work stays on `experimental`. The exact final SHA, normal push result and hosted CI run are reported at delivery. No forced push, main integration, PR, release, game deployment, or hardware test occurs in this task. The user's separate wording commit is preserved. Build logs, local package checks, fixtures, installation state, game binaries and research artifacts remain ignored/local.
