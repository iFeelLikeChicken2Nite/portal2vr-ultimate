"""Locate external payloads and persistent state in source and binary packages."""
from pathlib import Path
import sys


def application_root() -> Path:
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parents[1]


def prepare_frozen_process() -> None:
    """Restore Windows DLL search before starting Steam or system utilities.

    Called after launcher imports have loaded the bundled Python/Tk libraries.
    PyInstaller's SetDllDirectory is otherwise inherited by child processes.
    """
    if sys.platform == "win32" and getattr(sys, "frozen", False):
        import ctypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.SetDllDirectoryW.argtypes = [ctypes.c_wchar_p]
        kernel.SetDllDirectoryW.restype = ctypes.c_int
        if not kernel.SetDllDirectoryW(None):
            raise ctypes.WinError(ctypes.get_last_error())
