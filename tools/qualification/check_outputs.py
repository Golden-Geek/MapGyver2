#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run actual native output window API qualification, retaining binary identity."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--probe', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
args.report.parent.mkdir(parents=True, exist_ok=True)
result = subprocess.run([str(args.probe.resolve()), '--json'], capture_output=True, timeout=60)
args.report.with_suffix('.stdout.json').write_bytes(result.stdout)
args.report.with_suffix('.stderr.log').write_bytes(result.stderr)
if result.returncode:
    raise SystemExit(result.stderr.decode('utf-8', errors='replace') or f'Native output probe exited {result.returncode}')
report = json.loads(result.stdout)
assert report['windows'] == 3 and report['framesSubmitted'] == 13 and len(report['captures']) == 11
assert report['foreignOwnerRejected'] is True
assert all(x['maxChannelError'] <= x['tolerance'] == 2 for x in report['captures'])
mapping = report['mapping']
assert mapping['surfaces'] == mapping['windows'] == 3 and mapping['patchCounts'] == [5,5,5,4] and mapping['compositionNodes'] == 2
assert len(mapping['captures']) == 12 and all(x['maxChannelError'] <= x['tolerance'] == 2 for x in mapping['captures'])
assert mapping['sourceCpuUploadBytes'] == 4*128*64*16
assert mapping['oldFramesDuringPreparation'] > 0 and mapping['retirementFrames'] > 0
report['command'] = [str(args.probe.resolve()), '--json']
report['probeSha256'] = hashlib.sha256(args.probe.read_bytes()).hexdigest()
args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print('Passed hidden native outputs and many-to-many projective/overlap mapping GPU goldens')
