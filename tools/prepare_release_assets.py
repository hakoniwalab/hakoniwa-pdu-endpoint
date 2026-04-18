#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import shutil
import tomllib
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


PROJECT_NAME = "hakoniwa-pdu-endpoint"
MODULE_BASENAME = "hakoniwa_pdu_endpoint"
def normalize_arch(platform: str, arch: str) -> str:
    value = arch.strip().lower()
    aliases = {
        "linux": {
            "x64": "x86_64",
            "amd64": "x86_64",
            "x86_64": "x86_64",
            "aarch64": "arm64",
            "arm64": "arm64",
        },
        "macos": {
            "x64": "x86_64",
            "amd64": "x86_64",
            "x86_64": "x86_64",
            "aarch64": "arm64",
            "arm64": "arm64",
            "universal2": "universal2",
        },
        "windows": {
            "x64": "x64",
            "amd64": "x64",
            "x86_64": "x64",
        },
    }
    normalized = aliases[platform].get(value)
    if normalized is None:
        valid = ", ".join(sorted(aliases[platform].keys()))
        raise SystemExit(f"Unsupported arch '{arch}' for {platform}. Expected one of: {valid}")
    return normalized


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare zipped release bundles with native libs and cffi extension."
    )
    parser.add_argument("--platform", required=True, choices=("linux", "macos", "windows"))
    parser.add_argument("--arch", required=True)
    parser.add_argument("--python-tag", default="cp312", help="Python ABI tag used in the bundle name.")
    parser.add_argument("--build-dir", default=None, help="Native build directory.")
    parser.add_argument(
        "--python-build-dir",
        default="build/python",
        help="Directory that contains or will contain the built cffi extension module.",
    )
    parser.add_argument("--output-dir", default="release-assets", help="Output directory for bundles.")
    parser.add_argument(
        "--skip-cffi-build",
        action="store_true",
        help="Do not build the cffi extension before packaging.",
    )
    return parser.parse_args()


def native_artifacts(platform: str, build_dir: Path, arch: str) -> list[tuple[Path, str]]:
    if platform == "linux":
        return [
            (
                build_dir / "src" / f"lib{MODULE_BASENAME}.so",
                f"{MODULE_BASENAME}-linux-{arch}.so",
            )
        ]
    if platform == "macos":
        return [
            (
                build_dir / "src" / f"lib{MODULE_BASENAME}.dylib",
                f"{MODULE_BASENAME}-macos-{arch}.dylib",
            )
        ]
    return [
        (
            build_dir / "src" / "Release" / f"{MODULE_BASENAME}.dll",
            f"{MODULE_BASENAME}-windows-{arch}.dll",
        ),
        (
            build_dir / "src" / "Release" / f"{MODULE_BASENAME}.lib",
            f"{MODULE_BASENAME}-windows-{arch}.lib",
        ),
    ]


def find_cffi_extension(platform: str, python_build_dir: Path) -> Path:
    package_dir = python_build_dir / "hakoniwa_pdu_endpoint"
    candidates = sorted(package_dir.glob("_c_endpoint_ffi*"))
    expected_suffixes = {
        "linux": {".so"},
        "macos": {".so"},
        "windows": {".pyd"},
    }
    candidates = [path for path in candidates if path.suffix in expected_suffixes[platform]]
    if not candidates:
        raise SystemExit(f"Missing {platform} cffi extension under: {package_dir}")
    return candidates[0]


def bundle_name(platform: str, arch: str, python_tag: str) -> str:
    return f"{PROJECT_NAME}-{platform}-{arch}-{python_tag}"


def native_lib_info(platform: str, build_dir: Path) -> tuple[Path, Path]:
    if platform == "linux":
        lib_dir = build_dir / "src"
        shared_lib = lib_dir / f"lib{MODULE_BASENAME}.so"
        return lib_dir, shared_lib
    if platform == "macos":
        lib_dir = build_dir / "src"
        shared_lib = lib_dir / f"lib{MODULE_BASENAME}.dylib"
        return lib_dir, shared_lib
    lib_dir = build_dir / "src" / "Release"
    shared_lib = lib_dir / f"{MODULE_BASENAME}.dll"
    return lib_dir, shared_lib


