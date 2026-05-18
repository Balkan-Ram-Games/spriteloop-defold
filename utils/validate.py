#!/usr/bin/env python3
"""Validate the SpriteLoop Defold example project with Bob."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_BUILD_SERVER = "https://build.defold.com"
DEFAULT_PLATFORM = "x86_64-win32"
DEFAULT_VARIANT = "debug"

def default_java() -> str:
    return os.environ.get("JAVA") or "java"


def default_bob() -> Path | None:
    bob = os.environ.get("BOB")
    if bob:
        return Path(bob)

    dynamo_home = os.environ.get("DYNAMO_HOME")
    if dynamo_home:
        return Path(dynamo_home) / "share" / "java" / "bob.jar"

    return None


def run_bob(args: list[str], cwd: Path, verbose: bool) -> None:
    if verbose:
        print("+ " + " ".join(args))
        subprocess.run(args, cwd=cwd, check=True)
        return

    result = subprocess.run(
        args,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        print(result.stdout, end="")
        raise subprocess.CalledProcessError(result.returncode, args)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bob", type=Path, default=default_bob())
    parser.add_argument("--java", default=default_java())
    parser.add_argument("--platform", default=DEFAULT_PLATFORM)
    parser.add_argument("--variant", default=DEFAULT_VARIANT)
    parser.add_argument("--build-server", default=DEFAULT_BUILD_SERVER)
    parser.add_argument("--defold-sdk", default=os.environ.get("DEFOLDSDK"))
    parser.add_argument("--verbose", action="store_true", help="Print full Bob output.")
    return parser.parse_args()


def require_file(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(f"Missing required file: {path}")


def required_lib_platforms(bob_platform: str) -> list[str]:
    if bob_platform.endswith("-macos"):
        return ["x86_64-osx", "arm64-osx"]
    return [bob_platform]


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent

    if not args.bob:
        raise FileNotFoundError("Bob not found. Pass --bob, set BOB, or set DYNAMO_HOME.")

    bob = args.bob.expanduser().resolve()
    require_file(bob)

    require_file(repo_root / "spriteloop" / "SDK_VERSION")
    require_file(repo_root / "spriteloop" / "include" / "spriteloop" / "spla.hpp")
    require_file(repo_root / "spriteloop" / "plugins" / "share" / "pluginSpla.jar")

    for lib_platform in required_lib_platforms(args.platform):
        platform_lib_dir = repo_root / "spriteloop" / "lib" / lib_platform
        if not platform_lib_dir.is_dir() or not any(platform_lib_dir.iterdir()):
            raise FileNotFoundError(f"Missing platform library directory: {platform_lib_dir}")

    command = [
        args.java,
        "-jar",
        str(bob),
        f"--platform={args.platform}",
        f"--variant={args.variant}",
        f"--build-server={args.build_server}",
    ]
    if args.defold_sdk:
        command.append(f"--defoldsdk={args.defold_sdk}")
    command.extend(["clean", "build"])

    build_dir = repo_root / "build"
    if build_dir.exists():
        shutil.rmtree(build_dir)

    print(f"Validating SpriteLoop Defold extension")
    print(f"  platform: {args.platform}")
    print(f"  variant:  {args.variant}")
    print(f"  bob:      {bob}")
    print(f"  java:     {args.java}")
    print(f"  sdk:      {args.defold_sdk or 'from Bob/default'}")

    started = time.monotonic()
    run_bob(command, cwd=repo_root, verbose=args.verbose)
    elapsed = time.monotonic() - started

    print(f"Validation passed for {args.platform} in {elapsed:.1f}s.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
