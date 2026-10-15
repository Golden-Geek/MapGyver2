#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Manual native hidden multi-window API/lifetime contract; no scanout claim."""
import argparse
from datetime import datetime, timezone
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
command = [str(args.executable.resolve()), '--json']
result = subprocess.run(command, text=True, encoding='utf-8', capture_output=True,
                        timeout=30, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
(args.report_directory / 'window-stdout.log').write_text(result.stdout, encoding='utf-8')
(args.report_directory / 'window-stderr.log').write_text(result.stderr, encoding='utf-8')
if result.returncode or result.stderr:
    raise RuntimeError(f'Native multi-window API failed: {result.returncode}, {result.stderr}')
report = json.loads(result.stdout)
assert report['result'] == 'passed' and report['evidenceClass'] == 'native-gpu-hidden-sdl-bgfx-multi-window-api'
assert report['physicalTwoOutputQualification'] == 'not_run'
assert report['windowPixels'] == 'not_read_back' and report['opticalSynchronization'] == 'not_claimed'
assert report['productionDeadlineBudget'] == 'not_qualified'
assert report['remainingSdlWindows'] == 1  # Only the probe's original hidden bgfx context window.
assert report['memoryBefore']['bgfxFrameBufferHandles'] == report['memoryAfterRetirement']['bgfxFrameBufferHandles'] == 0
assert len(report['stages']) == 4
sequence = 0
previous_applied = 0
for index, stage in enumerate(report['stages']):
    count = [3,3,2,3][index]
    assert stage['stage'] == index and len(stage['windows']) == count
    assert stage['memory']['bgfxFrameBufferHandles'] == count
    assert stage['memory']['localVideoBudgetBytes'] > 0
    assert len({window['sdlWindowId'] for window in stage['windows']}) == count
    assert all(window['hidden'] for window in stage['windows'])
    assert len(stage['frames']) == 3
    for frame in stage['frames']:
        sequence += 1
        assert frame['sourceSequence'] == sequence and frame['appliedBgfxFrame'] > previous_applied
        previous_applied = frame['appliedBgfxFrame']
        assert frame['windowDrawSubmissions'] == frame['sourceCpuUploadCalls'] == count
        assert frame['cpuReadbacks'] == 0
initial, resized, removed, recreated = [stage['windows'] for stage in report['stages']]
for port in range(3):
    assert initial[port]['outputId'] == resized[port]['outputId'] == f'output-{port}'
    assert initial[port]['sdlWindowId'] == resized[port]['sdlWindowId']
    assert initial[port]['width'] == initial[port]['height'] == 64+port*16
    assert resized[port]['width'] == resized[port]['height'] == 128+port*16
    assert initial[port]['resourceGeneration'] == 1 and resized[port]['resourceGeneration'] == 2
assert [window['outputId'] for window in removed] == ['output-0','output-2']
assert [window['sdlWindowId'] for window in removed] == [resized[0]['sdlWindowId'],resized[2]['sdlWindowId']]
assert recreated[1]['sdlWindowId'] != initial[1]['sdlWindowId'] and recreated[1]['resourceGeneration'] == 3
assert (recreated[1]['width'],recreated[1]['height']) == (96,112)
assert recreated[0]['sdlWindowId'] == initial[0]['sdlWindowId'] and recreated[2]['sdlWindowId'] == initial[2]['sdlWindowId']
(args.report_directory / 'windows.json').write_text(result.stdout, encoding='utf-8')
summary = {'schemaVersion':1, 'collectedUtc':datetime.now(timezone.utc).isoformat(),
           'evidenceClass':report['evidenceClass'], 'result':'passed',
           'sourceRevision':report['sourceRevision'], 'sourceDirty':report['sourceDirty'],
           'buildConfiguration':report['buildConfiguration'], 'command':command,
           'applicationFrames':12, 'windowDrawSubmissions':33, 'stages':4,
           'remainingBgfxFrameBufferHandles':0, 'remainingContextWindows':1,
           'physicalTwoOutputQualification':'not_run', 'windowPixelQualification':'not_run'}
(args.report_directory / 'contract-summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
