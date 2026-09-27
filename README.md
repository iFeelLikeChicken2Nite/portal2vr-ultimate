# Portal 2 VR

Modernization in progress.

M3 roomscale movement is not enabled. In `VR/config.txt`, keep `RoomscaleMode=Off` (default); `Observe` collects diagnostics only and never moves the player. Physical portal traversal remains unverified.

M4 portal orientation is experimental and hardware-unverified. `PortalOrientationMode=LegacyYaw` preserves the existing behavior. `FullRotation`, `YawOnly` and `PreserveHorizon` are manual opt-ins that require a game restart; their semantics and test checklist are in the local-only `docs/architecture.md` and `docs/m4-vr-test-matrix.md`. If visual alignment is wrong, return to `LegacyYaw`.

M5 HUD rendering is also an experimental, hardware-unverified opt-in: `ExperimentalHUDOverlay=false` by default. When enabled it requires a restart and is shown only with controller-laser aiming (`AimMode=2`). HUD distance, width and vertical offset are configured in `VR/config.txt`; subtitle and menu acceptance still require a headset session. Details remain in local-only docs.

M6 adds a default-off, hardware-unverified portal-shot haptic experiment (`ExperimentalPortalShotHaptics=false`). It uses the existing OpenVR vibration bindings and does not change ordinary controller input. Pickup, damage and interaction haptics, a faster desktop mirror, and an OpenXR backend are **not** implemented; they require verified game events, frame-time measurements, or working graphics/UI integration before release claims.
