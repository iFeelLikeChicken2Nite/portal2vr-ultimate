"""Choose an incremental stable version; reuse a commit's release on retries."""
import argparse
import json
from pathlib import Path
import re


def select_release_version(releases: list[dict], commit: str) -> str:
    if not isinstance(releases, list) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected GitHub releases array and exact commit SHA")
    versions = []
    matching = []
    for release in releases:
        if not isinstance(release, dict):
            raise ValueError("Invalid GitHub release record")
        match = re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)",
                             release.get("tag_name", ""))
        if not match or release.get("prerelease", False):
            continue
        version = tuple(int(part) for part in match.groups())
        versions.append(version)  # Drafts reserve their tag until a retry completes.
        if release.get("target_commitish") == commit:
            matching.append(release["tag_name"])
    if len(matching) > 1:
        raise ValueError("Multiple stable releases target this commit")
    if matching:
        return matching[0]
    if not versions:
        return "v0.1.0"
    major, minor, patch = max(versions)
    return f"v{major}.{minor}.{patch + 1}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--releases", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    pages = json.loads(args.releases.read_text(encoding="utf-8-sig"))
    if isinstance(pages, list) and pages and isinstance(pages[0], list):
        pages = [release for page in pages for release in page]
    print(select_release_version(pages, args.commit))


if __name__ == "__main__":
    main()
