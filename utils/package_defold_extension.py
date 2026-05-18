#!/usr/bin/env python3
"""Stage and zip the SpriteLoop Defold extension."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


DEFAULT_STAGE = Path(".artifacts/defold-extension-stage")
DEFAULT_OUTPUT = Path(".artifacts/spriteloop-defold-extension.zip")


def parse_platform_lib(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("expected <arc-platform>=<library-or-directory>")
    platform, path = value.split("=", 1)
    platform = platform.strip()
    if not platform:
        raise argparse.ArgumentTypeError("arc-platform must not be empty")
    return platform, Path(path)


def copy_platform_lib(platform: str, source: Path, lib_root: Path) -> None:
    if not source.exists():
        raise FileNotFoundError(f"platform library path does not exist: {source}")

    destination = lib_root / platform
    destination.mkdir(parents=True, exist_ok=True)

    if source.is_dir():
        for child in source.iterdir():
            if child.is_file():
                shutil.copy2(child, destination / child.name)
        return

    shutil.copy2(source, destination / source.name)


def remove_miniz_binaries(lib_root: Path) -> None:
    if not lib_root.exists():
        return

    for path in lib_root.rglob("*"):
        if path.is_file() and path.stem.lower() == "miniz":
            path.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adapter-root", type=Path, default=Path("."))
    parser.add_argument("--extension-name", default="spriteloop")
    parser.add_argument("--stage-dir", type=Path, default=DEFAULT_STAGE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--platform-lib",
        action="append",
        type=parse_platform_lib,
        default=[],
        metavar="ARC_PLATFORM=PATH",
        help="Copy a built library or directory into spriteloop/lib/<arc-platform>/.",
    )
    parser.add_argument(
        "--no-existing-libs",
        action="store_true",
        help="Remove libs copied from the source tree before adding --platform-lib inputs.",
    )
    args = parser.parse_args()

    source_root = args.adapter_root / args.extension_name
    if not source_root.is_dir():
        raise FileNotFoundError(f"Defold extension folder not found: {source_root}")

    if args.stage_dir.exists():
        shutil.rmtree(args.stage_dir)
    staged_extension = args.stage_dir / args.extension_name
    shutil.copytree(
        source_root,
        staged_extension,
        ignore=shutil.ignore_patterns(
            ".DS_Store",
            "Thumbs.db",
            "__pycache__",
            "*.pyc",
            "*.user",
            "*.suo",
            "*.pdb",
            "*.ilk",
            "*.obj",
        ),
    )

    lib_root = staged_extension / "lib"
    if args.no_existing_libs and lib_root.exists():
        shutil.rmtree(lib_root)
    lib_root.mkdir(parents=True, exist_ok=True)

    for platform, source in args.platform_lib:
        copy_platform_lib(platform, source, lib_root)

    remove_miniz_binaries(lib_root)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        args.output.unlink()
    archive_base = args.output.with_suffix("")
    shutil.make_archive(str(archive_base), "zip", root_dir=args.stage_dir)
    archive_path = archive_base.with_suffix(".zip")
    if archive_path != args.output:
        archive_path.replace(args.output)

    print(f"Packaged {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

