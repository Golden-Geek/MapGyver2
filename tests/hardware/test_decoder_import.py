#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Manual native D3D11VA decode/conversion/bgfx import and fallback refusal."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
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


def run(name, command):
    completed = subprocess.run(list(map(str, command)), text=True, encoding='utf-8',
                               capture_output=True, timeout=30, startupinfo=startup,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    (args.report_directory / f'{name}-stdout.log').write_text(completed.stdout, encoding='utf-8')
    (args.report_directory / f'{name}-stderr.log').write_text(completed.stderr, encoding='utf-8')
    return completed


command = [args.executable.resolve(), '--json', '--fixture-directory', (args.report_directory / 'fixtures').resolve()]
positive = run('decode', command)
if positive.returncode != 0 or positive.stderr:
    raise RuntimeError(f'Native decoder path failed: {positive.returncode}, {positive.stderr}')
report = json.loads(positive.stdout)
assert report['result'] == 'passed' and report['evidenceClass'] == 'native-gpu-d3d11va-bgfx-decoder-import'
assert len(report['cases']) == 3
assert 'nonfree' not in report['ffmpegLicense'].lower()
assert '--enable-nonfree' not in report['ffmpegConfiguration']
for index, case in enumerate(report['cases'], start=1):
    assert case['generation'] == index and case['side'] == [128, 256, 128][index-1]
    assert case['decodePath'] == 'strict D3D11VA; software fallback forbidden'
    assert case['nativeFormat'] == 'NV12' and case['decodedFrames'] == 18
    assert case['applicationDecodedCpuUploadCalls'] == case['applicationDecodeReadbacks'] == 0
    assert case['candidateDrawSubmissions'] == case['videoProcessorBltSubmissions'] == 18
    assert case['validationGpuCopySubmissions'] == case['validationCpuReadbacks'] == 18
    assert case['maxChannelCodeError'] <= case['channelCodeTolerance'] == 2
    fixture = args.report_directory / f'fixtures/mpeg2-{index}.m2v'
    assert fixture.stat().st_size == case['fixtureBytes']
    assert hashlib.sha256(fixture.read_bytes()).hexdigest() == case['fixtureSha256']
    assert len(case['frames']) == 18
    for pts, frame in enumerate(case['frames']):
        assert frame['pts'] == pts and frame['timeBase'] == [1, 30]
        assert frame['appliedBgfxFrame'] > 0 and 0 <= frame['arraySlice'] < frame['arraySize']
        assert frame['maxChannelCodeError'] <= 2
    for snapshot in [case['memoryBefore'], case['memoryAllocated'], case['memoryAfterRetirement']]:
        assert snapshot['workingSetBytes'] > 0 and snapshot['localVideoBudgetBytes'] > 0
(args.report_directory / 'decoder.json').write_text(positive.stdout, encoding='utf-8')

# Reject the real offered hardware format through FFmpeg's callback. No injected
# SDK success, fallback selection or broadened process timeout is permitted.
negative_command = [args.executable.resolve(), '--json', '--reject-hardware-format']
negative = run('rejected-format', negative_command)
if (negative.returncode != 1 or negative.stdout.strip() or
        'decoding to AV_PIX_FMT_NONE is not supported' not in negative.stderr or
        'Decoder probe failed: Send compressed packet: Invalid argument' not in negative.stderr):
    raise RuntimeError(f'Strict hardware-format refusal did not fail: {negative.returncode}, {negative.stdout}, {negative.stderr}')
summary = {'schemaVersion': 1, 'collectedUtc': datetime.now(timezone.utc).isoformat(),
           'evidenceClass': report['evidenceClass'], 'result': 'passed',
           'sourceRevision': report['sourceRevision'], 'sourceDirty': report['sourceDirty'],
           'buildConfiguration': report['buildConfiguration'], 'generations': 3, 'decodedFrames': 54,
           'positiveCommand': list(map(str, command)), 'negativeCommand': list(map(str, negative_command)),
           'rejectedHardwareFormat': {'exitCode': negative.returncode, 'successPayload': False,
                                      'stderr': negative.stderr},
           'physicalOutputQualification': 'not_run', 'productionDeadlineQualification': 'not_run'}
(args.report_directory / 'contract-summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
print(json.dumps(summary, indent=2))
