#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bootstrap the exact manifest registry; CMake installs selected dependencies."""
import argparse
import json
from pathlib import Path
import platform
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
VCPKG = ROOT / "out" / "deps" / "vcpkg"
UPSTREAM = "https://github.com/microsoft/vcpkg.git"

def run(*command, cwd=ROOT, capture=False):
    print("+", " ".join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), cwd=cwd, check=True,
                          stdout=subprocess.PIPE if capture else None, text=True).stdout

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--triplet", required=True, choices=("x64-windows", "x64-linux", "arm64-osx"))
    parser.add_argument("--core-only", action="store_true", help="Require the Linux/macOS portable profile")
    args = parser.parse_args()
    hosts = {"x64-windows": ("Windows", {"AMD64", "x86_64"}),
             "x64-linux": ("Linux", {"x86_64", "AMD64"}),
             "arm64-osx": ("Darwin", {"arm64", "aarch64"})}
    system, machines = hosts[args.triplet]
    if platform.system() != system or platform.machine() not in machines:
        parser.error(f"{args.triplet} does not match this native host")
    if args.core_only and args.triplet == "x64-windows":
        parser.error("--core-only selects Linux/macOS portable CI; use Windows native presets here")
    baseline = json.loads((ROOT / "vcpkg-configuration.json").read_text(encoding="utf-8"))["default-registry"]["baseline"]
    if not re.fullmatch(r"[0-9a-f]{40}", baseline):
        raise ValueError("Dependency baseline must be a full commit SHA")
    VCPKG.parent.mkdir(parents=True, exist_ok=True)
    new_checkout = not VCPKG.exists()
    if new_checkout:
        run("git", "clone", "--filter=blob:none", "--no-checkout", UPSTREAM, VCPKG)
    origin = run("git", "remote", "get-url", "origin", cwd=VCPKG, capture=True).strip()
    if origin != UPSTREAM:
        raise ValueError("Refusing an unexpected vcpkg origin")
    status = run("git", "status", "--porcelain", "--untracked-files=no", cwd=VCPKG, capture=True)
    head = run("git", "rev-parse", "HEAD", cwd=VCPKG, capture=True).strip()
    # A new --no-checkout clone has a deliberately empty index.
    if status and not new_checkout:
        raise ValueError("Refusing to replace modified dependency sources")
    if head != baseline or not (VCPKG / "bootstrap-vcpkg.bat").exists():
        run("git", "checkout", "--detach", baseline, cwd=VCPKG)
    script = VCPKG / ("bootstrap-vcpkg.bat" if system == "Windows" else "bootstrap-vcpkg.sh")
    if system == "Windows":
        run(script, "-disableMetrics", cwd=VCPKG)
    else:
        run("sh", script, "-disableMetrics", cwd=VCPKG)
    run(VCPKG / ("vcpkg.exe" if system == "Windows" else "vcpkg"), "version")
    print(f"Verified dependency root {baseline}; triplet={args.triplet}; "
          f"profile={'portable-core' if args.core_only else 'windows-native'}. "
          "Configure its CMake preset to install the manifest.")

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"Bootstrap failed: {exc}", file=sys.stderr)
        sys.exit(1)
