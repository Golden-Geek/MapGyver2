#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit real-GPU contract. Never run it as a hosted/software-rendering pass."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', required=True, type=Path)
parser.add_argument('--report-directory', required=True, type=Path)
args = parser.parse_args()
args.report_directory.mkdir(parents=True, exist_ok=True)
expected = {(mode, fmt, side) for mode in ('bgfx_owned', 'imported_shared')
            for fmt in ('RGBA8', 'BGRA8') for side in (64, 128, 256)}
try:
    result = subprocess.run([str(args.executable), '--json'], capture_output=True, text=True, timeout=30)
except subprocess.TimeoutExpired as error:
    (args.report_directory / 'sampling.timeout.stdout').write_bytes(error.stdout or b'')
    (args.report_directory / 'sampling.timeout.stderr').write_bytes(error.stderr or b'')
    raise
(args.report_directory / 'sampling.stdout.json').write_text(result.stdout, encoding='utf-8')
(args.report_directory / 'sampling.stderr.log').write_text(result.stderr, encoding='utf-8')
if result.returncode != 0:
    raise RuntimeError(f'Real GPU sampling failed: {result.stderr}')
report = json.loads(result.stdout)
if report['result'] != 'passed' or report['evidenceClass'] != 'native-gpu-bgfx-d3d11-sampling' or report['adapter']['software']:
    raise RuntimeError('Wrong evidence class or software adapter')
cases = report['cases']
if len(cases) != 12 or {(case['mode'], case['format'], case['width']) for case in cases} != expected:
    raise RuntimeError('Missing or duplicate GPU format/size/ownership cases')
if any(not case['pixelsAndAlphaMatchExactly'] or not case['retiredAfterGpuCompletion'] for case in cases):
    raise RuntimeError('GPU pixel/alpha/lifetime contract failed')
if [case['generation'] for case in cases] != list(range(1, 13)):
    raise RuntimeError('Resource generations are not distinct and ordered')
frames = [case['appliedBgfxFrame'] for case in cases]
if frames != sorted(set(frames)):
    raise RuntimeError('Applied frame identity did not advance')
fault = subprocess.run([str(args.executable), '--json', '--hold-producer-key'],
                       capture_output=True, text=True, timeout=15)
(args.report_directory / 'stalled-producer.stdout').write_text(fault.stdout, encoding='utf-8')
(args.report_directory / 'stalled-producer.stderr.log').write_text(fault.stderr, encoding='utf-8')
if (fault.returncode != 1 or fault.stdout or
        fault.stderr.strip() != 'bgfx texture interop failed: Consumer keyed mutex acquire status=0x102'):
    raise RuntimeError(f'Stalled producer did not fail with the actual WAIT_TIMEOUT result: {fault.returncode}, {fault.stderr}')
summary = {'schemaVersion': 1, 'collectedUtc': datetime.now(timezone.utc).isoformat(),
           'evidenceClass': report['evidenceClass'], 'sourceRevision': report['sourceRevision'],
           'sourceDirty': report['sourceDirty'], 'result': 'passed', 'positiveCases': len(cases),
           'stalledProducer': {'exitCode': fault.returncode, 'status': 'WAIT_TIMEOUT', 'successPayload': False}}
(args.report_directory / 'contract-summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
print(json.dumps(summary, indent=2))
