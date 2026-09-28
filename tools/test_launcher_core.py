"""Pure configuration and file operations for the local test launcher."""

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import tempfile


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
    "ExperimentalPortalShotHaptics": "false",
    "AimMode": "2",
    "RenderWindow": "0",
}

PROFILES = {
    "baseline": Profile("Baseline (M0/M1)", {}),
    "standing": Profile("M2 Standing", {"TrackingMode": "Standing"}),
    "observe": Profile("M3 Observe (bez ruchu)", {"RoomscaleMode": "Observe"}),
    "full_rotation": Profile("M4 FullRotation", {"PortalOrientationMode": "FullRotation"}, True),
    "yaw_only": Profile("M4 YawOnly", {"PortalOrientationMode": "YawOnly"}, True),
    "preserve_horizon": Profile("M4 PreserveHorizon", {"PortalOrientationMode": "PreserveHorizon"}, True),
    "hud": Profile("M5 HUD", {"ExperimentalHUDOverlay": "true"}, True),
    "haptics": Profile("M6 Haptics", {"ExperimentalPortalShotHaptics": "true"}, True),
    "mirror": Profile("M6 Mirror ON", {"RenderWindow": "1"}, True),
}

CHECKLIST = (
    Check("M0/M1", "m1_launch", "Start -insecure, bez crasha; log zawiera rozpoznane hooki", "baseline"),
    Check("M0/M1", "m1_menu", "Menu i obraz w HMD są widoczne", "baseline"),
    Check("M0/M1", "m1_controller_loss", "Odłączenie/powrót kontrolera bez crasha", "baseline"),
    Check("M0/M1", "m1_shutdown", "Czyste wyjście z gry", "baseline"),
    Check("M2", "m2_seated", "Seated: głowa i obie dłonie poruszają się poprawnie", "baseline"),
    Check("M2", "m2_standing", "Standing: wysokość i recenter", "standing"),
    Check("M2", "m2_turn", "Snap/smooth turn i wybór kierunku ruchu", "baseline"),
    Check("M2", "m2_aim", "Celownik/laser pokrywa się z trafieniem portalu", "baseline"),
    Check("M3", "m3_observe", "Observe: loguje kroki bez ruchu gracza", "observe"),
    Check("M3", "m3_collision", "Brak aktywnego roomscale (oczekiwane ograniczenie)", "observe"),
    Check("M4", "m4_legacy", "LegacyYaw: przejście ściana-ściana", "baseline"),
    Check("M4", "m4_full", "FullRotation: ściana-ściana, orientacja głowy i dłoni", "full_rotation"),
    Check("M4", "m4_floor", "Portale podłoga/sufit: komfort i stereo", "preserve_horizon"),
    Check("M4", "m4_recenter", "Recenter przed/po portalu bez powtórzenia zdarzenia", "full_rotation"),
    Check("M5", "m5_hud", "HUD: czytelność, przezroczystość, skala", "hud"),
    Check("M5", "m5_subtitles", "Napisy: czytelność i zawijanie", "hud"),
    Check("M5", "m5_menu", "Pauza/menu: hover, klik, back, brak zablokowanego kliknięcia", "hud"),
    Check("M5", "m5_loss", "Menu po utracie kontrolera - klawiatura nadal działa", "hud"),
    Check("M6", "m6_shot", "Strzał portalem: pojedynczy impuls właściwej dłoni", "haptics"),
    Check("M6", "m6_loss", "Brak kontrolera: brak spamu i crasha haptyki", "haptics"),
    Check("M6", "m6_mirror", "Mirror off/on: obraz i UI, bez deklaracji przyspieszenia", "mirror"),
    Check("M6", "m6_perf", "Zapisano medianę i wysokie percentyle CPU/GPU", "mirror"),
)

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
                          ("installed_game_dir", str)):
        if key in loaded:
            if not isinstance(loaded[key], expected):
                raise ValueError(f"Invalid launcher state field: {key}")
            state[key] = loaded[key]
    if any(not isinstance(item, str) for item in state["checks"]):
        raise ValueError("Invalid launcher checklist entry")
    if any(not isinstance(key, str) or not isinstance(value, str)
           for key, value in state["managed_files"].items()):
        raise ValueError("Invalid launcher managed-file entry")
    return state


def save_state(path: Path, state: dict) -> None:
    """Atomically persist human observations and exact-file ownership."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    os.replace(temporary, path)


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
    return [str(steam), "-applaunch", "620", "-insecure"]


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
    if any(relative not in allowed for relative in managed):
        raise ValueError("Launcher state contains an unexpected managed path")
    if managed and state["installed_game_dir"] != str(game.resolve()):
        raise RuntimeError("Test files belong to another game directory; restore there first")
    return managed


def stage_install(repo: Path, game: Path, state_path: Path, profile: str) -> list[Path]:
    """Stage exact mod files, refusing unknown/modified targets and rolling back failures."""
    planned = plan_install(repo, game, profile)
    state = load_state(state_path)
    managed = _validate_managed(state, game)
    if managed.keys() - {target.relative_to(game).as_posix() for target in planned}:
        raise RuntimeError("Launcher version changed its file set; restore first")
    previous: dict[Path, bytes | None] = {}
    for target in planned:
        relative = target.relative_to(game).as_posix()
        if target.is_symlink():
            raise ValueError(f"Refusing symlink: {target}")
        if target.exists():
            contents = target.read_bytes()
            if relative not in managed:
                raise FileExistsError(f"Refusing to overwrite unmanaged file: {target}")
            if _digest(contents) != managed[relative]:
                raise RuntimeError(f"Managed file changed outside launcher: {target}")
            previous[target] = contents
        else:
            previous[target] = None
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


def restore_install(game: Path, state_path: Path) -> list[Path]:
    """Remove only unchanged files previously staged by this launcher."""
    state = load_state(state_path)
    managed = _validate_managed(state, game)
    targets: list[Path] = []
    for relative, digest in managed.items():
        target = _safe_target(game, relative)
        if target.exists() and _digest(target.read_bytes()) != digest:
            raise RuntimeError(f"Managed file changed outside launcher: {target}")
        targets.append(target)
    removed: list[Path] = []
    for target in targets:
        if target.exists():
            target.unlink()
            removed.append(target)
    _remove_empty_vr_directories(game)
    state["managed_files"] = {}
    state["installed_game_dir"] = ""
    save_state(state_path, state)
    return removed
