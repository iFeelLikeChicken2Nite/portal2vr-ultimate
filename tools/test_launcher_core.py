"""Pure configuration and file operations for the local test launcher."""

from dataclasses import dataclass
import json
import os
from pathlib import Path


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
                          ("game_dir", str), ("steam_exe", str), ("managed_files", dict)):
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
