"""Terminate a fixture-only launcher operation at a selected disk boundary."""

import os
from pathlib import Path
import sys

from tools import test_launcher_core as core


def main() -> None:
    root, operation, boundary, limit = Path(sys.argv[1]), sys.argv[2], sys.argv[3], int(sys.argv[4])
    game, state = root / "Portal 2", root / "state.json"
    calls = 0

    def hit() -> None:
        nonlocal calls
        calls += 1
        if calls == limit:
            os._exit(47)

    if boundary in ("before_backup", "after_backup"):
        original = core._create_backup

        def stop_backup(*args):
            if boundary == "before_backup":
                hit()
            result = original(*args)
            if boundary == "after_backup":
                hit()
            return result

        core._create_backup = stop_backup
    elif boundary == "during_backup":
        original = core.shutil.copyfile

        def stop_copy(*args, **kwargs):
            result = original(*args, **kwargs)
            hit()
            return result

        core.shutil.copyfile = stop_copy
    elif boundary == "after_write":
        original = core._write_payload

        def stop_write(*args):
            result = original(*args)
            hit()
            return result

        core._write_payload = stop_write
    elif boundary in ("before_replace", "before_state_replace"):
        original = core.os.replace

        def stop_replace(source, target):
            if (boundary == "before_replace" and Path(target).is_relative_to(game)
                    or boundary == "before_state_replace" and Path(target) == state):
                hit()
            return original(source, target)

        core.os.replace = stop_replace
    elif boundary in ("before_state", "after_state"):
        original = core.save_state

        def stop_state(path, data):
            if path == state and boundary == "before_state":
                hit()
            result = original(path, data)
            if path == state and boundary == "after_state":
                hit()
            return result

        core.save_state = stop_state
    elif boundary == "after_restore":
        original = core._copy_backup

        def stop_restore(*args):
            result = original(*args)
            hit()
            return result

        core._copy_backup = stop_restore
    else:
        raise ValueError(boundary)

    if operation == "install":
        core.stage_install(root / "repo", game, state, "baseline")
    elif operation == "reapply":
        core.stage_install(root / "repo", game, state, "hud")
    elif operation == "restore":
        core.restore_install(game, state)
    elif operation == "recover":
        core.recover_pending_transaction(state)
    else:
        raise ValueError(operation)
    raise AssertionError(f"Fault boundary was not reached: {boundary} {limit}")


if __name__ == "__main__":
    main()
