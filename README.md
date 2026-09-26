# Portal 2 VR

Modernization in progress.

M3 roomscale movement is not enabled. In `VR/config.txt`, keep `RoomscaleMode=Off` (default); `Observe` collects diagnostics only and never moves the player. Physical portal traversal remains unverified.

M4 portal orientation is experimental and hardware-unverified. `PortalOrientationMode=LegacyYaw` preserves the existing behavior. `FullRotation`, `YawOnly` and `PreserveHorizon` are manual opt-ins that require a game restart; their semantics and test checklist are in the local-only `docs/architecture.md` and `docs/m4-vr-test-matrix.md`. If visual alignment is wrong, return to `LegacyYaw`.
