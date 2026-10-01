"""Pure configuration and file operations for the local test launcher."""

from dataclasses import dataclass
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import uuid

if os.name == "nt":
    import msvcrt
else:
    import fcntl


@dataclass(frozen=True)
class Profile:
    label: str
    changes: dict[str, str]
    experimental: bool = False


@dataclass(frozen=True)
class Check:
    group: str
    id: str
    label: str
    profile: str


BASE_VALUES = {
    "TrackingMode": "Seated",
    "RoomscaleMode": "Off",
    "PortalOrientationMode": "LegacyYaw",
    "ExperimentalHUDOverlay": "false",
    "ExperimentalWorldAimMarker": "false",
    "ExperimentalViewmodelAlignment": "false",
    "ExperimentalPortalShotHaptics": "false",
    "AimMode": "2",
    "RenderWindow": "0",
    "HUDDistanceMeters": "1.3",
    "HUDWidthMeters": "1.4",
    "HUDVerticalOffsetMeters": "-0.15",
    "ViewmodelPosCustomOffsetX": "0.0",
    "ViewmodelPosCustomOffsetY": "0.0",
    "ViewmodelPosCustomOffsetZ": "0.0",
}

PROFILES = {
    "baseline": Profile("Baseline (M0/M1)", {}),
    "combined": Profile("Confirmed setup + native reticle test",
                        {"ExperimentalHUDOverlay": "true",
                         "ExperimentalWorldAimMarker": "true",
                         "ExperimentalPortalShotHaptics": "true"}, True),
    "standing": Profile("M2 Standing", {"TrackingMode": "Standing"}),
    "observe": Profile("M3 Observe (no player movement)", {"RoomscaleMode": "Observe"}),
    "roomscale_observe_combined": Profile("M3 Observe + current VR setup (no player movement)",
                                          {"RoomscaleMode": "Observe",
                                           "ExperimentalHUDOverlay": "true",
                                           "ExperimentalWorldAimMarker": "true",
                                           "ExperimentalPortalShotHaptics": "true",
                                           "ViewmodelPosCustomOffsetX": "-4.5",
                                           "ViewmodelPosCustomOffsetY": "1.0",
                                           "ViewmodelPosCustomOffsetZ": "-1.5"}, True),
    "roomscale_active_experimental": Profile("M3 Active roomscale (EXPERIMENTAL movement)",
                                             {"RoomscaleMode": "ActiveExperimental",
                                              "6DOF": "true",
                                              "ExperimentalViewmodelAlignment": "true",
                                              "HUDDistanceMeters": "1.25",
                                              "HUDWidthMeters": "1.65",
                                              "HUDVerticalOffsetMeters": "0.10",
                                              "ExperimentalHUDOverlay": "true",
                                              "ExperimentalWorldAimMarker": "true",
                                              "ExperimentalPortalShotHaptics": "true",
                                              "ViewmodelPosCustomOffsetX": "-4.5",
                                              "ViewmodelPosCustomOffsetY": "1.0",
                                              "ViewmodelPosCustomOffsetZ": "-1.5"}, True),
    "full_rotation": Profile("M4 FullRotation", {"PortalOrientationMode": "FullRotation"}, True),
    "yaw_only": Profile("M4 YawOnly", {"PortalOrientationMode": "YawOnly"}, True),
    "preserve_horizon": Profile("M4 PreserveHorizon", {"PortalOrientationMode": "PreserveHorizon"}, True),
    "hud": Profile("M5 HUD", {"ExperimentalHUDOverlay": "true"}, True),
    "aim_marker": Profile("Controller aim marker (experimental)",
                          {"ExperimentalWorldAimMarker": "true"}, True),
    "aim_model_alignment": Profile("Native reticle + portal-gun alignment (experimental)",
                                   {"ExperimentalHUDOverlay": "true",
                                    "ExperimentalViewmodelAlignment": "true",
                                    "ExperimentalWorldAimMarker": "true",
                                    "ExperimentalPortalShotHaptics": "true",
                                    "ViewmodelPosCustomOffsetX": "-4.5",
                                    "ViewmodelPosCustomOffsetY": "1.0",
                                    "ViewmodelPosCustomOffsetZ": "-1.5"}, True),
    "haptics": Profile("M6 Haptics", {"ExperimentalPortalShotHaptics": "true"}, True),
    "mirror": Profile("M6 Mirror ON", {"RenderWindow": "1"}, True),
}

