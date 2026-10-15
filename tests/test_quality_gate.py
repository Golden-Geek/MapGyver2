#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Skipped, cancelled, missing and failing matrix prerequisites cannot pass CI."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--script", required=True, type=Path)
args = parser.parse_args()
names = ("PLAN_RESULT", "WINDOWS_RESULT", "PORTABLE_RESULT")
success = dict.fromkeys(names, "success")
cases = [(success, 0)]
for name in names:
    for value in (None, "failure", "cancelled", "skipped", "", "timed_out"):
        values = dict(success)
        if value is None:
            del values[name]
        else:
            values[name] = value
        cases.append((values, 1))
for values, expected in cases:
    environment = {key: value for key, value in os.environ.items() if key not in names}
    environment.update(values)
    result = subprocess.run([sys.executable, str(args.script)], env=environment,
                            capture_output=True, text=True, timeout=10)
    if result.returncode != expected:
        raise RuntimeError(f"Gate returned {result.returncode}, expected {expected}, inputs={values}: {result.stderr}")
print(f"Verified {len(cases)} quality-gate result combinations.")
