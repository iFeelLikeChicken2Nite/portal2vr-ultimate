"""PyInstaller entry point; launcher data files remain beside the executable."""

from tools.launcher import main


if __name__ == "__main__":
    raise SystemExit(main())
