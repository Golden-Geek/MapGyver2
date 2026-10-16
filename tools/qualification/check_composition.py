#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run native GPU composition goldens and retain exact executable/shader identity."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--probe', type=Path, required=True)
parser.add_argument('--shaders', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
args.report.parent.mkdir(parents=True, exist_ok=True)
result = subprocess.run([str(args.probe.resolve()), '--json'], capture_output=True, timeout=60)
args.report.with_suffix('.stdout.json').write_bytes(result.stdout)
args.report.with_suffix('.stderr.log').write_bytes(result.stderr)
if result.returncode:
    raise SystemExit(result.stderr.decode('utf-8', errors='replace') or f'Native compositor exited {result.returncode}')
report = json.loads(result.stdout)
assert len(report['cases']) == 8 and sum(c['framesSubmitted'] for c in report['cases']) == 160
assert sum(len(c['captures']) for c in report['cases']) == 16
assert all(c['vendorId'] and all(x['maxChannelError'] <= x['tolerance'] == 2 for x in c['captures']) for c in report['cases'])
report['command'] = [str(args.probe.resolve()), '--json']
report['probeSha256'] = hashlib.sha256(args.probe.read_bytes()).hexdigest()
report['shaderBuild'] = json.loads(args.shaders.read_text(encoding='utf-8'))
args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print('Passed eight actual-GPU cases, 160 submitted frames and sixteen fixed-tolerance captures')
