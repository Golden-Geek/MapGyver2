#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Qualify real hardware background preparation without claiming runtime adoption."""
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
command = [str(args.probe.resolve()), '--json']
result = subprocess.run(command, capture_output=True, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW)
args.report.with_suffix('.stdout.json').write_bytes(result.stdout)
args.report.with_suffix('.stderr.log').write_bytes(result.stderr)
if result.returncode:
    raise SystemExit(result.stderr.decode('utf-8', errors='replace') or f'Preparation probe exited {result.returncode}')
report = json.loads(result.stdout)
assert report['vendorId'] and not report['deviceCreationFlags'] & 1  # D3D11_CREATE_DEVICE_SINGLETHREADED
assert report['framesSubmitted'] == 120 and report['backgroundImageGenerations'] == 128
assert report['preparedAtLastFrame'] > report['preparedAtFirstFrame']
assert report['retainedGenerationCapacity'] == 8 and report['retainedImagePayloadCapacityBytes'] == 1048576
assert report['backgroundCpuUploadBytes'] == 16777216 and report['backgroundGpuCopySubmissions'] == 0
assert report['invalidRequestsRejected'] == 5 and report['postCompositorShutdownPreparation']
assert report['backgroundImageBitwiseFloatReadback']
assert report['activeOutputMaxChannelError'] <= report['activeOutputTolerance'] == 2
live = report['liveImageHandoff']
assert live['appliedGeneration'] == live['appliedRevision'] == 3 and live['gpuCopySubmissions'] == 2
assert live['copiedPayloadBytes'] == live['backgroundAcceptedCandidateCpuUploadBytes'] == 262144
assert live['candidateOwnershipBackpressureVerified'] and live['actualCompletionBeforeRetirement'] and live['retirementOnPreparationThread']
assert live['staleForeignLayoutCapacityRequestsRejected'] == 5 and live['captures'] == 6
assert live['foreignOwnerRequestsRejected'] == 2 and live['validationRejectedCandidateCpuUploadBytes'] == 393216
assert live['shutdownWithPendingCopy'] and live['shutdownProbeGpuCopySubmissions'] == 1 and live['shutdownProbeCpuUploadBytes'] == 262144
assert live['maxChannelError'] <= live['tolerance'] == 2
borrowed = report['borrowedRenderTarget']
assert (borrowed['width'], borrowed['height']) == (257, 131) and borrowed['oldFramesAdvanced'] >= 5
assert borrowed['maxChannelError'] <= borrowed['tolerance'] == 2
graph = report['graphHandoff']
assert graph['graphsApplied'] == 2 and graph['oldSceneFramesDuringStaging'] > 0 and graph['retirementFrames'] > 0
assert graph['partiallyBoundGraphCancelled'] and graph['backgroundRetirement'] and graph['captures'] == 6
assert graph['maxChannelError'] <= graph['tolerance'] == 2
report['command'] = command
report['probeSha256'] = hashlib.sha256(args.probe.read_bytes()).hexdigest()
args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print('Passed concurrent preparation, two live image handoffs, completion/retirement and six live-state goldens')
