#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real GPU coherent application frames; missing and stale member failures."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', required=True, type=Path)
parser.add_argument('--report-directory', required=True, type=Path)
args = parser.parse_args()
if not __debug__:
    raise RuntimeError('This qualification contract requires Python assertions enabled')
args.report_directory.mkdir(parents=True, exist_ok=True)
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE


def run(name, *extra):
    command = [str(args.executable.resolve()), '--json', *extra]
    result = subprocess.run(command, text=True, encoding='utf-8', capture_output=True,
                            timeout=30, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    (args.report_directory / f'{name}-stdout.log').write_text(result.stdout, encoding='utf-8')
    (args.report_directory / f'{name}-stderr.log').write_text(result.stderr, encoding='utf-8')
    return command, result


def expected_hash(side, generation, sequence, port):
    value = 2166136261
    marker = generation*100000 + sequence*256 + port
    for y in range(side):
        for x in range(side):
            word = (marker ^ ((x*0x9e3779b9) & 0xffffffff) ^ ((y*0x85ebca6b) & 0xffffffff)) & 0xffffffff
            for byte in struct.pack('<I', word):
                value = ((value ^ byte)*16777619) & 0xffffffff
    return value


command, positive = run('frame-sets')
if positive.returncode or positive.stderr:
    raise RuntimeError(f'Native frame-set validation failed: {positive.returncode}, {positive.stderr}')
report = json.loads(positive.stdout)
assert report['result'] == 'passed' and report['evidenceClass'] == 'native-gpu-cooperating-bgfx-frame-sets'
assert len(report['sets']) == 8
previous_frame = 0
for sequence, frame_set in enumerate(report['sets'], start=1):
    generation = (sequence-1)//2 + 1
    count = [2,5,11,2][generation-1]
    side = [64,128,256,64][generation-1]
    assert frame_set['frameSequence'] == sequence and frame_set['pts'] == sequence-1
    assert frame_set['timeBase'] == [1,30] and frame_set['generation'] == generation
    assert frame_set['requiredPorts'] == len(frame_set['ports']) == count
    assert frame_set['completeAfterGpuCompletion'] and frame_set['appliedBgfxFrame'] > previous_frame
    previous_frame = frame_set['appliedBgfxFrame']
    assert frame_set['sourceCpuUploadCalls'] == frame_set['samplingDrawSubmissions'] == count
    assert frame_set['validationGpuCopySubmissions'] == frame_set['validationCpuReadbacks'] == count
    for port, member in enumerate(frame_set['ports']):
        assert member['portId'] == f'port-{port}' and member['side'] == side
        assert member['format'] == ('RGBA8' if port % 2 == 0 else 'BGRA8')
        assert member['resourceGeneration'] == generation and member['frameSequence'] == sequence
        assert member['appliedBgfxFrame'] == frame_set['appliedBgfxFrame']
        assert member['pixelsAndAlphaMatchExactly']
        assert member['pixelHash'] == expected_hash(side,generation,sequence,port)
(args.report_directory / 'frame-sets.json').write_text(positive.stdout, encoding='utf-8')
negatives = []
for option, expected in [('--stale-member', 'Frame-set member pixel identity mismatch'),
                         ('--missing-member', 'Candidate set contains a missing source')]:
    negative_command, result = run(option.removeprefix('--'), option)
    if result.returncode != 1 or result.stdout.strip() or expected not in result.stderr:
        raise RuntimeError(f'{option} failed for the wrong reason: {result.returncode}, {result.stdout}, {result.stderr}')
    negatives.append({'command': negative_command, 'exitCode': result.returncode, 'stderr': result.stderr, 'successPayload': False})
summary = {'schemaVersion': 1, 'collectedUtc': datetime.now(timezone.utc).isoformat(),
           'evidenceClass': report['evidenceClass'], 'result': 'passed',
           'sourceRevision': report['sourceRevision'], 'sourceDirty': report['sourceDirty'],
           'buildConfiguration': report['buildConfiguration'], 'command': command,
           'sets': 8, 'memberPixelCases': 40, 'negativeContracts': negatives,
           'physicalScanoutSynchronization': 'not_qualified'}
(args.report_directory / 'contract-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
print(json.dumps(summary, indent=2))
