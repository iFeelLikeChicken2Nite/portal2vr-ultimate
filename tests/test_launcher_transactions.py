"""Real subprocess termination against isolated Portal 2 fixture files."""

from pathlib import Path
import json
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

from tools import test_launcher_core as core
from tests.test_test_launcher import fixture_install


class CrashRecoveryTests(unittest.TestCase):
    def crash(self, root: Path, operation: str, boundary: str, limit: int = 1) -> None:
        result = subprocess.run(
            [sys.executable, "-m", "tests.launcher_fault_worker", str(root),
             operation, boundary, str(limit)],
            cwd=Path(__file__).resolve().parents[1], check=False)
        self.assertEqual(result.returncode, 47)

    def test_first_replaced_file_recovers_original_after_process_exit(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, game, _ = fixture_install(root)
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"user-dll")
            state = root / "state.json"
            self.crash(root, "install", "after_write")
            self.assertEqual(original.read_bytes(), b"test-d3d9")

            core.restore_install(game, state)

            self.assertEqual(original.read_bytes(), b"user-dll")
            self.assertFalse((game / "bin/openvr_api.dll").exists())
            self.assertFalse(core.load_state(state)["managed_files"])

    def test_backup_interruptions_leave_game_intact_and_retry_preserves_original(self):
        for boundary in ("before_backup", "during_backup", "after_backup"):
            with self.subTest(boundary=boundary), TemporaryDirectory() as temporary:
                root = Path(temporary)
                repo, game, _ = fixture_install(root)
                original = game / "bin/d3d9.dll"
                original.write_bytes(b"user-dll")
                state = root / "state.json"
                self.crash(root, "install", boundary)
                self.assertEqual(original.read_bytes(), b"user-dll")
                self.assertFalse(core.load_state(state)["managed_files"])
                core.stage_install(repo, game, state, "baseline")
                core.restore_install(game, state)
                self.assertEqual(original.read_bytes(), b"user-dll")

    def test_install_replace_and_state_commit_boundaries_recover(self):
        for boundary, limit, committed in (
                ("before_replace", 1, False), ("after_write", 1, False),
                ("after_write", 4, False), ("after_write", 9, False),
                ("before_state", 1, False), ("before_state_replace", 1, False),
                ("after_state", 1, True)):
            with self.subTest(boundary=boundary, limit=limit), TemporaryDirectory() as temporary:
                root = Path(temporary)
                _, game, _ = fixture_install(root)
                original = game / "bin/d3d9.dll"
                original.write_bytes(b"user-dll")
                state = root / "state.json"
                self.crash(root, "install", boundary, limit)
                self.assertTrue(core.recover_pending_transaction(state))
                self.assertFalse(core.recover_pending_transaction(state))
                self.assertEqual(original.read_bytes(),
                                 b"test-d3d9" if committed else b"user-dll")
                self.assertEqual(bool(core.load_state(state)["managed_files"]), committed)
                if committed:
                    core.restore_install(game, state)
                    self.assertEqual(original.read_bytes(), b"user-dll")

    def test_reapply_interruptions_keep_first_original_snapshot(self):
        for boundary, committed in (("after_write", False),
                                    ("before_state", False), ("after_state", True)):
            with self.subTest(boundary=boundary), TemporaryDirectory() as temporary:
                root = Path(temporary)
                repo, game, _ = fixture_install(root)
                original = game / "bin/d3d9.dll"
                original.write_bytes(b"user-dll")
                state = root / "state.json"
                core.stage_install(repo, game, state, "baseline")
                first_id = core.load_state(state)["backup_id"]
                baseline = (game / "VR/config.txt").read_bytes()
                self.crash(root, "reapply", boundary)
                self.assertTrue(core.recover_pending_transaction(state))
                self.assertEqual(core.load_state(state)["backup_id"], first_id)
                self.assertIn(b"ExperimentalHUDOverlay=true" if committed else
                              b"ExperimentalHUDOverlay=false",
                              (game / "VR/config.txt").read_bytes())
                self.assertEqual((game / "VR/config.txt").read_bytes() == baseline,
                                 not committed)
                core.restore_install(game, state)
                self.assertEqual(original.read_bytes(), b"user-dll")

    def test_restore_interruptions_recover_or_complete_commit(self):
        for boundary, limit, committed in (
                ("before_replace", 1, False), ("after_restore", 1, False),
                ("after_restore", 2, False), ("before_state", 1, False),
                ("before_state_replace", 1, False),
                ("after_state", 1, True)):
            with self.subTest(boundary=boundary, limit=limit), TemporaryDirectory() as temporary:
                root = Path(temporary)
                repo, game, _ = fixture_install(root)
                originals = {"bin/d3d9.dll": b"user-dll",
                             "VR/config.txt": b"user-config"}
                for relative, contents in originals.items():
                    target = game / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(contents)
                state = root / "state.json"
                core.stage_install(repo, game, state, "baseline")
                self.crash(root, "restore", boundary, limit)
                self.assertTrue(core.recover_pending_transaction(state))
                self.assertEqual(bool(core.load_state(state)["managed_files"]), not committed)
                if not committed:
                    self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"test-d3d9")
                    core.restore_install(game, state)
                for relative, contents in originals.items():
                    self.assertEqual((game / relative).read_bytes(), contents)
                self.assertFalse((game / "bin/openvr_api.dll").exists())

    def test_changed_game_file_blocks_recovery_without_overwriting_edit(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, game, _ = fixture_install(root)
            state = root / "state.json"
            self.crash(root, "install", "after_write")
            target = game / "bin/d3d9.dll"
            target.write_bytes(b"user edit after crash")
            with self.assertRaisesRegex(RuntimeError, "outside interrupted"):
                core.recover_pending_transaction(state)
            self.assertEqual(target.read_bytes(), b"user edit after crash")
            self.assertTrue((root / "state.json.transaction.json").exists())

    def test_corrupt_journal_blocks_recovery_without_changing_game(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, game, _ = fixture_install(root)
            state = root / "state.json"
            self.crash(root, "install", "after_write")
            original = (game / "bin/d3d9.dll").read_bytes()
            journal_path = root / "state.json.transaction.json"
            journal = json.loads(journal_path.read_text())
            journal["files"]["../../outside.txt"] = journal["files"]["bin/d3d9.dll"]
            journal_path.write_text(json.dumps(journal))
            with self.assertRaisesRegex(RuntimeError, "file set"):
                core.recover_pending_transaction(state)
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), original)

    def test_journal_digest_mismatch_with_owned_state_blocks_recovery(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, game, _ = fixture_install(root)
            state = root / "state.json"
            self.crash(root, "install", "after_write")
            journal_path = root / "state.json.transaction.json"
            journal = json.loads(journal_path.read_text())
            journal["files"]["VR/config.txt"]["after"] = "0" * 64
            journal_path.write_text(json.dumps(journal))
            with self.assertRaisesRegex(RuntimeError, "ownership"):
                core.recover_pending_transaction(state)
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"test-d3d9")

    def test_restore_refuses_changed_backup_payload_before_clearing_ownership(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"user-dll")
            state = root / "state.json"
            core.stage_install(repo, game, state, "baseline")

            def wrong_copy(source, target):
                target.write_bytes(b"wrong backup bytes")

            with patch.object(core, "_copy_backup", side_effect=wrong_copy):
                with self.assertRaisesRegex(RuntimeError, "verification"):
                    core.restore_install(game, state)
            self.assertEqual(original.read_bytes(), b"wrong backup bytes")
            self.assertTrue(core.load_state(state)["managed_files"])
            self.assertTrue((root / "state.json.transaction.json").exists())

    def test_recovery_can_resume_after_another_process_exit(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, game, _ = fixture_install(root)
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"user-dll")
            state = root / "state.json"
            self.crash(root, "install", "after_write", 4)
            self.crash(root, "recover", "after_write", 1)
            self.assertTrue(core.recover_pending_transaction(state))
            self.assertEqual(original.read_bytes(), b"user-dll")
            self.assertFalse((game / "bin/openvr_api.dll").exists())
            self.assertFalse(core.load_state(state)["managed_files"])

    def test_checklist_save_cannot_hide_pending_install_recovery(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, game, _ = fixture_install(root)
            state = root / "state.json"
            self.crash(root, "install", "after_write")
            with self.assertRaisesRegex(RuntimeError, "requires recovery"):
                core.save_preferences(state, core.load_state(state))
            self.assertFalse(state.exists())
            self.assertEqual((game / "bin/d3d9.dll").read_bytes(), b"test-d3d9")

    def test_incomplete_existing_state_cannot_resnapshot_installed_mod_as_original(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"user-dll")
            state = root / "state.json"
            core.stage_install(repo, game, state, "baseline")
            backup_id = core.load_state(state)["backup_id"]
            state.write_text("{}", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "Incomplete launcher state"):
                core.stage_install(repo, game, state, "baseline")

            self.assertEqual(original.read_bytes(), b"test-d3d9")
            self.assertEqual(state.read_text(encoding="utf-8"), "{}")
            self.assertEqual({path.name for path in (root / ".launcher-backups").iterdir()
                              if path.name != ".transactions"}, {backup_id})

    def test_unmanaged_state_with_installed_game_path_cannot_resnapshot_mod(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"user-dll")
            state = root / "state.json"
            core.stage_install(repo, game, state, "baseline")
            backup_id = core.load_state(state)["backup_id"]
            invalid = core.load_state(state)
            invalid.update(managed_files={}, original_files={}, backup_id="")
            core.save_state(state, invalid)

            with self.assertRaisesRegex(ValueError, "inconsistent.*ownership"):
                core.stage_install(repo, game, state, "baseline")

            self.assertEqual(original.read_bytes(), b"test-d3d9")
            self.assertEqual({path.name for path in (root / ".launcher-backups").iterdir()
                              if path.name != ".transactions"}, {backup_id})

    def test_missing_state_with_retained_backup_cannot_resnapshot_installed_mod(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            repo, game, _ = fixture_install(root)
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"user-dll")
            state = root / "state.json"
            core.stage_install(repo, game, state, "baseline")
            backup_id = core.load_state(state)["backup_id"]
            state.unlink()

            with self.assertRaisesRegex(RuntimeError, "backup.*ownership"):
                core.stage_install(repo, game, state, "baseline")

            self.assertEqual(original.read_bytes(), b"test-d3d9")
            self.assertFalse(state.exists())
            self.assertEqual({path.name for path in (root / ".launcher-backups").iterdir()
                              if path.name != ".transactions"}, {backup_id})

    def test_missing_preimage_blocks_recovery_and_preserves_game(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, game, _ = fixture_install(root)
            original = game / "bin/d3d9.dll"
            original.write_bytes(b"user-dll")
            state = root / "state.json"
            self.crash(root, "install", "after_write")
            journal = json.loads((root / "state.json.transaction.json").read_text())
            preimage = root / ".launcher-backups/.transactions" / journal["id"] / "bin/d3d9.dll"
            preimage.unlink()
            with self.assertRaisesRegex(RuntimeError, "preimage"):
                core.recover_pending_transaction(state)
            self.assertEqual(original.read_bytes(), b"test-d3d9")
            self.assertTrue((root / "state.json.transaction.json").exists())


if __name__ == "__main__":
    unittest.main()
