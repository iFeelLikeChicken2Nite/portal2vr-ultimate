# Portal2VR modernization roadmap

This fork incrementally stabilizes the Windows Portal 2 runtime hook implementation. The x86 Source/D3D9 path, modified DXVK renderer, OpenVR backend and existing controller bindings remain the compatibility baseline.

| Milestone | Scope | Depends on / exit criterion |
| --- | --- | --- |
| M0: baseline | Reproducible x86 build, architecture/hook/space audit and issue map | Known toolchain, dependency revisions, observed baseline and unverified hardware paths documented |
| M1: stabilization | Digital input edges, pose validity, checked OpenVR and hook startup, persistent diagnostics, atomic config reload, safe lifecycle and optional deployment | M0; deterministic isolated tests, successful build, clear failures and no silent game termination |
| M2: tracking spaces and locomotion | Explicit standing/seated mode, origin and height policy, locomotion mapping | M1 validity and diagnostics; real headset and Portal 2 movement checks |
| M3: portal-aware roomscale | Portal-aware collision and physical traversal | M2 explicit spaces; portal entrance/exit tests |
| M4: traversal comfort | Orientation policy, recovery and user comfort controls | M3 traversal data; test awkward portal angles and user comfort |
| M5: VR HUD | HUD/UI/subtitle architecture and legibility | Stable view/space model from M2–M4 |
| M6: polish/backend | Haptics, renderer/performance changes and optional OpenXR | Stable OpenVR path and measured profiling; OpenXR as an additional backend |

M1 deliberately leaves roomscale, player-origin memory manipulation, portal-aware movement, OpenXR, HUD redesign, haptic redesign, render optimization and Portal Reloaded `ThirdAttack` for later decisions. PRs and forks in [the architecture audit](architecture.md) are references, not integration branches.
