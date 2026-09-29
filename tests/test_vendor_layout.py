"""Repository-level checks for sources needed by the Windows VR build."""

from pathlib import Path
import subprocess
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


class VendoredSourcesTests(unittest.TestCase):
    def test_dxvk_and_minhook_are_tracked_as_regular_files(self):
        result = subprocess.run(
            ["git", "ls-files", "--stage", "--", "dxvk", "thirdparty/minhook"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        modes = {}
        for line in result.stdout.splitlines():
            metadata, path = line.split("\t", 1)
            modes[path] = metadata.split(" ", 1)[0]

        for path in (
            "dxvk/LICENSE",
            "dxvk/src/d3d9/d3d9_device.cpp",
            "dxvk/src/d3d9/d3d9_interface.cpp",
            "dxvk/src/d3d9/d3d9_vr.cpp",
            "thirdparty/minhook/LICENSE.txt",
            "thirdparty/minhook/src/hook.c",
        ):
            with self.subTest(path=path):
                self.assertIn(modes.get(path), {"100644", "100755"})
                self.assertTrue((ROOT / path).is_file())

        self.assertNotIn("160000", modes.values())
        self.assertFalse((ROOT / ".gitmodules").exists())

    def test_project_does_not_mutate_vendored_dxvk_before_compilation(self):
        project = ET.parse(ROOT / "L4D2VR" / "l4d2vr.vcxproj")
        targets = project.findall("{http://schemas.microsoft.com/developer/msbuild/2003}Target")
        self.assertNotIn(
            "PrepareDxvkViewportGuard",
            {target.get("Name") for target in targets},
        )
        self.assertFalse((ROOT / "tools" / "prepare_dxvk.ps1").exists())
        self.assertFalse((ROOT / "tools" / "dxvk-viewport-readiness.patch").exists())


if __name__ == "__main__":
    unittest.main()