def build_cffi_extension(repo_root: Path, platform: str, build_dir: Path) -> None:
    lib_dir, shared_lib = native_lib_info(platform, build_dir)
    if not shared_lib.exists():
        raise SystemExit(f"Missing shared library for cffi build: {shared_lib}")

    env = os.environ.copy()
    env["HAKO_PDU_ENDPOINT_LIB_DIR"] = str(lib_dir)
    env["HAKO_PDU_ENDPOINT_SHARED_LIB"] = str(shared_lib)

    cmd = [
        os.environ.get("PYTHON", "python3"),
        str(repo_root / "python" / "hakoniwa_pdu_endpoint" / "build_c_endpoint_ffi.py"),
    ]
    try:
        subprocess.run(cmd, cwd=repo_root, env=env, check=True)
    except FileNotFoundError as exc:
        raise SystemExit(
            "Python interpreter not found. Set the PYTHON environment variable if needed."
        ) from exc
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"cffi build failed with exit code {exc.returncode}") from exc


def package_version(repo_root: Path) -> str:
    pyproject_path = repo_root / "pyproject.toml"
    with pyproject_path.open("rb") as f:
        data = tomllib.load(f)
    return str(data["project"]["version"])


def write_bundle_readme(
    bundle_dir: Path,
    platform: str,
    arch: str,
    python_tag: str,
    shared_lib_name: str,
    version: str,
) -> None:
    readme = bundle_dir / "README.txt"
    readme.write_text(
        "\n".join(
            [
                f"{PROJECT_NAME} release bundle",
                f"package version: {version}",
                f"platform: {platform}",
                f"arch: {arch}",
                f"python abi: {python_tag}",
                "",
                "Bundle contents:",
                "  - native shared library",
                "  - cffi extension module",
                "  - hakoniwa_pdu_endpoint pure-Python runtime files",
                "",
                "Typical installer behavior:",
                "  - install the Python package separately",
                "  - overlay hakoniwa_pdu_endpoint/* from this bundle into the installed package dir",
                "  - place the shared library and _c_endpoint_ffi beside c_endpoint.py",
                "",
                "If you use this bundle manually, set these environment variables:",
                f"  HAKO_PDU_ENDPOINT_LIB_DIR=<installed-package-dir>",
                f"  HAKO_PDU_ENDPOINT_SHARED_LIB=<installed-package-dir>/{shared_lib_name}",
            ]
        ),
        encoding="utf-8",
    )


def zip_directory(src_dir: Path, zip_path: Path) -> None:
    with ZipFile(zip_path, "w", compression=ZIP_DEFLATED) as zf:
        for path in sorted(src_dir.rglob("*")):
            if path.is_dir():
                continue
            zf.write(path, path.relative_to(src_dir.parent))


def main() -> int:
    args = parse_args()
    platform = args.platform
    arch = normalize_arch(platform, args.arch)
    python_tag = args.python_tag
    repo_root = Path(__file__).resolve().parents[1]
    version = package_version(repo_root)

    default_build_dirs = {
        "linux": "build-shared",
        "macos": "build-shared",
        "windows": "build-win",
    }
    build_dir = Path(args.build_dir or default_build_dirs[platform]).resolve()
    python_build_dir = Path(args.python_build_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    if not args.skip_cffi_build:
        build_cffi_extension(repo_root, platform, build_dir)

    bundle_dir = output_dir / bundle_name(platform, arch, python_tag)
    if bundle_dir.exists():
        shutil.rmtree(bundle_dir)
    bundle_dir.mkdir(parents=True)

    copied: list[Path] = []
    shared_lib_name = ""
    for source_path, target_name in native_artifacts(platform, build_dir, arch):
        if not source_path.exists():
            raise SystemExit(f"Missing native artifact: {source_path}")
        target_path = bundle_dir / target_name
        shutil.copy2(source_path, target_path)
        copied.append(target_path)
        if source_path.suffix in {".so", ".dylib", ".dll"}:
            shared_lib_name = target_name

    cffi_ext = find_cffi_extension(platform, python_build_dir)
    cffi_target = bundle_dir / cffi_ext.name
    shutil.copy2(cffi_ext, cffi_target)
    copied.append(cffi_target)

    package_src = repo_root / "python" / "hakoniwa_pdu_endpoint"
    package_dst = bundle_dir / "hakoniwa_pdu_endpoint"
    shutil.copytree(package_src, package_dst)
    copied.extend(sorted(path for path in package_dst.rglob("*") if path.is_file()))

    write_bundle_readme(bundle_dir, platform, arch, python_tag, shared_lib_name, version)
    copied.append(bundle_dir / "README.txt")

    zip_path = output_dir / f"{bundle_name(platform, arch, python_tag)}.zip"
    if zip_path.exists():
        zip_path.unlink()
    zip_directory(bundle_dir, zip_path)

    print("Prepared release bundle:")
    print(zip_path)
    print("Bundle contents:")
    for path in copied:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
