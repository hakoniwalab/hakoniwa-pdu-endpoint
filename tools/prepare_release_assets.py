#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


PROJECT_NAME = "hakoniwa_pdu_endpoint"


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


def default_artifacts(platform: str, build_dir: Path) -> list[tuple[Path, str]]:
    if platform == "linux":
        return [
            (
                build_dir / "src" / f"lib{PROJECT_NAME}.so",
                f"{PROJECT_NAME}-linux-{{arch}}.so",
            )
        ]
    if platform == "macos":
        return [
            (
                build_dir / "src" / f"lib{PROJECT_NAME}.dylib",
                f"{PROJECT_NAME}-macos-{{arch}}.dylib",
            )
        ]
    return [
        (
            build_dir / "src" / "Release" / f"{PROJECT_NAME}.dll",
            f"{PROJECT_NAME}-windows-{{arch}}.dll",
        ),
        (
            build_dir / "src" / "Release" / f"{PROJECT_NAME}.lib",
            f"{PROJECT_NAME}-windows-{{arch}}.lib",
        ),
    ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Copy and rename build artifacts into GitHub Release asset names."
    )
    parser.add_argument(
        "--platform",
        required=True,
        choices=("linux", "macos", "windows"),
        help="Target platform for the prepared release assets.",
    )
    parser.add_argument(
        "--arch",
        required=True,
        help="Target architecture label. Examples: x86_64, arm64, x64, universal2.",
    )
    parser.add_argument(
        "--build-dir",
        default=None,
        help="Build directory that contains the compiled artifacts.",
    )
    parser.add_argument(
        "--output-dir",
        default="release-assets",
        help="Directory where renamed release assets will be copied.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    platform = args.platform
    arch = normalize_arch(platform, args.arch)

    default_build_dirs = {
        "linux": "build-shared",
        "macos": "build-shared",
        "windows": "build-win",
    }
    build_dir = Path(args.build_dir or default_build_dirs[platform]).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    copied_files: list[Path] = []
    for source_path, target_template in default_artifacts(platform, build_dir):
        if not source_path.exists():
            raise SystemExit(f"Missing build artifact: {source_path}")
        target_path = output_dir / target_template.format(arch=arch)
        shutil.copy2(source_path, target_path)
        copied_files.append(target_path)

    print("Prepared release assets:")
    for path in copied_files:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
