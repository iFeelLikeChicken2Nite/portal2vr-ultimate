"""Host-only tests for the temporary Portal2VR test launcher."""

import unittest
import importlib
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from tools import test_launcher_core as core


TEMPLATE = (
    "TrackingMode=Seated # tracking\n"
    "RoomscaleMode=Off\n"
    "PortalOrientationMode=LegacyYaw\n"
    "ExperimentalHUDOverlay=false\n"
    "ExperimentalWorldAimMarker=false\n"
    "ExperimentalPortalShotHaptics=false\n"
    "AimMode=2\n"
    "RenderWindow=0\n"
    "TurnSpeed=0.15\n"
    "ViewmodelPosCustomOffsetX=0.0\n"
    "ViewmodelPosCustomOffsetY=0.0\n"
    "ViewmodelPosCustomOffsetZ=0.0\n"
)


class ProfileTests(unittest.TestCase):
    def test_gui_import_has_no_window_side_effect_and_checklist_ids_are_unique(self):
        gui = importlib.import_module("tools.test_launcher")
        self.assertTrue(callable(gui.main))
        ids = [item.id for item in core.CHECKLIST]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(item.profile in core.PROFILES for item in core.CHECKLIST))

    def test_baseline_resets_experiments_without_changing_other_settings(self):
        template = TEMPLATE.replace("RoomscaleMode=Off", "RoomscaleMode=Observe")
        template = template.replace("ExperimentalHUDOverlay=false", "ExperimentalHUDOverlay=true")
        rendered = core.render_config(template, "baseline")
        self.assertIn("TrackingMode=Seated # tracking\n", rendered)
        self.assertIn("RoomscaleMode=Off\n", rendered)
        self.assertIn("ExperimentalHUDOverlay=false\n", rendered)
        self.assertIn("TurnSpeed=0.15\n", rendered)

    def test_experimental_profile_enables_only_its_own_feature(self):
        rendered = core.render_config(TEMPLATE, "full_rotation")
        self.assertIn("PortalOrientationMode=FullRotation\n", rendered)
        self.assertIn("RoomscaleMode=Off\n", rendered)
        self.assertIn("ExperimentalHUDOverlay=false\n", rendered)
        self.assertIn("ExperimentalPortalShotHaptics=false\n", rendered)

    def test_aim_marker_profile_is_isolated_from_baseline(self):
        baseline = core.render_config(TEMPLATE, "baseline")
        aimed = core.render_config(TEMPLATE, "aim_marker")
        self.assertIn("ExperimentalWorldAimMarker=false\n", baseline)
        self.assertIn("ExperimentalWorldAimMarker=true\n", aimed)
        self.assertIn("ExperimentalHUDOverlay=false\n", aimed)
        self.assertIn("ExperimentalPortalShotHaptics=false\n", aimed)

    def test_model_alignment_profile_cancels_legacy_offset_without_changing_baseline(self):
        changed = TEMPLATE.replace("ViewmodelPosCustomOffsetX=0.0", "ViewmodelPosCustomOffsetX=-2.0")
        baseline = core.render_config(changed, "baseline")
        aligned = core.render_config(TEMPLATE, "aim_model_alignment")
        self.assertIn("ViewmodelPosCustomOffsetX=0.0\n", baseline)
        self.assertIn("ViewmodelPosCustomOffsetY=0.0\n", baseline)
        self.assertIn("ViewmodelPosCustomOffsetZ=0.0\n", baseline)
        self.assertIn("ViewmodelPosCustomOffsetX=-4.5\n", aligned)
        self.assertIn("ViewmodelPosCustomOffsetY=1.0\n", aligned)
        self.assertIn("ViewmodelPosCustomOffsetZ=-1.5\n", aligned)
        self.assertIn("ExperimentalWorldAimMarker=true\n", aligned)
        self.assertIn("ExperimentalHUDOverlay=false\n", aligned)

    def test_missing_or_duplicate_required_key_refuses_partial_config(self):
        with self.assertRaisesRegex(ValueError, "Missing config keys: RenderWindow"):
            core.render_config(TEMPLATE.replace("RenderWindow=0\n", ""), "baseline")
        with self.assertRaisesRegex(ValueError, "Duplicate config key: RoomscaleMode"):
            core.render_config(TEMPLATE + "RoomscaleMode=Observe\n", "baseline")


