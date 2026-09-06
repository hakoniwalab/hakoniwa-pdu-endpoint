#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, Mapping


DEFAULT_CONFIG: Dict[str, Any] = {
    "version": 1,
    "build": {
        "type": "Release",
        "dir": "build",
        "shared": "auto",
        "parallel": 0,
    },
    "bindings": {"python": True},
    "features": {
        "hakoniwa_core": False,
        "zenoh": False,
        "mqtt": False,
    },
    "validation": {
        "tests": False,
        "examples": False,
        "tools": False,
        "benchmarks": False,
        "python_import": True,
    },
    "paths": {
        "hakoniwa_core_root": "",
        "vcpkg_root": "",
    },
}

VALID_BUILD_TYPES = {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"}
VALID_SHARED = {"auto", True, False}


class ConfigError(RuntimeError):
    pass


def _strip_comment(text: str) -> str:
    quote: str | None = None
    escaped = False
    out = []
    for ch in text:
        if escaped:
            out.append(ch)
            escaped = False
            continue
        if ch == "\\" and quote:
            out.append(ch)
            escaped = True
            continue
        if ch in {"'", '"'}:
            if quote is None:
                quote = ch
            elif quote == ch:
                quote = None
            out.append(ch)
            continue
        if ch == "#" and quote is None:
            break
        out.append(ch)
    return "".join(out).rstrip()


def _parse_scalar(text: str) -> Any:
    value = text.strip()
    if value == "":
        return {}
    lowered = value.lower()
    if lowered == "true":
        return True
    if lowered == "false":
        return False
    if lowered in {"null", "~"}:
        return None
    if value.startswith(('"', "'")):
        if len(value) < 2 or value[-1] != value[0]:
            raise ConfigError(f"unterminated quoted scalar: {value}")
        if value[0] == '"':
            try:
                return json.loads(value)
            except json.JSONDecodeError as exc:
                raise ConfigError(f"invalid quoted scalar: {value}") from exc
        return value[1:-1].replace("''", "'")
    try:
        return int(value)
    except ValueError:
        return value


def load_simple_yaml(path: Path) -> Dict[str, Any]:
    """Load the small mapping/scalar YAML subset used by hakoniwa-build.yaml.

    The build manifest intentionally avoids sequences, anchors, tags, and other
    advanced YAML features so the bootstrap configure tool has no third-party
    Python dependency.
    """
    root: Dict[str, Any] = {}
    stack: list[tuple[int, Dict[str, Any]]] = [(-1, root)]

    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if "\t" in raw:
            raise ConfigError(f"{path}:{lineno}: tabs are not allowed")
        line = _strip_comment(raw)
        if not line.strip():
            continue
        stripped = line.lstrip(" ")
        indent = len(line) - len(stripped)
        if stripped.startswith("-"):
            raise ConfigError(f"{path}:{lineno}: sequences are not supported in build manifest v1")
        if ":" not in stripped:
            raise ConfigError(f"{path}:{lineno}: expected 'key: value'")
        key, raw_value = stripped.split(":", 1)
        key = key.strip()
        if not key:
            raise ConfigError(f"{path}:{lineno}: empty key")

        while stack and indent <= stack[-1][0]:
            stack.pop()
        if not stack:
            raise ConfigError(f"{path}:{lineno}: invalid indentation")
        parent = stack[-1][1]
        if key in parent:
            raise ConfigError(f"{path}:{lineno}: duplicate key: {key}")

        parsed = _parse_scalar(raw_value)
        parent[key] = parsed
        if isinstance(parsed, dict):
            stack.append((indent, parsed))

    return root


def _merge_known(defaults: Mapping[str, Any], overrides: Mapping[str, Any], prefix: str = "") -> Dict[str, Any]:
    result: Dict[str, Any] = {}
    unknown = sorted(set(overrides) - set(defaults))
    if unknown:
        where = prefix or "root"
        raise ConfigError(f"unknown key(s) under {where}: {', '.join(unknown)}")
    for key, default_value in defaults.items():
        value = overrides.get(key, default_value)
        path = f"{prefix}.{key}" if prefix else key
        if isinstance(default_value, Mapping):
            if not isinstance(value, Mapping):
                raise ConfigError(f"{path} must be a mapping")
            result[key] = _merge_known(default_value, value, path)
        else:
            result[key] = value
    return result


def resolve_config(raw: Mapping[str, Any]) -> Dict[str, Any]:
    cfg = _merge_known(DEFAULT_CONFIG, raw)
    if cfg["version"] != 1:
        raise ConfigError("version must be 1")
    if cfg["build"]["type"] not in VALID_BUILD_TYPES:
        raise ConfigError(f"build.type must be one of: {', '.join(sorted(VALID_BUILD_TYPES))}")
    if cfg["build"]["shared"] not in VALID_SHARED:
        raise ConfigError("build.shared must be auto, true, or false")
    if not isinstance(cfg["build"]["dir"], str) or not cfg["build"]["dir"].strip():
        raise ConfigError("build.dir must be a non-empty string")
    parallel = cfg["build"]["parallel"]
    if not isinstance(parallel, int) or isinstance(parallel, bool) or parallel < 0:
        raise ConfigError("build.parallel must be a non-negative integer")

    for section, keys in {
        "bindings": ["python"],
        "features": ["hakoniwa_core", "zenoh", "mqtt"],
        "validation": ["tests", "examples", "tools", "benchmarks", "python_import"],
    }.items():
        for key in keys:
            if not isinstance(cfg[section][key], bool):
                raise ConfigError(f"{section}.{key} must be true or false")

    for key in ["hakoniwa_core_root", "vcpkg_root"]:
        if not isinstance(cfg["paths"][key], str):
            raise ConfigError(f"paths.{key} must be a string")

    python_enabled = cfg["bindings"]["python"]
    shared = cfg["build"]["shared"]
    if shared == "auto":
        cfg["build"]["shared_resolved"] = python_enabled
    else:
        cfg["build"]["shared_resolved"] = bool(shared)
    if python_enabled and not cfg["build"]["shared_resolved"]:
        raise ConfigError("bindings.python=true requires a shared native library; use build.shared=auto or true")
    if not python_enabled:
        cfg["validation"]["python_import"] = False
    return cfg


def _host_platform() -> tuple[str, str]:
    if sys.platform == "win32":
        os_name = "windows"
    elif sys.platform == "darwin":
        os_name = "macos"
    elif sys.platform.startswith("linux"):
        os_name = "linux"
    else:
        os_name = sys.platform
    machine = platform.machine().lower()
    arch = {
        "amd64": "x64",
        "x86_64": "x64",
        "arm64": "arm64",
        "aarch64": "arm64",
    }.get(machine, machine or "unknown")
    return os_name, arch


def _find_vcpkg_root(
    cfg: Mapping[str, Any],
    repo_root: Path,
    platform_name: str,
) -> Path | None:
    candidates = [cfg["paths"]["vcpkg_root"]]
    if platform_name == "windows":
        candidates.extend(
            [
                os.environ.get("VCPKG_ROOT", ""),
                os.environ.get("VCPKG_INSTALLATION_ROOT", ""),
                str(repo_root.parent / "vcpkg"),
            ]
        )
    for value in candidates:
        if not value:
            continue
        root = Path(value).expanduser().resolve()
        if (root / "scripts" / "buildsystems" / "vcpkg.cmake").exists():
            return root
    return None


def _find_core_root(cfg: Mapping[str, Any]) -> Path | None:
    candidates = [
        cfg["paths"]["hakoniwa_core_root"],
        os.environ.get("HAKONIWA_CORE_ROOT", ""),
        os.environ.get("HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT", ""),
    ]
    for value in candidates:
        if not value:
            continue
        root = Path(value).expanduser().resolve()
        if root.exists():
            return root
    return None


def _find_vswhere() -> Path | None:
    found = shutil.which("vswhere.exe") or shutil.which("vswhere")
    if found:
        return Path(found).resolve()
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    candidate = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    return candidate if candidate.exists() else None


def _prepend_path(env: Dict[str, str], paths: Iterable[Path]) -> None:
    existing = env.get("PATH", "")
    clean = [str(path) for path in paths if path and path.exists()]
    if clean:
        env["PATH"] = os.pathsep.join(clean + ([existing] if existing else []))


@dataclass
class BuildContext:
    repo_root: Path
    manifest_path: Path
    cfg: Dict[str, Any]
    platform_name: str
    arch: str
    build_dir: Path
    vcpkg_root: Path | None
    core_root: Path | None
    vcpkg_triplet: str
    child_env: Dict[str, str]

    @property
    def cmake_args(self) -> list[str]:
        cfg = self.cfg
        args = [
            f"-DCMAKE_BUILD_TYPE={cfg['build']['type']}",
            f"-DBUILD_SHARED_LIBS={'ON' if cfg['build']['shared_resolved'] else 'OFF'}",
            f"-DHAKO_PDU_ENDPOINT_BUILD_TESTS={'ON' if cfg['validation']['tests'] else 'OFF'}",
            f"-DHAKO_PDU_ENDPOINT_BUILD_EXAMPLES={'ON' if cfg['validation']['examples'] else 'OFF'}",
            f"-DHAKO_PDU_ENDPOINT_BUILD_TOOLS={'ON' if cfg['validation']['tools'] else 'OFF'}",
            f"-DHAKO_PDU_ENDPOINT_BUILD_BENCHMARKS={'ON' if cfg['validation']['benchmarks'] else 'OFF'}",
            f"-DHAKO_PDU_ENDPOINT_ENABLE_ZENOH={'ON' if cfg['features']['zenoh'] else 'OFF'}",
            f"-DHAKO_PDU_ENDPOINT_ENABLE_MQTT={'ON' if cfg['features']['mqtt'] else 'OFF'}",
            f"-DHAKO_PDU_ENDPOINT_ENABLE_HAKONIWA_CORE={'ON' if cfg['features']['hakoniwa_core'] else 'OFF'}",
        ]
        if self.core_root:
            args.append(f"-DHAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT={self.core_root}")
        if self.vcpkg_root:
            args.append(f"-DCMAKE_TOOLCHAIN_FILE={self.vcpkg_root / 'scripts' / 'buildsystems' / 'vcpkg.cmake'}")
            if self.platform_name == "windows":
                args.append(f"-DVCPKG_TARGET_TRIPLET={self.vcpkg_triplet}")
        return args


def create_context(manifest: Path, repo_root: Path) -> BuildContext:
    raw = load_simple_yaml(manifest)
    cfg = resolve_config(raw)
    platform_name, arch = _host_platform()
    build_dir = Path(cfg["build"]["dir"])
    if not build_dir.is_absolute():
        build_dir = (repo_root / build_dir).resolve()
    core_root = _find_core_root(cfg)
    if cfg["features"]["hakoniwa_core"] and not core_root:
        raise ConfigError(
            "features.hakoniwa_core=true requires paths.hakoniwa_core_root or "
            "HAKONIWA_CORE_ROOT/HAKO_PDU_ENDPOINT_HAKONIWA_CORE_ROOT"
        )
    vcpkg_root = _find_vcpkg_root(cfg, repo_root, platform_name)
    triplet = f"{arch}-windows" if platform_name == "windows" else ""
    env = dict(os.environ)
    if platform_name == "windows" and cfg["bindings"]["python"]:
        vswhere = _find_vswhere()
        if vswhere:
            _prepend_path(env, [vswhere.parent])
    return BuildContext(
        repo_root=repo_root,
        manifest_path=manifest,
        cfg=cfg,
        platform_name=platform_name,
        arch=arch,
        build_dir=build_dir,
        vcpkg_root=vcpkg_root,
        core_root=core_root,
        vcpkg_triplet=triplet,
        child_env=env,
    )


def _python_package_available(interpreter: Path, package: str) -> bool:
    result = subprocess.run(
        [
            str(interpreter),
            "-c",
            (
                "import importlib.util, sys; "
                f"sys.exit(0 if importlib.util.find_spec({package!r}) else 1)"
            ),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    return result.returncode == 0


def _python_build_requirements(repo_root: Path) -> list[str]:
    try:
        import tomllib
    except ModuleNotFoundError as exc:
        raise ConfigError(
            "managed prepare requires Python 3.11+ to read pyproject.toml"
        ) from exc
    path = repo_root / "pyproject.toml"
    if not path.is_file():
        raise ConfigError(f"Python build requirements are missing: {path}")
    with path.open("rb") as stream:
        data = tomllib.load(stream)
    build_system = data.get("build-system") if isinstance(data, dict) else None
    requirements = (
        build_system.get("requires") if isinstance(build_system, dict) else None
    )
    if not isinstance(requirements, list) or not requirements or not all(
        isinstance(item, str) and item.strip() for item in requirements
    ):
        raise ConfigError(
            f"pyproject.toml build-system.requires is missing or invalid: {path}"
        )
    return [item.strip() for item in requirements]


def _managed_workspace_python_venv(requested: Path) -> Path:
    if os.environ.get("HAKONIWA_WORKSPACE_ACTIVE") != "1":
        raise ConfigError(
            "prepare may install Python packages only inside an active Hakoniwa "
            "Business Pack workspace; run tools/workspace.py enter first"
        )
    workspace_root_value = os.environ.get("HAKONIWA_WORKSPACE_ROOT", "").strip()
    home_value = os.environ.get("HAKONIWA_HOME", "").strip()
    virtual_env_value = os.environ.get("VIRTUAL_ENV", "").strip()
    if not workspace_root_value or not home_value or not virtual_env_value:
        raise ConfigError(
            "active Hakoniwa workspace is incomplete; HAKONIWA_WORKSPACE_ROOT, "
            "HAKONIWA_HOME, and VIRTUAL_ENV are required"
        )
    workspace_root = Path(workspace_root_value).expanduser().resolve()
    home = Path(home_value).expanduser().resolve()
    virtual_env = Path(virtual_env_value).expanduser().resolve()
    expected_home = workspace_root / "work" / "foundation" / "install"
    expected_venv = expected_home / "python"
    requested = requested.expanduser().resolve()
    if home != expected_home or virtual_env != expected_venv or requested != expected_venv:
        raise ConfigError(
            "prepare refuses to modify a Python environment outside the active "
            f"Hakoniwa Foundation: requested={requested}, expected={expected_venv}"
        )
    return expected_venv


def prepare(ctx: BuildContext, python_venv: Path | None) -> None:
    if not ctx.cfg["bindings"]["python"]:
        print("Python binding disabled; no Python build prerequisites to prepare.")
        return
    if python_venv is None:
        raise ConfigError(
            "bindings.python=true requires --python-venv during prepare"
        )
    managed_venv = _managed_workspace_python_venv(python_venv)
    interpreter = _venv_python(managed_venv, ctx.platform_name)
    if not interpreter.is_file():
        raise ConfigError(f"Foundation Python venv was not found: {managed_venv}")
    requirements = _python_build_requirements(ctx.repo_root)
    _run(
        [str(interpreter), "-m", "pip", "install", *requirements],
        cwd=ctx.repo_root,
    )


def _first_command(names: tuple[str, ...]) -> str | None:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


@dataclass(frozen=True)
class BoostProbeResult:
    available: bool
    detail: str


def _cmake_boost_headers_probe(
    ctx: BuildContext,
    cmake: str,
    compiler: str | None,
) -> BoostProbeResult:
    """Probe the same Boost header contract without configuring the real build."""
    with tempfile.TemporaryDirectory(prefix="hako-boost-doctor-") as temp_dir:
        root = Path(temp_dir)
        source = root / "source"
        build_dir = root / "build"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            "\n".join(
                [
                    "cmake_minimum_required(VERSION 3.16)",
                    "project(hako_boost_doctor LANGUAGES CXX)",
                    "set(CMAKE_CXX_STANDARD 20)",
                    "set(CMAKE_CXX_STANDARD_REQUIRED ON)",
                    "find_package(Boost 1.70 CONFIG QUIET)",
                    "if(TARGET Boost::headers)",
                    "  set(HAKO_BOOST_TARGET Boost::headers)",
                    "elseif(TARGET Boost::boost)",
                    "  set(HAKO_BOOST_TARGET Boost::boost)",
                    "else()",
                    "  find_path(HAKO_BOOST_INCLUDE_DIR boost/asio.hpp)",
                    "  if(NOT HAKO_BOOST_INCLUDE_DIR)",
                    '    message(FATAL_ERROR "HAKO_BOOST_HEADERS_NOT_FOUND")',
                    "  endif()",
                    "  add_library(hako_boost_headers INTERFACE)",
                    "  target_include_directories(hako_boost_headers INTERFACE ${HAKO_BOOST_INCLUDE_DIR})",
                    "  set(HAKO_BOOST_TARGET hako_boost_headers)",
                    "endif()",
                    "include(CheckCXXSourceCompiles)",
                    "set(CMAKE_REQUIRED_LIBRARIES ${HAKO_BOOST_TARGET})",
                    'check_cxx_source_compiles("#include <boost/asio.hpp>\\n#include <boost/beast.hpp>\\nint main() { return 0; }" HAKO_BOOST_HEADERS_COMPILE)',
                    "if(NOT HAKO_BOOST_HEADERS_COMPILE)",
                    '    message(FATAL_ERROR "HAKO_BOOST_HEADERS_COMPILE_FAILED")',
                    "endif()",
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        command = [cmake, "-S", str(source), "-B", str(build_dir)]
        if ctx.vcpkg_root:
            command.append(
                "-DCMAKE_TOOLCHAIN_FILE="
                + str(ctx.vcpkg_root / "scripts" / "buildsystems" / "vcpkg.cmake")
            )
            if ctx.platform_name == "windows":
                command.append(f"-DVCPKG_TARGET_TRIPLET={ctx.vcpkg_triplet}")
        env = dict(ctx.child_env)
        if compiler:
            env["CXX"] = compiler
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )
        if result.returncode == 0:
            return BoostProbeResult(True, "Boost headers compile with C++20")
        output = f"{result.stdout}\n{result.stderr}"
        if "HAKO_BOOST_HEADERS_NOT_FOUND" in output:
            detail = "Boost headers were not discovered by CMake"
        elif "HAKO_BOOST_HEADERS_COMPILE_FAILED" in output:
            detail = "Boost headers were discovered but did not compile with C++20"
        else:
            detail = "the CMake Boost probe could not configure or compile"
        return BoostProbeResult(False, detail)


def _windows_vcpkg_boost_hint(ctx: BuildContext) -> str:
    if not ctx.vcpkg_root:
        return ""
    include_root = ctx.vcpkg_root / "installed" / ctx.vcpkg_triplet / "include"
    required = {
        "Boost.Asio": (include_root / "boost" / "asio.hpp", "boost-asio"),
        "Boost.Beast": (include_root / "boost" / "beast.hpp", "boost-beast"),
    }
    missing = [name for name, (header, _package) in required.items() if not header.exists()]
    if not missing:
        return ""
    packages = [f"{required[name][1]}:{ctx.vcpkg_triplet}" for name in missing]
    return (
        "; selected vcpkg is missing "
        + ", ".join(missing)
        + "; install with: vcpkg install "
        + " ".join(packages)
    )


def doctor(
    ctx: BuildContext,
    python_interpreter: Path | None = None,
) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    cmake = shutil.which("cmake")
    if not cmake:
        errors.append("CMake was not found on PATH")
    compiler: str | None = None
    if ctx.platform_name != "windows":
        compiler = _first_command(("c++", "g++", "clang++"))
        if not compiler:
            errors.append(
                f"[MISSING] hakoniwa-pdu-endpoint build prerequisite: C++ compiler; platform: {ctx.platform_name} {ctx.arch}"
            )
    if ctx.cfg["features"]["zenoh"]:
        missing_rust_tools = [tool for tool in ("cargo", "rustc") if not shutil.which(tool)]
        if missing_rust_tools:
            errors.append(
                "Zenoh support requires a Rust toolchain on PATH; missing: "
                + ", ".join(missing_rust_tools)
                + " (install Rust with rustup and ensure $HOME/.cargo/bin is on PATH)"
            )
    if ctx.cfg["bindings"]["python"]:
        interpreter = python_interpreter or Path(sys.executable)
        if not interpreter.is_file():
            errors.append(f"Python interpreter was not found: {interpreter}")
        elif not _python_package_available(interpreter, "cffi"):
            errors.append("Python package 'cffi' is missing (install with: python -m pip install cffi)")
        if interpreter.is_file() and not _python_package_available(interpreter, "setuptools"):
            errors.append("Python package 'setuptools' is missing (install with: python -m pip install setuptools)")
        if ctx.platform_name == "windows" and not (_find_vswhere() or shutil.which("cl.exe")):
            errors.append("Visual Studio C++ tools were not found (vswhere.exe/cl.exe unavailable)")
    if cmake and (ctx.platform_name == "windows" or compiler):
        boost = _cmake_boost_headers_probe(ctx, cmake, compiler)
        if not boost.available:
            errors.append(
                "[MISSING] hakoniwa-pdu-endpoint build prerequisite: Boost headers; "
                "required: boost/asio.hpp, boost/beast.hpp; "
                f"platform: {ctx.platform_name} {ctx.arch}; "
                f"reason: {boost.detail}; "
                "install Boost development headers for this environment or make an existing Boost installation discoverable by CMake"
                + (
                    _windows_vcpkg_boost_hint(ctx)
                    if ctx.platform_name == "windows"
                    else ""
                )
            )
    return errors, warnings


def _yaml_scalar(value: Any) -> str:
    if value is True:
        return "true"
    if value is False:
        return "false"
    if value is None:
        return "null"
    if isinstance(value, int):
        return str(value)
    return json.dumps(str(value), ensure_ascii=False)


def dump_yaml(data: Mapping[str, Any], indent: int = 0) -> str:
    lines: list[str] = []
    prefix = " " * indent
    for key, value in data.items():
        if isinstance(value, Mapping):
            lines.append(f"{prefix}{key}:")
            lines.append(dump_yaml(value, indent + 2).rstrip())
        elif isinstance(value, list):
            lines.append(f"{prefix}{key}:")
            for item in value:
                lines.append(f"{prefix}  - {_yaml_scalar(item)}")
        else:
            lines.append(f"{prefix}{key}: {_yaml_scalar(value)}")
    return "\n".join(lines) + "\n"


def _resolved_record(ctx: BuildContext) -> Dict[str, Any]:
    return {
        "version": 1,
        "manifest": str(ctx.manifest_path),
        "platform": {"os": ctx.platform_name, "arch": ctx.arch},
        "build": {
            "type": ctx.cfg["build"]["type"],
            "dir": str(ctx.build_dir),
            "shared": ctx.cfg["build"]["shared_resolved"],
        },
        "bindings": {"python": ctx.cfg["bindings"]["python"]},
        "features": dict(ctx.cfg["features"]),
        "validation": dict(ctx.cfg["validation"]),
        "resolved_paths": {
            "hakoniwa_core_root": str(ctx.core_root) if ctx.core_root else "",
            "vcpkg_root": str(ctx.vcpkg_root) if ctx.vcpkg_root else "",
        },
        "cmake_args": ctx.cmake_args,
    }


def write_resolved(ctx: BuildContext) -> Path:
    out_dir = ctx.repo_root / ".hako"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "resolved-build.yaml"
    out_path.write_text(dump_yaml(_resolved_record(ctx)), encoding="utf-8")
    (out_dir / "cmake-args.txt").write_text("\n".join(ctx.cmake_args) + "\n", encoding="utf-8")
    return out_path


def print_summary(ctx: BuildContext, errors: list[str], warnings: list[str]) -> None:
    cfg = ctx.cfg
    print("Hakoniwa PDU Endpoint build configuration")
    print(f"  Platform       : {ctx.platform_name}-{ctx.arch}")
    print(f"  Build type     : {cfg['build']['type']}")
    print(f"  Build directory: {ctx.build_dir}")
    print(f"  Shared library : {'ON' if cfg['build']['shared_resolved'] else 'OFF'}")
    print(f"  Python binding : {'ON' if cfg['bindings']['python'] else 'OFF'}")
    print(f"  Hakoniwa Core  : {'ON' if cfg['features']['hakoniwa_core'] else 'OFF'}")
    print(f"  Zenoh          : {'ON' if cfg['features']['zenoh'] else 'OFF'}")
    print(f"  MQTT           : {'ON' if cfg['features']['mqtt'] else 'OFF'}")
    print(f"  Tests          : {'ON' if cfg['validation']['tests'] else 'OFF'}")
    print(f"  vcpkg          : {ctx.vcpkg_root or 'not resolved'}")
    if cfg["features"]["hakoniwa_core"]:
        print(f"  Core root      : {ctx.core_root or 'not resolved'}")
    if errors:
        print("\nDoctor errors:")
        for item in errors:
            print(f"  - {item}")
    if warnings:
        print("\nDoctor warnings:")
        for item in warnings:
            print(f"  - {item}")


def _run(command: list[str], *, cwd: Path, env: Mapping[str, str] | None = None) -> None:
    print(">", " ".join(command))
    subprocess.run(command, cwd=cwd, env=dict(env) if env else None, check=True)


def configure(ctx: BuildContext) -> None:
    ctx.build_dir.mkdir(parents=True, exist_ok=True)
    _run(["cmake", "-S", str(ctx.repo_root), "-B", str(ctx.build_dir), *ctx.cmake_args], cwd=ctx.repo_root, env=ctx.child_env)


def _find_native_shared_lib(ctx: BuildContext) -> Path:
    names = {
        "windows": "hakoniwa_pdu_endpoint.dll",
        "macos": "libhakoniwa_pdu_endpoint.dylib",
        "linux": "libhakoniwa_pdu_endpoint.so",
    }
    name = names.get(ctx.platform_name)
    if not name:
        raise ConfigError(f"Python binding shared-library discovery is unsupported on {ctx.platform_name}")
    src_dir = ctx.build_dir / "src"
    candidates = [src_dir / ctx.cfg["build"]["type"] / name, src_dir / name]
    candidates.extend(sorted(src_dir.rglob(name)) if src_dir.exists() else [])
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    raise ConfigError(f"native shared library not found under {src_dir}: {name}")


def _runtime_dirs(ctx: BuildContext, native_lib: Path) -> list[Path]:
    dirs = [native_lib.parent]
    if ctx.core_root:
        for child in ("bin", "lib"):
            candidate = ctx.core_root / child
            if candidate.exists():
                dirs.append(candidate)
    if ctx.vcpkg_root and ctx.platform_name == "windows":
        candidate = ctx.vcpkg_root / "installed" / ctx.vcpkg_triplet / "bin"
        if candidate.exists():
            dirs.append(candidate)
    seen: set[Path] = set()
    result: list[Path] = []
    for item in dirs:
        resolved = item.resolve()
        if resolved not in seen:
            seen.add(resolved)
            result.append(resolved)
    return result


def _python_env(ctx: BuildContext, native_lib: Path) -> Dict[str, str]:
    env = dict(ctx.child_env)
    python_build = ctx.build_dir / "python"
    env["HAKO_PDU_ENDPOINT_SHARED_LIB"] = str(native_lib)
    env["HAKO_PDU_ENDPOINT_LIB_DIR"] = str(native_lib.parent)
    env["HAKO_PDU_ENDPOINT_PYTHON_BUILD_DIR"] = str(python_build)
    runtime_dirs = _runtime_dirs(ctx, native_lib)
    env["HAKO_PDU_ENDPOINT_RUNTIME_DIRS"] = os.pathsep.join(str(path) for path in runtime_dirs)
    _prepend_path(env, runtime_dirs)
    py_paths = [str(ctx.repo_root / "python"), str(python_build)]
    old_py_path = env.get("PYTHONPATH", "")
    if old_py_path:
        py_paths.append(old_py_path)
    env["PYTHONPATH"] = os.pathsep.join(py_paths)
    return env


def build(ctx: BuildContext, python_interpreter: Path | None = None) -> None:
    configure(ctx)
    command = ["cmake", "--build", str(ctx.build_dir), "--config", ctx.cfg["build"]["type"]]
    parallel = ctx.cfg["build"]["parallel"]
    if parallel:
        command += ["--parallel", str(parallel)]
    _run(command, cwd=ctx.repo_root, env=ctx.child_env)
    if ctx.cfg["bindings"]["python"]:
        native_lib = _find_native_shared_lib(ctx)
        env = _python_env(ctx, native_lib)
        interpreter = python_interpreter or Path(sys.executable)
        _run([str(interpreter), str(ctx.repo_root / "python" / "hakoniwa_pdu_endpoint" / "build_c_endpoint_ffi.py")], cwd=ctx.repo_root, env=env)


def test(ctx: BuildContext, python_interpreter: Path | None = None) -> None:
    if ctx.cfg["validation"]["tests"]:
        _run(
            ["ctest", "--test-dir", str(ctx.build_dir), "-C", ctx.cfg["build"]["type"], "--output-on-failure"],
            cwd=ctx.repo_root,
            env=ctx.child_env,
        )
    if ctx.cfg["validation"]["python_import"]:
        native_lib = _find_native_shared_lib(ctx)
        env = _python_env(ctx, native_lib)
        interpreter = python_interpreter or Path(sys.executable)
        _run(
            [str(interpreter), "-c", "from hakoniwa_pdu_endpoint.c_endpoint import Endpoint; print('HAKO_PYTHON_IMPORT_OK', Endpoint)"],
            cwd=ctx.repo_root,
            env=env,
        )


def _command_output(command: list[str], cwd: Path) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=False,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def _cmake_cache_value(build_dir: Path, key: str) -> str:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return "unknown"
    prefix = f"{key}:"
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix) and "=" in line:
            return line.split("=", 1)[1] or "unknown"
    return "unknown"


def _read_dependency_receipt(prefix: Path, component_id: str) -> Dict[str, Any]:
    path = prefix / "share" / "hakoniwa" / "receipts" / f"{component_id}.yaml"
    if not path.is_file():
        raise ConfigError(f"dependency receipt not found: {path}")

    result: Dict[str, Any] = {"build_limits": {}}
    section = ""
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw and not raw.startswith(" ") and raw.endswith(":"):
            section = raw[:-1]
            continue
        if not raw.startswith("  ") or raw.startswith("    ") or ":" not in raw:
            continue
        key, value = raw.strip().split(":", 1)
        parsed = _parse_scalar(value)
        if section == "component" and key in {"version", "source_revision"}:
            result[key] = parsed
        elif section == "build_limits":
            result["build_limits"][key] = parsed

    if not result.get("version") or not result.get("source_revision"):
        raise ConfigError(f"incomplete dependency receipt: {path}")
    return result


def _endpoint_artifacts(install_dir: Path) -> list[tuple[Path, str]]:
    artifacts: list[tuple[Path, str]] = []
    fixed = (
        (Path("include/hakoniwa/pdu/endpoint.hpp"), "header"),
        (Path("include/hakoniwa/time_source"), "directory"),
        (Path("lib/cmake/hakoniwa_pdu_endpoint"), "cmake-package"),
    )
    for relative, kind in fixed:
        installed = install_dir / relative
        if installed.exists():
            artifacts.append((relative, kind))

    for child in ("bin", "lib"):
        parent = install_dir / child
        if not parent.is_dir():
            continue
        for installed in parent.iterdir():
            if installed.is_file() and "hakoniwa_pdu_endpoint" in installed.name:
                kind = "executable" if child == "bin" and installed.suffix == ".exe" else "library"
                artifacts.append((installed.relative_to(install_dir), kind))
    venv_dir = install_dir / "python"
    if venv_dir.is_dir():
        for package_dir in venv_dir.rglob("hakoniwa_pdu_endpoint"):
            if package_dir.is_dir() and (package_dir / "c_endpoint.py").is_file():
                artifacts.append(
                    (package_dir.relative_to(install_dir), "python-package")
                )
                break
    return sorted(set(artifacts), key=lambda item: item[0].as_posix())


def _validate_core_variant_artifacts(
    ctx: BuildContext,
    install_dir: Path,
) -> None:
    if not (
        ctx.platform_name == "windows"
        and ctx.cfg["features"]["hakoniwa_core"]
        and ctx.cfg["build"]["shared_resolved"]
    ):
        return

    required: list[Path] = []
    for variant in ("core_callback", "core_polling"):
        name = f"hakoniwa_pdu_endpoint_{variant}"
        required.extend(
            [
                Path("bin") / f"{name}.dll",
                Path("lib") / f"{name}.lib",
            ]
        )
    missing = [path for path in required if not (install_dir / path).is_file()]
    if missing:
        raise ConfigError(
            "installed Windows Core endpoint variant is incomplete; missing: "
            + ", ".join(path.as_posix() for path in missing)
        )


def _remove_stale_profile_artifacts(
    ctx: BuildContext,
    install_dir: Path,
) -> None:
    if not ctx.cfg["features"]["hakoniwa_core"]:
        for child in ("bin", "lib"):
            directory = install_dir / child
            if not directory.is_dir():
                continue
            for pattern in (
                "*hakoniwa_pdu_endpoint_core_callback*",
                "*hakoniwa_pdu_endpoint_core_polling*",
            ):
                for artifact in directory.glob(pattern):
                    if artifact.is_file():
                        artifact.unlink()

    if not ctx.cfg["bindings"]["python"]:
        venv_dir = install_dir / "python"
        if venv_dir.is_dir():
            for package_dir in venv_dir.rglob("hakoniwa_pdu_endpoint"):
                if package_dir.is_dir() and (package_dir / "c_endpoint.py").is_file():
                    shutil.rmtree(package_dir)


def write_receipt(ctx: BuildContext, install_dir: Path) -> Path:
    receipt_root = install_dir / "share" / "hakoniwa" / "receipts"
    resolved_relative = (
        Path("share")
        / "hakoniwa"
        / "receipts"
        / "resolved"
        / "hakoniwa-pdu-endpoint.yaml"
    )
    (install_dir / resolved_relative).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(
        ctx.repo_root / ".hako" / "resolved-build.yaml",
        install_dir / resolved_relative,
    )

    _validate_core_variant_artifacts(ctx, install_dir)
    artifacts = _endpoint_artifacts(install_dir)
    if not any(kind == "cmake-package" for _, kind in artifacts):
        raise ConfigError(f"installed Endpoint CMake package not found under: {install_dir}")

    dependencies: Dict[str, Dict[str, Any]] = {}
    if ctx.cfg["features"]["hakoniwa_core"]:
        if ctx.core_root is None:
            raise ConfigError("resolved Hakoniwa Core root is missing")
        dependencies["hakoniwa-core-pro"] = _read_dependency_receipt(
            ctx.core_root,
            "hakoniwa-core-pro",
        )

    compiler = _cmake_cache_value(ctx.build_dir, "CMAKE_CXX_COMPILER")
    revision = _command_output(["git", "rev-parse", "HEAD"], ctx.repo_root)
    capabilities = {
        "cmake_package": True,
        "python_binding": ctx.cfg["bindings"]["python"],
        "hakoniwa_core": ctx.cfg["features"]["hakoniwa_core"],
        "core_callback": ctx.cfg["features"]["hakoniwa_core"],
        "core_polling": ctx.cfg["features"]["hakoniwa_core"],
        "tcp": True,
        "udp": True,
        "websocket": True,
        "storage": True,
        "zenoh": ctx.cfg["features"]["zenoh"],
        "mqtt": ctx.cfg["features"]["mqtt"],
    }
    lines = [
        "schema_version: 1",
        "component:",
        "  id: hakoniwa-pdu-endpoint",
        "  version: 1.0.0",
        f"  source_revision: {_yaml_scalar(revision)}",
        "platform:",
        f"  os: {_yaml_scalar(ctx.platform_name)}",
        f"  architecture: {_yaml_scalar(ctx.arch)}",
        f"  toolchain: {_yaml_scalar(compiler)}",
        "install:",
        f"  prefix: {_yaml_scalar(install_dir)}",
        "capabilities:",
    ]
    for key, value in capabilities.items():
        lines.append(f"  {key}: {_yaml_scalar(value)}")
    core_limits = (
        dependencies["hakoniwa-core-pro"]["build_limits"]
        if "hakoniwa-core-pro" in dependencies
        else {}
    )
    if core_limits:
        lines.append("build_limits:")
        for key, value in core_limits.items():
            lines.append(f"  {key}: {_yaml_scalar(value)}")
    else:
        lines.append("build_limits: {}")
    if dependencies:
        lines.append("dependencies:")
        for component_id, dependency in dependencies.items():
            lines.extend(
                [
                    f"  {component_id}:",
                    f"    version: {_yaml_scalar(dependency['version'])}",
                    f"    source_revision: {_yaml_scalar(dependency['source_revision'])}",
                    "    build_limits:",
                ]
            )
            for key, value in dependency["build_limits"].items():
                lines.append(f"      {key}: {_yaml_scalar(value)}")
    else:
        lines.append("dependencies: {}")
    lines.append("artifacts:")
    for path, kind in artifacts:
        lines.extend(
            [
                f"  - path: {_yaml_scalar(path.as_posix())}",
                f"    kind: {kind}",
            ]
        )
    lines.append(f"resolved_manifest: {_yaml_scalar(resolved_relative.as_posix())}")
    receipt_path = receipt_root / "hakoniwa-pdu-endpoint.yaml"
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return receipt_path


def _venv_python(venv_dir: Path, platform_name: str) -> Path:
    if platform_name == "windows":
        return venv_dir / "Scripts" / "python.exe"
    return venv_dir / "bin" / "python"


def _install_python_binding(
    ctx: BuildContext,
    install_dir: Path,
    python_venv: Path,
) -> None:
    interpreter = _venv_python(python_venv, ctx.platform_name)
    if not interpreter.is_file():
        raise ConfigError(f"Foundation Python venv was not found: {python_venv}")
    site_result = subprocess.run(
        [
            str(interpreter),
            "-c",
            "import site; print(site.getsitepackages()[0])",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    site_packages = Path(site_result.stdout.strip()).resolve()
    try:
        site_packages.relative_to(install_dir)
    except ValueError as exc:
        raise ConfigError(
            f"Python venv is outside the selected install prefix: {python_venv}"
        ) from exc

    destination = site_packages / "hakoniwa_pdu_endpoint"
    shutil.copytree(
        ctx.repo_root / "python" / "hakoniwa_pdu_endpoint",
        destination,
        dirs_exist_ok=True,
    )
    extension_root = ctx.build_dir / "python" / "hakoniwa_pdu_endpoint"
    extensions = [
        path
        for path in extension_root.glob("_c_endpoint_ffi*")
        if path.is_file() and path.suffix.lower() in {".so", ".pyd", ".dll"}
    ]
    if not extensions:
        raise ConfigError(
            f"built Endpoint CFFI extension not found under: {extension_root}"
        )
    for extension in extensions:
        shutil.copy2(extension, destination / extension.name)


def install(
    ctx: BuildContext,
    install_dir: Path,
    python_venv: Path | None,
) -> None:
    if not (ctx.build_dir / "CMakeCache.txt").is_file():
        raise ConfigError(
            f"configured build tree not found: {ctx.build_dir}; run hako.py build first"
        )
    _remove_stale_profile_artifacts(ctx, install_dir)
    command = ["cmake", "--install", str(ctx.build_dir), "--prefix", str(install_dir)]
    if ctx.platform_name == "windows":
        command.extend(["--config", ctx.cfg["build"]["type"]])
    _run(command, cwd=ctx.repo_root, env=ctx.child_env)
    if ctx.cfg["bindings"]["python"]:
        if python_venv is None:
            raise ConfigError(
                "bindings.python=true requires --python-venv during install"
            )
        _install_python_binding(ctx, install_dir, python_venv)
    receipt = write_receipt(ctx, install_dir)
    print(f"Component Receipt: {receipt}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="OS-independent Hakoniwa PDU Endpoint build configurator")
    parser.add_argument("command", choices=["prepare", "doctor", "configure", "build", "test", "install"])
    parser.add_argument("--config", default=None, help="build manifest (default: repository root/hakoniwa-build.yaml)")
    parser.add_argument("--install-dir", default=None, help="explicit local install prefix (required by install)")
    parser.add_argument("--python-venv", default=None, help="Foundation Python venv used to install the CFFI binding")
    parser.add_argument("--dry-run", action="store_true", help="resolve and print without running build commands")
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parents[1]
    manifest = repo_root / "hakoniwa-build.yaml" if args.config is None else Path(args.config)
    if args.config is not None and not manifest.is_absolute():
        manifest = (Path.cwd() / manifest).resolve()
    if not manifest.exists():
        raise ConfigError(f"build manifest not found: {manifest}")

    ctx = create_context(manifest, repo_root)
    python_venv = Path(args.python_venv).resolve() if args.python_venv else None
    python_interpreter = (
        _venv_python(python_venv, ctx.platform_name)
        if python_venv is not None
        else Path(sys.executable)
    )
    if args.command == "prepare" and not args.dry_run:
        prepare(ctx, python_venv)

    errors, warnings = doctor(ctx, python_interpreter)
    print_summary(ctx, errors, warnings)
    resolved = write_resolved(ctx)
    print(f"\nResolved configuration: {resolved}")

    if args.command in {"prepare", "doctor"}:
        return 1 if errors else 0
    if args.command in {"build", "test", "install"} and errors:
        raise ConfigError("doctor found blocking prerequisites; fix them before building/testing")
    if args.command == "configure" and not shutil.which("cmake"):
        raise ConfigError("CMake was not found on PATH")
    if args.dry_run:
        return 0
    if args.command == "configure":
        configure(ctx)
    elif args.command == "build":
        build(ctx, python_interpreter)
    elif args.command == "test":
        test(ctx, python_interpreter)
    elif args.command == "install":
        if not args.install_dir:
            raise ConfigError("install requires --install-dir")
        install_dir = Path(args.install_dir)
        if not install_dir.is_absolute():
            install_dir = (Path.cwd() / install_dir).resolve()
        install(ctx, install_dir, python_venv)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ConfigError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
    except subprocess.CalledProcessError as exc:
        print(f"ERROR: command failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode or 1)
