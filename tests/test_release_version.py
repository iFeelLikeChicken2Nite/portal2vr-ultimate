"""Incremental main releases and retry-safe version selection."""
import unittest
from tools.release_version import select_release_version

COMMIT = "1" * 40


class ReleaseVersionTests(unittest.TestCase):
    def test_first_release_is_initial_version(self):
        self.assertEqual(select_release_version([], COMMIT), "v0.1.0")

    def test_next_release_increments_highest_stable_patch(self):
        releases = [{"tag_name": "v0.9.9"}, {"tag_name": "v1.2.3"}, {"tag_name": "v1.2.2"},
                    {"tag_name": "v99.0.0", "prerelease": True}, {"tag_name": "other"}]
        self.assertEqual(select_release_version(releases, COMMIT), "v1.2.4")

    def test_retry_reuses_existing_commit_release_including_draft(self):
        for draft in (False, True):
            self.assertEqual(select_release_version([
                {"tag_name": "v1.2.3", "target_commitish": COMMIT, "draft": draft},
                {"tag_name": "v1.2.4", "target_commitish": "2" * 40}], COMMIT), "v1.2.3")

    def test_other_commit_draft_reserves_version(self):
        self.assertEqual(select_release_version([
            {"tag_name": "v0.1.0", "draft": True, "target_commitish": "2" * 40}], COMMIT), "v0.1.1")

    def test_duplicate_release_same_commit_is_reported(self):
        with self.assertRaisesRegex(ValueError, "Multiple"):
            select_release_version([{"tag_name": "v0.1.0", "target_commitish": COMMIT},
                                    {"tag_name": "v0.1.1", "target_commitish": COMMIT}], COMMIT)

    def test_invalid_api_result_and_commit_are_reported(self):
        with self.assertRaises(ValueError):
            select_release_version({}, COMMIT)
        with self.assertRaises(ValueError):
            select_release_version([], "main")


if __name__ == "__main__":
    unittest.main()
