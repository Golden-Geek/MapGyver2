#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unimplemented and misspelled feature requests must fail for the right reason."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True, type=Path)
parser.add_argument("--cmake", required=True)
parser.add_argument("--generator", required=True)
args = parser.parse_args()
for option, expected in [("PM_BUILD_PLATFORM", "is not implemented/qualified yet"),
                         ("PM_BUILD_MEDAI", "Unknown MapGyver feature selection")]:
    with tempfile.TemporaryDirectory(prefix="mapgyver config ") as temp:
        result = subprocess.run([args.cmake, "-S", str(args.source), "-B", temp,
                                 "-G", args.generator, f"-D{option}=ON", "-DBUILD_TESTING=OFF"],
                                text=True, capture_output=True, timeout=60)
        if result.returncode == 0 or expected not in result.stdout + result.stderr:
            raise RuntimeError(f"{option} did not fail for its feature contract:\n{result.stdout}\n{result.stderr}")
        print(f"{option}: correctly rejected")
