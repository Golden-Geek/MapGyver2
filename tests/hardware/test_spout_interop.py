#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real cross-version Spout processes, pixel identity, resize/restart and stalls."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import uuid
from spout_fixture import Actor, wait_frame

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--participant', required=True, type=Path)
parser.add_argument('--reference', required=True, type=Path)
parser.add_argument('--report-directory', required=True, type=Path)
args = parser.parse_args()
args.report_directory.mkdir(parents=True, exist_ok=True)

reports = []
for direction, sender_exe, receiver_exe in [('mapgyver_to_reference', args.participant, args.reference),
                                             ('reference_to_mapgyver', args.reference, args.participant)]:
    actors = []
    name = 'MapGyver_M01_' + uuid.uuid4().hex
    report = {'direction': direction, 'senderName': name, 'result': 'running', 'frames': [], 'actors': []}
    reports.append(report)
    try:
        sender = Actor(sender_exe, 'sender', name); actors.append(sender)
        receiver = Actor(receiver_exe, 'receiver', name); actors.append(receiver)
        report['actors'].extend([sender.info, receiver.info])
        if sender.dll_hash == receiver.dll_hash:
            raise RuntimeError('Qualification did not use independent SDK versions')
        if sender.info['sourceRevision'] != receiver.info['sourceRevision'] or sender.info['sourceDirty'] != receiver.info['sourceDirty']:
            raise RuntimeError('Participant and reference peer source identity differ')
        if sender.info['adapter'] != receiver.info['adapter']:
            raise RuntimeError('Participants did not use the same actual hardware adapter')
        expected_renderers = ('bgfx-D3D11', 'native-reference') if direction == 'mapgyver_to_reference' else ('native-reference', 'bgfx-D3D11')
        if (sender.info['renderer'], receiver.info['renderer']) != expected_renderers:
            raise RuntimeError('The real candidate renderer was not connected to the independent peer')
        if receiver.command(op='poll').get('ready'):
            raise RuntimeError('Absent sender claimed frame readiness')
        marker = 100
        for repeat in range(2):
            for fmt in ('RGBA8', 'BGRA8'):
                for side in (64, 128, 256):
                    marker += 1
                    sent = sender.command(op='publish', side=side, marker=marker, format=fmt)
                    if not sent.get('copySubmitted'):
                        raise RuntimeError(f'Uncontended publication did not submit: {sent}')
                    frame = wait_frame(receiver, side, marker, fmt)
                    candidate_frame = sent['appliedBgfxFrame'] if direction == 'mapgyver_to_reference' else frame['appliedBgfxFrame']
                    if candidate_frame <= 0:
                        raise RuntimeError('Spout transfer did not traverse an applied candidate render frame')
                    report['frames'].append(frame)
        # Connected without content advancement is a stalled source, not a new frame.
        repeated = receiver.command(op='poll')
        if not repeated['ready'] or repeated['contentAdvanced'] or repeated['marker'] != marker:
            raise RuntimeError('A repeated/stalled frame was mislabeled as new content')
        # Hold the producer's real Spout mutex. The receiver must not enter the SDK copy path.
        sender.command(op='lock')
        before = receiver.command(op='stats')
        blocked = receiver.command(op='poll')
        sender.command(op='unlock')
        if not blocked.get('busy') or blocked['stats']['sdkGpuCopyPathCalls'] != before['sdkGpuCopyPathCalls']:
            raise RuntimeError('Stalled producer created a fictitious receive/copy')
        # A slow receiver similarly cannot block publication or claim a successful SDK call.
        receiver.command(op='lock')
        before = sender.command(op='stats')
        dropped = sender.command(op='publish', side=256, marker=marker+1, format='BGRA8')
        receiver.command(op='unlock')
        if dropped.get('copySubmitted') or not dropped.get('busy') or dropped['stats']['sdkGpuCopyPathCalls'] != before['sdkGpuCopyPathCalls']:
            raise RuntimeError('Slow consumer falsely advanced publication')
        # Same name, same dimensions/format, different producer process and marker.
        old_pid = sender.process.pid
        sender.stop()
        disconnected = receiver.command(op='poll')
        if disconnected.get('connected') or disconnected.get('ready'):
            raise RuntimeError(f'Gracefully closed sender still appeared ready: {disconnected}')
        sender = Actor(sender_exe, 'sender', name); actors.append(sender)
        report['actors'].append(sender.info)
        if sender.process.pid == old_pid:
            raise RuntimeError('Producer restart did not create a new process')
        marker += 2
        sender.command(op='publish', side=256, marker=marker, format='BGRA8')
        recovered = wait_frame(receiver, 256, marker, 'BGRA8')
        if recovered['generation'] <= report['frames'][-1]['generation']:
            raise RuntimeError('Producer restart reused the previous resource generation')
        report.update({'result': 'passed', 'producerStall': blocked, 'slowReceiver': dropped,
                       'disconnected': disconnected, 'restart': recovered,
                       'senderStats': sender.command(op='stats'), 'receiverStats': receiver.command(op='stats')})
        candidate_stats = report['senderStats'] if direction == 'mapgyver_to_reference' else report['receiverStats']
        reference_stats = report['receiverStats'] if direction == 'mapgyver_to_reference' else report['senderStats']
        if candidate_stats['candidateDrawSubmissions'] == 0 or reference_stats['candidateDrawSubmissions'] != 0:
            raise RuntimeError('Candidate/reference render paths were not independent')
    except Exception as error:
        report['result'] = 'failed'; report['error'] = str(error)
        raise
    finally:
        cleanup_errors = []
        for index, actor in enumerate(reversed(actors)):
            try:
                actor.stop()
            except Exception as error:
                cleanup_errors.append(str(error))
            (args.report_directory / f'{direction}-{index}-transcript.json').write_text(
                json.dumps({'info': getattr(actor, 'info', None), 'stderr': list(actor.errors), 'traffic': actor.log}, indent=2) + '\n', encoding='utf-8')
        (args.report_directory / 'interop.json').write_text(json.dumps({
            'schemaVersion': 1, 'collectedUtc': datetime.now(timezone.utc).isoformat(),
            'evidenceClass': 'native-gpu-spout-cross-version-processes',
            'counterMeaning': 'Controlled native API-copy paths, not physical hardware transfer counters; readback is validation only',
            'reports': reports, 'cleanupErrors': cleanup_errors}, indent=2) + '\n', encoding='utf-8')
        if cleanup_errors:
            raise RuntimeError(f'Owned participant cleanup failed: {cleanup_errors}')
print(json.dumps({'result': 'passed', 'directions': len(reports), 'pixelCases': sum(len(item['frames']) for item in reports)}, indent=2))
