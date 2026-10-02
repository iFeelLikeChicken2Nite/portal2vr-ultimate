"""Offline user settings and launcher tests. Only temporary game files are used."""

import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from tools import test_launcher_core as core
from tests.test_test_launcher import fixture_install

REPO = Path(__file__).resolve().parent.parent
TEMPLATE = (REPO / "L4D2VR/config.txt").read_text(encoding="utf-8")


def config_values(text):
    return dict(line.split("#", 1)[0].strip().split("=", 1)
                for line in text.splitlines() if "=" in line.split("#", 1)[0])


def complete_fixture(root):
    repo, game, steam = fixture_install(root)
    (repo / "L4D2VR/config.txt").write_text(TEMPLATE, encoding="utf-8")
    return repo, game, steam


class SettingsTests(unittest.TestCase):
    def setUp(self):
        from importlib.util import find_spec
        self.assertIsNotNone(find_spec("tools.launcher_settings"),
                             "The validated user settings component is missing")
        from tools import launcher_settings
        self.settings = launcher_settings

    def test_recommended_config_emits_confirmed_setup_and_every_runtime_key(self):
        with TemporaryDirectory() as temporary:
            repo, game, _ = complete_fixture(Path(temporary))
            planned = core.plan_install(repo, game, "baseline",
                                        config_values=self.settings.recommended_settings())
            result = config_values(planned[game / "VR/config.txt"].decode("utf-8"))
            self.assertEqual(result.keys(), config_values(TEMPLATE).keys())
            for key, want in {
                "TrackingMode": "Seated", "RoomscaleMode": "ActiveExperimental",
                "PortalOrientationMode": "LegacyYaw", "6DOF": "true",
                "ExperimentalWorldAimMarker": "true", "ExperimentalStereoReticle": "false",
                "ExperimentalViewmodelAlignment": "true", "AimFromViewmodelMuzzle": "true",
                "ExperimentalHUDOverlay": "true", "ExperimentalPortalShotHaptics": "true",
                "VerboseDiagnostics": "false", "RenderWindow": "0",
            }.items():
                self.assertEqual(result[key], want, key)
            for key, want in {"HUDDistanceMeters": 1.25, "HUDWidthMeters": 1.65,
                              "HUDVerticalOffsetMeters": .1, "ViewmodelPosCustomOffsetX": -4.5,
                              "ViewmodelPosCustomOffsetY": 1, "ViewmodelPosCustomOffsetZ": -1.5}.items():
                self.assertEqual(float(result[key]), want, key)

    def test_invalid_values_are_rejected_instead_of_emitted_to_game(self):
        invalid = {"TurnSpeed": ["0", "2.01", "nan", "1_0", "1x"],
                   "SnapTurnAngle": ["0", "181"], "VRScale": ["0", "201", "inf"],
                   "IPDScale": ["0.49", "1.51"], "AntiAliasing": ["1", "16", "2.0"],
                   "AimMode": ["3", "-1"], "TrackingMode": ["Flying"],
                   "SnapTurning": ["yes", "1"], "HUDWidthMeters": ["0", "2.6"],
                   "ControllerPitchDegrees": ["61"], "PortalShotHapticDurationSeconds": ["0.16"],
                   "ViewmodelAngCustomOffsetZ": ["181"]}
        for key, bad_values in invalid.items():
            for value in bad_values:
                with self.subTest(key=key, value=value):
                    values = self.settings.recommended_settings()
                    values[key] = value
                    with self.assertRaisesRegex(ValueError, key):
                        self.settings.validate_settings(values)

    def test_boundaries_and_custom_numbers_are_normalized(self):
        values = self.settings.recommended_settings()
        values.update(TurnSpeed=" 2.000 ", SnapTurnAngle="180", VRScale="200",
                      IPDScale="0.5", AntiAliasing="8", AimMode="0")
        normalized = self.settings.validate_settings(values)
        self.assertEqual(float(normalized["TurnSpeed"]), 2)
        self.assertEqual(normalized["AntiAliasing"], "8")
        self.assertNotIn(" ", normalized["TurnSpeed"])

    def test_incomplete_unknown_and_incompatible_settings_fail(self):
        changes = [{"6DOF": "false"}, {"PortalOrientationMode": "FullRotation"},
                   {"ExperimentalViewmodelAlignment": "false"}, {"ThirdAttack": "true"}]
        for change in changes:
            values = {**self.settings.recommended_settings(), **change}
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.settings.validate_settings(values)
        values = self.settings.recommended_settings()
        del values["AimMode"]
        with self.assertRaises(ValueError):
            self.settings.validate_settings(values)

    def test_preferences_roundtrip_separate_from_install_state(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            legacy = core.load_state(Path(temporary) / "legacy.json")
            legacy["profile"] = "roomscale_active_native_aim"
            preferences = self.settings.load_preferences(path, legacy)
            self.assertEqual(preferences["values"]["ExperimentalWorldAimMarker"], "true")
            self.assertFalse(path.exists())
            preferences["values"]["SnapTurning"] = "true"
            self.settings.save_preferences(path, preferences)
            self.assertEqual(self.settings.load_preferences(path, legacy), preferences)
            self.assertFalse((Path(temporary) / "legacy.json").exists())

    def test_invalid_save_and_corrupt_load_do_not_replace_preferences(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            prefs = self.settings.load_preferences(path, core.load_state(path.parent / "state"))
            self.settings.save_preferences(path, prefs)
            original = path.read_bytes()
            prefs["values"]["TurnSpeed"] = "NaN"
            with self.assertRaises(ValueError):
                self.settings.save_preferences(path, prefs)
            self.assertEqual(path.read_bytes(), original)
            path.write_bytes(b"broken json")
            with self.assertRaisesRegex(ValueError, "preferences"):
                self.settings.load_preferences(path, {})
            prefs["values"]["TurnSpeed"] = "0.15"
            with self.assertRaisesRegex(ValueError, "preferences"):
                self.settings.save_preferences(path, prefs)
            self.assertEqual(path.read_bytes(), b"broken json")

    def test_failed_preferences_replace_retains_previous_file(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "settings.json"
            prefs = self.settings.load_preferences(path, core.load_state(path.parent / "state"))
            self.settings.save_preferences(path, prefs)
            original = path.read_bytes()
            prefs["values"]["SnapTurning"] = "true"
            with patch.object(self.settings.os, "replace", side_effect=OSError("replace denied")):
                with self.assertRaises(OSError):
                    self.settings.save_preferences(path, prefs)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(list(path.parent.glob("*.tmp")), [])

    def test_custom_install_retains_first_original_snapshot_and_restores(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = complete_fixture(root)
            state_path = root / "state.json"
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"first original")
            core.stage_install(repo, game, state_path, "baseline")
            before = core.load_state(state_path)
            values = self.settings.recommended_settings()
            values["SnapTurnAngle"] = "30"
            core.stage_install(repo, game, state_path, "baseline", config_values=values)
            after = core.load_state(state_path)
            self.assertEqual(after["backup_id"], before["backup_id"])
            self.assertEqual(after["original_files"], before["original_files"])
            self.assertEqual(config_values((game / "VR/config.txt").read_text())["SnapTurnAngle"], "30")
            core.restore_install(game, state_path)
            self.assertEqual(original.read_bytes(), b"first original")
            self.assertFalse((game / "VR").exists())

    def test_invalid_custom_install_writes_no_game_files_or_snapshot(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = complete_fixture(root)
            values = self.settings.recommended_settings()
            values["IPDScale"] = "inf"
            with self.assertRaises(ValueError):
                core.stage_install(repo, game, root / "state.json", "baseline", config_values=values)
            self.assertFalse((game / "VR").exists())
            self.assertFalse((game / "bin/d3d9.dll").exists())
            self.assertFalse((root / ".launcher-backups").exists())

    def test_custom_install_keeps_external_edit_and_transaction_guards(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = complete_fixture(root)
            state_path = root / "state.json"
            values = self.settings.recommended_settings()
            with core._transaction_lock(state_path):
                with self.assertRaisesRegex(RuntimeError, "in progress"):
                    core.stage_install(repo, game, state_path, "baseline", config_values=values)
            core.stage_install(repo, game, state_path, "baseline", config_values=values)
            config = game / "VR/config.txt"
            config.write_bytes(b"user edit")
            with self.assertRaisesRegex(RuntimeError, "outside launcher"):
                core.stage_install(repo, game, state_path, "baseline", config_values=values)
            self.assertEqual(config.read_bytes(), b"user edit")


if __name__ == "__main__":
    unittest.main()
