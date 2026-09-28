"""Host-only tests for the temporary Portal2VR test launcher."""

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from tools import test_launcher_core as core


TEMPLATE = (
    "TrackingMode=Seated # tracking\n"
    "RoomscaleMode=Off\n"
    "PortalOrientationMode=LegacyYaw\n"
    "ExperimentalHUDOverlay=false\n"
    "ExperimentalPortalShotHaptics=false\n"
    "AimMode=2\n"
    "RenderWindow=0\n"
    "TurnSpeed=0.15\n"
)


class ProfileTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
