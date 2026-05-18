#!/usr/bin/env python3
"""Build and refresh the SpriteLoop Defold editor/Bob plugin jar."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


DEFAULT_BUILD_SERVER = "https://build.defold.com"
DEFAULT_VARIANT = "headless"
DEFAULT_PLATFORM = "x86_64-win32"


def find_default_java() -> str:
    return os.environ.get("JAVA") or "java"


def find_default_bob() -> Path | None:
    bob = os.environ.get("BOB")
    if bob:
        return Path(bob)

    dynamo_home = os.environ.get("DYNAMO_HOME")
    if dynamo_home:
        return Path(dynamo_home) / "share" / "java" / "bob.jar"

    return None


def run(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(args))
    return subprocess.run(args, cwd=cwd, text=True, check=True)


def read_defold_sdk(java: str, bob: Path, project_root: Path) -> str:
    result = subprocess.run(
        [java, "-jar", str(bob), "--version"],
        cwd=project_root,
        text=True,
        capture_output=True,
        check=True,
    )
    tokens = result.stdout.strip().split()
    if not tokens:
        raise RuntimeError("Bob --version returned no output.")
    return tokens[-1]


def find_plugin_jar(build_root: Path) -> Path:
    jars = [
        path
        for path in build_root.rglob("pluginSpla.jar")
        if "spriteloop" in path.parts
    ]
    if not jars:
        raise FileNotFoundError(f"Bob did not produce pluginSpla.jar under {build_root}.")
    if len(jars) > 1:
        joined = "\n".join(str(path) for path in jars)
        raise RuntimeError(f"Bob produced multiple pluginSpla.jar files:\n{joined}")
    return jars[0]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bob", type=Path, default=find_default_bob())
    parser.add_argument("--java", default=find_default_java())
    parser.add_argument("--defold-sdk", default=os.environ.get("DEFOLDSDK"))
    parser.add_argument("--build-server", default=DEFAULT_BUILD_SERVER)
    parser.add_argument("--variant", default=DEFAULT_VARIANT)
    parser.add_argument(
        "--platform",
        action="append",
        dest="platforms",
        default=[],
        help="Defold arc-platform to build. May be passed more than once.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent

    if not args.bob:
        raise FileNotFoundError("Bob not found. Pass --bob, set BOB, or set DYNAMO_HOME.")

    bob = args.bob.expanduser().resolve()
    if not bob.is_file():
        raise FileNotFoundError(f"Bob not found: {bob}")

    defold_sdk = args.defold_sdk or read_defold_sdk(args.java, bob, project_root)
    platforms = args.platforms or [DEFAULT_PLATFORM]

    target_dir = project_root / "spriteloop" / "plugins" / "share"
    current_jar = target_dir / "pluginSpla.jar"
    build_root = project_root / "build"

    backup_dir = Path(tempfile.mkdtemp(prefix="spriteloop-plugin-backup-"))
    backup_jar = backup_dir / "pluginSpla.jar"
    had_existing_jar = current_jar.exists()

    try:
        if had_existing_jar:
            shutil.copy2(current_jar, backup_jar)
            current_jar.unlink()

        for platform in platforms:
            run(
                [
                    args.java,
                    "-jar",
                    str(bob),
                    f"--platform={platform}",
                    "clean",
                    "build",
                    "--build-artifacts=plugins",
                    f"--variant={args.variant}",
                    f"--build-server={args.build_server}",
                    f"--defoldsdk={defold_sdk}",
                ],
                cwd=project_root,
            )

        plugin_jar = find_plugin_jar(build_root)
        target_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(plugin_jar, current_jar)
        print(f"Updated {current_jar}")
    except Exception:
        if had_existing_jar and backup_jar.exists() and not current_jar.exists():
            target_dir.mkdir(parents=True, exist_ok=True)
            shutil.copy2(backup_jar, current_jar)
        raise
    finally:
        shutil.rmtree(backup_dir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