CHECKLIST = (
    Check("M0/M1", "m1_launch", "Launch with -insecure; no crash; resolved hooks appear in log", "baseline"),
    Check("M0/M1", "m1_menu", "Menu and game image are visible in the HMD", "baseline"),
    Check("M0/M1", "m1_controller_loss", "Controller disconnect/reconnect does not crash", "baseline"),
    Check("M0/M1", "m1_shutdown", "Game exits cleanly", "baseline"),
    Check("M2", "m2_seated", "Seated: head and both hands track correctly", "baseline"),
    Check("M2", "m2_standing", "Standing: level loads; tracked-height anchor and recenter feel correct", "standing"),
    Check("M2", "m2_turn", "Snap/smooth turning and movement direction", "baseline"),
    Check("M2", "m2_aim", "3D laser line follows the right-controller shot", "aim_marker"),
    Check("M2", "m2_portal_status", "Native blue/orange portal status appears at the laser endpoint", "combined"),
    Check("M2", "m2_model_alignment", "Compare portal-gun body/glow against aim-marker profile", "aim_model_alignment"),
    Check("M3", "m3_observe", "Observe logs steps without moving the player", "roomscale_observe_combined"),
    Check("M3", "m3_active_walk", "Small physical steps: body follows head; returning restores alignment", "roomscale_active_experimental"),
    Check("M3", "m3_active_wall", "Wall: view remains bounded; stepping back does not drift the body", "roomscale_active_experimental"),
    Check("M3", "m3_active_input", "Stick, pause and recenter: no jumps or lingering physical movement", "roomscale_active_experimental"),
    Check("M3", "m3_active_portal", "After walk tests: wall portal resets old target; head/hand stay aligned", "roomscale_active_experimental"),
    Check("M4", "m4_legacy", "LegacyYaw: wall-to-wall portal traversal", "baseline"),
    Check("M4", "m4_full", "FullRotation: head/hand orientation across wall portals", "full_rotation"),
    Check("M4", "m4_floor", "Floor/ceiling portals: comfort and stereo", "preserve_horizon"),
    Check("M4", "m4_recenter", "Recenter around portal traversal; no duplicate event", "full_rotation"),
    Check("M5", "m5_subtitles", "Subtitles: visible, readable, and follow the view", "hud"),
    Check("M5", "m5_menu", "Pause/menu: hover, click, back; no stuck click", "hud"),
    Check("M5", "m5_roomscale_menu", "After physical walking: pause menu is readable without returning to start", "roomscale_active_experimental"),
    Check("M5", "m5_loss", "Keyboard still operates menu after controller loss", "hud"),
    Check("M6", "m6_shot", "Portal shot: one pulse on the correct hand", "haptics"),
    Check("M6", "m6_loss", "Controller loss: no haptic spam or crash", "haptics"),
    Check("M6", "m6_mirror", "Mirror off/on: image and UI; no performance claim", "mirror"),
    Check("M6", "m6_perf", "Record median and high-percentile CPU/GPU timing", "mirror"),
)


def visible_checks(completed: set[str], show_completed: bool = False) -> tuple[Check, ...]:
    """Keep the local record while presenting only outstanding checks by default."""
    return tuple(item for item in CHECKLIST if show_completed or item.id not in completed)


SOURCE_TO_TARGET = (
    ("Release/d3d9.dll", "bin/d3d9.dll"),
    ("thirdparty/openvr/bin/win32/openvr_api.dll", "bin/openvr_api.dll"),
    ("L4D2VR/config.txt", "VR/config.txt"),
    ("L4D2VR/manifest.vrmanifest", "VR/manifest.vrmanifest"),
    ("L4D2VR/portal2vr_capsule_main.png", "VR/portal2vr_capsule_main.png"),
    ("L4D2VR/SteamVRActionManifest/action_manifest.json",
     "VR/SteamVRActionManifest/action_manifest.json"),
    ("L4D2VR/SteamVRActionManifest/bindings_knuckles.json",
     "VR/SteamVRActionManifest/bindings_knuckles.json"),
    ("L4D2VR/SteamVRActionManifest/bindings_oculus_touch.json",
     "VR/SteamVRActionManifest/bindings_oculus_touch.json"),
    ("L4D2VR/SteamVRActionManifest/bindings_vive_cosmos_controller.json",
     "VR/SteamVRActionManifest/bindings_vive_cosmos_controller.json"),
)