class StateTests(unittest.TestCase):
    def test_checklist_and_notes_survive_restart(self):
        with TemporaryDirectory() as temporary:
            path = Path(temporary) / "state.json"
            state = core.load_state(path)
            self.assertEqual(state["checks"], [])
            state["checks"] = ["m1_start"]
            state["notes"] = "laser drifts after recenter"
            state["profile"] = "standing"
            core.save_state(path, state)
            loaded = core.load_state(path)
            self.assertEqual(loaded["checks"], ["m1_start"])
            self.assertEqual(loaded["notes"], "laser drifts after recenter")
            self.assertEqual(loaded["profile"], "standing")

    def test_saving_stale_ui_state_preserves_install_ownership(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            stale = core.load_state(state_path)
            core.stage_install(repo, game, state_path, "baseline")
            stale["notes"] = "manual observation"
            core.save_preferences(state_path, stale)
            current = core.load_state(state_path)
            self.assertEqual(current["notes"], "manual observation")
            self.assertEqual(len(current["managed_files"]), 9)
            self.assertTrue(current["backup_id"])


def fixture_install(root: Path) -> tuple[Path, Path, Path]:
    repo = root / "repo"
    game = root / "Portal 2"
    steam = root / "Steam" / "steam.exe"
    sources = {
        "Release/d3d9.dll": b"test-d3d9",
        "thirdparty/openvr/bin/win32/openvr_api.dll": b"test-openvr",
        "L4D2VR/config.txt": TEMPLATE.encode("utf-8"),
        "L4D2VR/manifest.vrmanifest": b"{}",
        "L4D2VR/portal2vr_capsule_main.png": b"test-image",
        "L4D2VR/SteamVRActionManifest/action_manifest.json": b"{}",
        "L4D2VR/SteamVRActionManifest/bindings_knuckles.json": b"{}",
        "L4D2VR/SteamVRActionManifest/bindings_oculus_touch.json": b"{}",
        "L4D2VR/SteamVRActionManifest/bindings_vive_cosmos_controller.json": b"{}",
    }
    for relative, contents in sources.items():
        target = repo / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(contents)
    (game / "bin").mkdir(parents=True)
    (game / "portal2.exe").write_bytes(b"test-game")
    steam.parent.mkdir(parents=True)
    steam.write_bytes(b"test-steam")
    return repo, game, steam


class InstallTests(unittest.TestCase):
    def test_stage_and_restore_only_launcher_files(self):
        with TemporaryDirectory() as temporary:
            repo, game, steam = fixture_install(Path(temporary))
            state_path = Path(temporary) / "state.json"
            core.validate_launch_paths(repo, game, steam)
            installed = core.stage_install(repo, game, state_path, "full_rotation")
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"test-d3d9")
            self.assertEqual((game / "bin/openvr_api.dll").read_bytes(), b"test-openvr")
            self.assertIn("PortalOrientationMode=FullRotation",
                          (game / "VR/config.txt").read_text(encoding="utf-8"))
            self.assertEqual(len(installed), 9)
            removed = core.restore_install(game, state_path)
            self.assertEqual(len(removed), 9)
            self.assertFalse((game / "bin/d3d9.dll").exists())
            self.assertFalse((game / "VR/config.txt").exists())
            self.assertTrue((game / "portal2.exe").exists())

    def test_original_files_are_backed_up_and_restored(self):
        with TemporaryDirectory() as temporary:
            repo, game, _ = fixture_install(Path(temporary))
            state_path = Path(temporary) / "state.json"
            (game / "bin/d3d9.dll").write_bytes(b"user-dll")
            (game / "VR").mkdir()
            (game / "VR/config.txt").write_bytes(b"user-config")
            core.stage_install(repo, game, state_path, "baseline")
            state = core.load_state(state_path)
            backup = state_path.parent / ".launcher-backups" / state["backup_id"]
            self.assertEqual((backup / "bin/d3d9.dll").read_bytes(), b"user-dll")
            self.assertEqual((backup / "VR/config.txt").read_bytes(), b"user-config")
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"test-d3d9")
            core.restore_install(game, state_path)
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"user-dll")
            self.assertEqual((game / "VR/config.txt").read_bytes(), b"user-config")
            self.assertFalse((game / "bin/openvr_api.dll").exists())
            self.assertTrue((backup / "bin/d3d9.dll").exists())

    def test_corrupt_backup_blocks_restore_without_changing_game(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            (game / "bin/d3d9.dll").write_bytes(b"user-dll")
            core.stage_install(repo, game, state_path, "baseline")
            state = core.load_state(state_path)
            backup = root / ".launcher-backups" / state["backup_id"]
            (backup / "bin/d3d9.dll").write_bytes(b"corrupt")
            with self.assertRaisesRegex(RuntimeError, "Backup.*changed"):
                core.restore_install(game, state_path)
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"test-d3d9")

    def test_profile_change_preserves_first_original_backup(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            (game / "VR").mkdir()
            (game / "VR/config.txt").write_bytes(b"user-config")
            core.stage_install(repo, game, state_path, "baseline")
            first_id = core.load_state(state_path)["backup_id"]
            core.stage_install(repo, game, state_path, "hud")
            self.assertEqual(core.load_state(state_path)["backup_id"], first_id)
            core.restore_install(game, state_path)
            self.assertEqual((game / "VR/config.txt").read_bytes(), b"user-config")

    def test_invalid_backup_identifier_cannot_escape_backup_root(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            core.stage_install(repo, game, state_path, "baseline")
            state = core.load_state(state_path)
            state["backup_id"] = "../../outside"
            core.save_state(state_path, state)
            with self.assertRaises(ValueError):
                core.restore_install(game, state_path)

    def test_incomplete_managed_file_state_refuses_restore(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            core.stage_install(repo, game, state_path, "baseline")
            state = core.load_state(state_path)
            state["managed_files"].pop("bin/d3d9.dll")
            core.save_state(state_path, state)
            with self.assertRaisesRegex(ValueError, "incomplete"):
                core.restore_install(game, state_path)
            self.assertTrue((game / "bin/d3d9.dll").exists())

    def test_stage_refuses_a_concurrent_transaction(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            with core._transaction_lock(state_path):
                with self.assertRaisesRegex(RuntimeError, "in progress"):
                    core.stage_install(repo, game, state_path, "baseline")
            self.assertFalse((game / "bin/d3d9.dll").exists())

    def test_failed_restore_rolls_back_to_staged_files(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            (game / "bin/d3d9.dll").write_bytes(b"user-dll")
            core.stage_install(repo, game, state_path, "baseline")
            real_copy = core._copy_backup

            def fail_after_copy(source, target):
                real_copy(source, target)
                raise OSError("simulated restore failure")

            with patch.object(core, "_copy_backup", side_effect=fail_after_copy):
                with self.assertRaisesRegex(OSError, "simulated restore failure"):
                    core.restore_install(game, state_path)
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"test-d3d9")
            self.assertEqual((game / "bin/openvr_api.dll").read_bytes(), b"test-openvr")
            self.assertTrue(core.load_state(state_path)["managed_files"])

    def test_failed_stage_restores_original_files_and_retains_backup(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            (game / "bin/d3d9.dll").write_bytes(b"user-dll")
            real_write = core._write_payload
            calls = 0

            def fail_after_second_write(target, payload):
                nonlocal calls
                calls += 1
                real_write(target, payload)
                if calls == 2:
                    raise OSError("simulated copy failure")

            with patch.object(core, "_write_payload", side_effect=fail_after_second_write):
                with self.assertRaisesRegex(OSError, "simulated copy failure"):
                    core.stage_install(repo, game, state_path, "baseline")
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"user-dll")
            self.assertFalse((game / "bin/openvr_api.dll").exists())
            self.assertFalse(state_path.exists())
            backups = list((root / ".launcher-backups").glob("*/bin/d3d9.dll"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), b"user-dll")

    def test_user_edited_managed_config_is_preserved(self):
        with TemporaryDirectory() as temporary:
            repo, game, _ = fixture_install(Path(temporary))
            state_path = Path(temporary) / "state.json"
            core.stage_install(repo, game, state_path, "baseline")
            config = game / "VR/config.txt"
            config.write_text("user-edited\n", encoding="utf-8")
            with self.assertRaises(RuntimeError):
                core.stage_install(repo, game, state_path, "hud")
            with self.assertRaises(RuntimeError):
                core.restore_install(game, state_path)
            self.assertEqual(config.read_text(encoding="utf-8"), "user-edited\n")

    def test_missing_source_or_steam_blocks_before_staging(self):
        with TemporaryDirectory() as temporary:
            repo, game, steam = fixture_install(Path(temporary))
            steam.unlink()
            with self.assertRaises(FileNotFoundError):
                core.validate_launch_paths(repo, game, steam)
            steam.write_bytes(b"test-steam")
            (repo / "thirdparty/openvr/bin/win32/openvr_api.dll").unlink()
            with self.assertRaises(FileNotFoundError):
                core.validate_launch_paths(repo, game, steam)
            self.assertFalse((game / "VR").exists())

    def test_steam_launch_command_preserves_original_vr_video_options(self):
        steam = Path(r"C:\Program Files (x86)\Steam\steam.exe")
        self.assertEqual(core.build_steam_command(steam),
                         [str(steam), "-applaunch", "620", "-insecure", "-window", "-novid",
                          "+mat_motion_blur_percent_of_screen_max", "0", "+mat_queue_mode", "0",
                          "+mat_vsync", "0", "+mat_antialias", "0",
                          "+mat_grain_scale_override", "0", "-width", "1280", "-height", "720"])

    def test_failed_stage_rolls_back_new_files(self):
        with TemporaryDirectory() as temporary:
            repo, game, _ = fixture_install(Path(temporary))
            state_path = Path(temporary) / "state.json"
            real_write = core._write_payload
            calls = 0

            def fail_after_second_write(target, payload):
                nonlocal calls
                calls += 1
                real_write(target, payload)
                if calls == 2:
                    raise OSError("simulated copy failure")

            with patch.object(core, "_write_payload", side_effect=fail_after_second_write):
                with self.assertRaisesRegex(OSError, "simulated copy failure"):
                    core.stage_install(repo, game, state_path, "baseline")
            self.assertFalse((game / "bin/d3d9.dll").exists())
            self.assertFalse((game / "bin/openvr_api.dll").exists())
            self.assertFalse((game / "VR").exists())
            self.assertFalse(state_path.exists())

    def test_staging_does_not_overwrite_unrelated_temp_named_file(self):
        with TemporaryDirectory() as temporary:
            repo, game, _ = fixture_install(Path(temporary))
            unrelated = game / "bin/d3d9.dll.portal2vr-tmp"
            unrelated.write_bytes(b"user-temporary-file")
            core.stage_install(repo, game, Path(temporary) / "state.json", "baseline")
            self.assertEqual(unrelated.read_bytes(), b"user-temporary-file")

    def test_corrupted_manifest_cannot_delete_outside_game(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            state_path = root / "state.json"
            outside = root / "outside.txt"
            outside.write_bytes(b"keep")
            core.stage_install(repo, game, state_path, "baseline")
            state = core.load_state(state_path)
            state["managed_files"]["../../outside.txt"] = core._digest(b"keep")
            core.save_state(state_path, state)
            with self.assertRaises(ValueError):
                core.restore_install(game, state_path)
            self.assertEqual(outside.read_bytes(), b"keep")


if __name__ == "__main__":
    unittest.main()
