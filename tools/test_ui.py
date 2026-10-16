#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run real-browser editor contracts against the selected owned native process."""
import argparse
import os
from pathlib import Path
import subprocess
from build_ui import ROOT, npm_command
parser = argparse.ArgumentParser(description=__doc__)
target = parser.add_mutually_exclusive_group(required=True)
target.add_argument('--engine', type=Path)
target.add_argument('--fixture', type=Path)
parser.add_argument('--assets', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
env = os.environ.copy()
env['MAPGYVER_TEST_ENGINE' if args.engine else 'MAPGYVER_TEST_FIXTURE'] = str((args.engine or args.fixture).resolve())
env['MAPGYVER_TEST_ASSETS'] = str(args.assets.resolve())
env['MAPGYVER_UI_REPORT'] = str(args.report.resolve())
args.report.mkdir(parents=True, exist_ok=True)
raise SystemExit(subprocess.run(npm_command('test'), cwd=ROOT/'ui', env=env).returncode)
