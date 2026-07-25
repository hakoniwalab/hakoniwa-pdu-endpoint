"""Hakoniwa PDU Endpoint Python utilities."""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Iterable


_DLL_DIRECTORY_HANDLES = []


def _existing_dirs(paths: Iterable[Path]) -> list[Path]:
    seen: set[Path] = set()
    result: list[Path] = []
    for path in paths:
        try:
            resolved = path.expanduser().resolve()
        except OSError:
            continue
        if not resolved.is_dir() or resolved in seen:
            continue
        seen.add(resolved)
        result.append(resolved)
    return result


def _candidate_windows_runtime_dirs() -> list[Path]:
    candidates: list[Path] = []

    runtime_dirs = os.environ.get("HAKO_PDU_ENDPOINT_RUNTIME_DIRS", "")
    if runtime_dirs:
        candidates.extend(Path(value) for value in runtime_dirs.split(os.pathsep) if value)

    lib_dir = os.environ.get("HAKO_PDU_ENDPOINT_LIB_DIR", "")
    if lib_dir:
        candidates.append(Path(lib_dir))

    shared_lib = os.environ.get("HAKO_PDU_ENDPOINT_SHARED_LIB", "")
    if shared_lib:
        candidates.append(Path(shared_lib).expanduser().parent)

    core_root = (
        os.environ.get("HAKONIWA_CORE_ROOT", "")
        or os.environ.get("HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT", "")
    )
    if core_root:
        root = Path(core_root)
        candidates.extend((root / "bin", root / "lib"))

    vcpkg_root = os.environ.get("VCPKG_ROOT", "") or os.environ.get("VCPKG_INSTALLATION_ROOT", "")
    if vcpkg_root:
        triplet = os.environ.get("VCPKG_TARGET_TRIPLET", "x64-windows")
        candidates.append(Path(vcpkg_root) / "installed" / triplet / "bin")

    return _existing_dirs(candidates)


def _register_windows_runtime_dirs() -> None:
    if sys.platform != "win32" or not hasattr(os, "add_dll_directory"):
        return

    for runtime_dir in _candidate_windows_runtime_dirs():
        try:
            # Keep the handles alive for the lifetime of the package. Closing or
            # garbage-collecting them removes the directory from the DLL search path.
            _DLL_DIRECTORY_HANDLES.append(os.add_dll_directory(str(runtime_dir)))
        except (FileNotFoundError, OSError):
            continue


_register_windows_runtime_dirs()

__all__ = [
    "c_endpoint",
    "endpoint_container",
    "validate_json",
    "validate_pdudef",
]