def render_config(template: str, profile: str) -> str:
    """Apply one known test profile to the versioned config template."""
    if profile not in PROFILES:
        raise ValueError(f"Unknown test profile: {profile}")
    values = {**BASE_VALUES, **PROFILES[profile].changes}
    seen: set[str] = set()
    result: list[str] = []
    for line in template.splitlines(keepends=True):
        newline = "\r\n" if line.endswith("\r\n") else "\n" if line.endswith("\n") else ""
        body = line[: -len(newline)] if newline else line
        if "=" not in body:
            result.append(line)
            continue
        key_part, old_value = body.split("=", 1)
        key = key_part.strip()
        if key not in values:
            result.append(line)
            continue
        if key in seen:
            raise ValueError(f"Duplicate config key: {key}")
        seen.add(key)
        before_comment, marker, comment = old_value.partition("#")
        spacing = before_comment[len(before_comment.rstrip()):] if marker else ""
        result.append(f"{key_part}={values[key]}{spacing}{marker}{comment}{newline}")
    missing = values.keys() - seen
    if missing:
        raise ValueError("Missing config keys: " + ", ".join(sorted(missing)))
    return "".join(result)


def load_state(path: Path) -> dict:
    """Read local checklist/install state, without silently discarding corruption."""
    state = {
        "checks": [],
        "notes": "",
        "profile": "baseline",
        "game_dir": r"D:\SteamLibrary\steamapps\common\Portal 2",
        "steam_exe": r"C:\Program Files (x86)\Steam\steam.exe",
        "managed_files": {},
        "original_files": {},
        "backup_id": "",
        "installed_game_dir": "",
    }
    if not path.exists():
        return state
    try:
        loaded = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Cannot read launcher state: {error}") from error
    if not isinstance(loaded, dict):
        raise ValueError("Launcher state must be a JSON object")
    for key, expected in (("checks", list), ("notes", str), ("profile", str),
                          ("game_dir", str), ("steam_exe", str), ("managed_files", dict),
                          ("original_files", dict), ("backup_id", str),
                          ("installed_game_dir", str)):
        if key in loaded:
            if not isinstance(loaded[key], expected):
                raise ValueError(f"Invalid launcher state field: {key}")
            state[key] = loaded[key]
    if any(not isinstance(item, str) for item in state["checks"]):
        raise ValueError("Invalid launcher checklist entry")
    for field in ("managed_files", "original_files"):
        if any(not isinstance(key, str) or not isinstance(value, str)
               for key, value in state[field].items()):
            raise ValueError(f"Invalid launcher {field} entry")
    return state


def save_state(path: Path, state: dict) -> None:
    """Atomically persist human observations and exact-file ownership."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    os.replace(temporary, path)


@contextmanager
def _transaction_lock(state_path: Path):
    """Serialize state and game-file operations across launcher processes."""
    lock_path = state_path.with_name(state_path.name + ".lock")
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open("a+b") as handle:
        if lock_path.stat().st_size == 0:
            handle.write(b"0")
            handle.flush()
        handle.seek(0)
        try:
            if os.name == "nt":
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError("Another launcher operation is in progress") from error
        try:
            yield
        finally:
            handle.seek(0)
            if os.name == "nt":
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)


def save_preferences(state_path: Path, ui_state: dict) -> dict:
    """Save human-entered fields without replacing install ownership metadata."""
    if ui_state["profile"] not in PROFILES:
        raise ValueError("Unknown test profile")
    with _transaction_lock(state_path):
        current = load_state(state_path)
        for key in ("checks", "notes", "profile", "game_dir", "steam_exe"):
            current[key] = ui_state[key]
        save_state(state_path, current)
        return current


def _digest(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def _safe_target(game: Path, relative: str) -> Path:
    target = game / relative
    if target.is_symlink() or not target.resolve().is_relative_to(game.resolve()):
        raise ValueError(f"Unsafe game target: {target}")
    return target


def plan_install(repo: Path, game: Path, profile: str) -> dict[Path, bytes]:
    """Read exact source payloads without writing to the game."""
    if not (game / "portal2.exe").is_file() or not (game / "bin").is_dir():
        raise FileNotFoundError(f"Not a Portal 2 installation: {game}")
    planned: dict[Path, bytes] = {}
    for source_relative, target_relative in SOURCE_TO_TARGET:
        source = repo / source_relative
        if not source.is_file():
            raise FileNotFoundError(f"Missing launcher source: {source}")
        payload = source.read_bytes()
        if target_relative == "VR/config.txt":
            payload = render_config(payload.decode("utf-8"), profile).encode("utf-8")
        planned[_safe_target(game, target_relative)] = payload
    return planned


def validate_launch_paths(repo: Path, game: Path, steam: Path) -> None:
    if not steam.is_file() or steam.name.lower() != "steam.exe":
        raise FileNotFoundError(f"Steam executable not found: {steam}")
    plan_install(repo, game, "baseline")


def build_steam_command(steam: Path) -> list[str]:
    return [str(steam), "-applaunch", "620", "-insecure", "-window", "-novid",
            "+mat_motion_blur_percent_of_screen_max", "0", "+mat_queue_mode", "0",
            "+mat_vsync", "0", "+mat_antialias", "0",
            "+mat_grain_scale_override", "0", "-width", "1280", "-height", "720"]


def _write_payload(target: Path, payload: bytes) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=target.name + ".portal2vr-", suffix=".tmp",
                                     dir=target.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        temporary.write_bytes(payload)
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def _remove_empty_vr_directories(game: Path) -> None:
    for directory in (game / "VR/SteamVRActionManifest", game / "VR"):
        try:
            directory.rmdir()
        except (FileNotFoundError, OSError):
            pass


def _validate_managed(state: dict, game: Path) -> dict[str, str]:
    managed = state["managed_files"]
    allowed = {target for _, target in SOURCE_TO_TARGET}
    if any(relative not in allowed for relative in managed | state["original_files"]):
        raise ValueError("Launcher state contains an unexpected file path")
    if managed and managed.keys() != allowed:
        raise ValueError("Launcher state has an incomplete managed file set")
    if managed and state["installed_game_dir"] != str(game.resolve()):
        raise RuntimeError("Test files belong to another game directory; restore there first")
    if managed and not re.fullmatch(r"[0-9a-f]{32}", state["backup_id"]):
        raise ValueError("Launcher state has no valid backup identifier")
    if not managed and (state["original_files"] or state["backup_id"]):
        raise ValueError("Launcher state has inconsistent backup information")
    return managed


def _backup_dir(state_path: Path, backup_id: str) -> Path:
    if not re.fullmatch(r"[0-9a-f]{32}", backup_id):
        raise ValueError("Invalid backup identifier")
    root = state_path.parent / ".launcher-backups"
    if root.is_symlink():
        raise ValueError("Backup root must not be a symlink")
    snapshot = root / backup_id
    if snapshot.is_symlink():
        raise ValueError("Backup snapshot must not be a symlink")
    return snapshot


def _create_backup(game: Path, state_path: Path,
                   previous: dict[Path, bytes | None]) -> tuple[str, dict[str, str]]:
    backup_id = uuid.uuid4().hex
    snapshot = _backup_dir(state_path, backup_id)
    snapshot.mkdir(parents=True, exist_ok=False)
    originals: dict[str, str] = {}
    for target, contents in previous.items():
        if contents is None:
            continue
        relative = target.relative_to(game).as_posix()
        backup = snapshot / relative
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(target, backup)
        digest = _digest(contents)
        if _digest(backup.read_bytes()) != digest:
            raise RuntimeError(f"Backup verification failed: {backup}")
        originals[relative] = digest
    save_state(snapshot / "manifest.json", {
        "game_dir": str(game.resolve()), "original_files": originals,
    })
    return backup_id, originals


def _verified_backups(game: Path, state_path: Path, state: dict) -> dict[str, Path]:
    snapshot = _backup_dir(state_path, state["backup_id"])
    manifest = snapshot / "manifest.json"
    if manifest.is_symlink() or not manifest.is_file():
        raise RuntimeError(f"Backup manifest missing or unsafe: {manifest}")
    try:
        recorded = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Cannot read backup manifest: {error}") from error
    originals = state["original_files"]
    if recorded != {"game_dir": str(game.resolve()), "original_files": originals}:
        raise RuntimeError("Backup manifest does not match staged game state")
    verified: dict[str, Path] = {}
    for relative, digest in originals.items():
        backup = snapshot / relative
        if (backup.is_symlink() or not backup.resolve().is_relative_to(snapshot.resolve())
                or not backup.is_file() or _digest(backup.read_bytes()) != digest):
            raise RuntimeError(f"Backup file changed or missing: {backup}")
        verified[relative] = backup
    return verified


def _copy_backup(backup: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=target.name + ".portal2vr-", suffix=".tmp",
                                     dir=target.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copy2(backup, temporary)
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def _stage_install_unlocked(repo: Path, game: Path, state_path: Path, profile: str) -> list[Path]:
    """Stage exact mod files after snapshotting originals; roll back failures."""
    planned = plan_install(repo, game, profile)
    state = load_state(state_path)
    managed = _validate_managed(state, game)
    planned_paths = {target.relative_to(game).as_posix() for target in planned}
    if managed and managed.keys() != planned_paths:
        raise RuntimeError("Launcher version changed its file set; restore first")
    previous: dict[Path, bytes | None] = {}
    backups = _verified_backups(game, state_path, state) if managed else {}
    for target in planned:
        relative = target.relative_to(game).as_posix()
        if target.is_symlink():
            raise ValueError(f"Refusing symlink: {target}")
        if target.exists():
            contents = target.read_bytes()
            if managed and _digest(contents) != managed[relative]:
                raise RuntimeError(f"Managed file changed outside launcher: {target}")
            previous[target] = contents
        else:
            if managed:
                raise RuntimeError(f"Managed file disappeared outside launcher: {target}")
            previous[target] = None
    backup_id, originals = (state["backup_id"], state["original_files"])
    if not managed:
        backup_id, originals = _create_backup(game, state_path, previous)
    written: list[Path] = []
    try:
        for target, payload in planned.items():
            if previous[target] == payload:
                continue
            written.append(target)
            _write_payload(target, payload)
        state["managed_files"] = {
            target.relative_to(game).as_posix(): _digest(payload)
            for target, payload in planned.items()
        }
        state["original_files"] = originals
        state["backup_id"] = backup_id
        state["installed_game_dir"] = str(game.resolve())
        state["game_dir"] = str(game)
        state["profile"] = profile
        save_state(state_path, state)
    except Exception as error:
        rollback_errors = []
        for target in reversed(written):
            try:
                if previous[target] is None:
                    target.unlink(missing_ok=True)
                elif not managed and target.relative_to(game).as_posix() in originals:
                    _copy_backup(_backup_dir(state_path, backup_id) /
                                 target.relative_to(game).as_posix(), target)
                else:
                    _write_payload(target, previous[target])
            except OSError as rollback_error:
                rollback_errors.append(f"{target}: {rollback_error}")
        _remove_empty_vr_directories(game)
        if rollback_errors:
            raise RuntimeError("Staging failed and rollback is incomplete: " +
                               "; ".join(rollback_errors)) from error
        raise
    return list(planned)


def stage_install(repo: Path, game: Path, state_path: Path, profile: str) -> list[Path]:
    with _transaction_lock(state_path):
        return _stage_install_unlocked(repo, game, state_path, profile)


def _restore_install_unlocked(game: Path, state_path: Path) -> list[Path]:
    """Restore pre-existing files, remove created ones, and retain backups."""
    state = load_state(state_path)
    managed = _validate_managed(state, game)
    if not managed:
        return []
    backups = _verified_backups(game, state_path, state)
    targets: list[Path] = []
    for relative, digest in managed.items():
        target = _safe_target(game, relative)
        if target.is_symlink() or not target.is_file() or _digest(target.read_bytes()) != digest:
            raise RuntimeError(f"Managed file changed outside launcher: {target}")
        targets.append(target)
    staged_payloads = {target: target.read_bytes() for target in targets}
    restored: list[Path] = []
    try:
        for target in targets:
            relative = target.relative_to(game).as_posix()
            restored.append(target)
            if relative in backups:
                _copy_backup(backups[relative], target)
            else:
                target.unlink()
        state["managed_files"] = {}
        state["original_files"] = {}
        state["backup_id"] = ""
        state["installed_game_dir"] = ""
        save_state(state_path, state)
    except Exception as error:
        rollback_errors = []
        for target in reversed(restored):
            try:
                _write_payload(target, staged_payloads[target])
            except OSError as rollback_error:
                rollback_errors.append(f"{target}: {rollback_error}")
        if rollback_errors:
            raise RuntimeError("Restore failed and rollback is incomplete: " +
                               "; ".join(rollback_errors)) from error
        raise
    _remove_empty_vr_directories(game)
    return restored


def restore_install(game: Path, state_path: Path) -> list[Path]:
    with _transaction_lock(state_path):
        return _restore_install_unlocked(game, state_path)
