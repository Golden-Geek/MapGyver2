#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned SDK/GPU process death, absent consumers and independent worker restart."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import uuid
from spout_fixture import Actor, wait_frame

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--participant', required=True, type=Path)
parser.add_argument('--reference', required=True, type=Path)
parser.add_argument('--report-directory', required=True, type=Path)
args = parser.parse_args()
args.report_directory.mkdir(parents=True, exist_ok=True)


def terminate_owned(actor):
    if actor.process.poll() is not None:
        raise RuntimeError('Worker exited before the intended fault')
    actor.process.kill()  # Only this fixture's Popen handle, never discovery/name-based termination.
    actor.process.wait(timeout=5)
    actor.output_thread.join(timeout=1); actor.error_thread.join(timeout=1)
    if actor.process.returncode != 1 or actor.errors or actor.reader_error:
        raise RuntimeError(f'Unexpected owned-process termination: {actor.process.returncode}, {actor.errors}, {actor.reader_error}')
    actor.intentionally_terminated = True


reports = []
for direction, sender_exe, receiver_exe in [('mapgyver_to_reference', args.participant, args.reference),
                                           ('reference_to_mapgyver', args.reference, args.participant)]:
    actors = []
    name = 'MapGyver_M01_Fault_' + uuid.uuid4().hex
    report = {'direction': direction, 'senderName': name, 'result': 'running'}
    reports.append(report)
    try:
        sender = Actor(sender_exe, 'sender', name); actors.append(sender)
        receiver = Actor(receiver_exe, 'receiver', name); actors.append(receiver)
        if sender.dll_hash == receiver.dll_hash or sender.info['adapter'] != receiver.info['adapter']:
            raise RuntimeError('Independent real SDK/GPU fixture not established')
        if sender.info['sourceRevision'] != receiver.info['sourceRevision'] or sender.info['sourceDirty'] != receiver.info['sourceDirty']:
            raise RuntimeError('Fault participants have different source identities')
        expected = ('bgfx-D3D11', 'native-reference') if direction == 'mapgyver_to_reference' else ('native-reference', 'bgfx-D3D11')
        if (sender.info['renderer'], receiver.info['renderer']) != expected:
            raise RuntimeError('Fault fixture bypassed the candidate renderer')
        sender.command(op='publish', side=128, marker=201, format='RGBA8')
        initial = wait_frame(receiver, 128, 201, 'RGBA8')
        receiver_pid = receiver.process.pid
        terminate_owned(receiver)
        # Native publishing continues with no receiver, browser or preview callback.
        absent = []
        for marker in range(202, 208):
            result = sender.command(op='publish', side=128, marker=marker, format='RGBA8')
            if not result.get('copySubmitted') or result.get('busy'):
                raise RuntimeError('Consumer death stopped native producer progress')
            absent.append(result)
        started = time.monotonic()
        receiver = Actor(receiver_exe, 'receiver', name); actors.append(receiver)
        if receiver.process.pid == receiver_pid:
            raise RuntimeError('Consumer restart reused a process identity')
        reconnected = wait_frame(receiver, 128, 207, 'RGBA8')
        reconnect_ms = (time.monotonic() - started) * 1000
        # A producer dying while owning the real mutex must be disconnected or
        # report an abandoned lock, never copied stale data or SDK success.
        sender.command(op='lock')
        producer_pid = sender.process.pid
        terminate_owned(sender)
        try:
            abandoned = receiver.command(op='poll')
        except RuntimeError:
            receiver.process.wait(timeout=5)
            receiver.error_thread.join(timeout=1)
            diagnostic = ''.join(receiver.errors)
            if receiver.process.returncode != 1 or 'Spout named mutex was abandoned' not in diagnostic:
                raise RuntimeError(f'Abandoned producer did not fail for its actual mutex: {diagnostic}')
            receiver.expected_failure = True
            dead_status = {'state': 'abandoned_mutex', 'diagnostic': diagnostic}
        else:
            # If sender metadata vanishes first, disconnected/not-ready is also
            # valid; it must not report old content as a successful new frame.
            if abandoned.get('connected') or abandoned.get('ready'):
                raise RuntimeError(f'Dead producer remained ready: {abandoned}')
            diagnostic = None
            dead_status = {'state': 'sender_metadata_removed', 'response': abandoned}
        # Restart the isolated pair after closing any surviving old receiver.
        if receiver.process.poll() is None:
            receiver.stop()
        started = time.monotonic()
        sender = Actor(sender_exe, 'sender', name); actors.append(sender)
        receiver = Actor(receiver_exe, 'receiver', name); actors.append(receiver)
        if sender.process.pid == producer_pid:
            raise RuntimeError('Producer restart reused a process identity')
        sender.command(op='publish', side=256, marker=208, format='BGRA8')
        recovered = wait_frame(receiver, 256, 208, 'BGRA8')
        report.update({'result': 'passed', 'initialFrame': initial, 'absentConsumerPublications': absent,
                       'consumerRestartFrame': reconnected, 'consumerRestartMilliseconds': reconnect_ms,
                       'deadProducerStatus': dead_status, 'restartFrame': recovered,
                       'pairRestartMilliseconds': (time.monotonic() - started) * 1000,
                       'survivingControl': sender.command(op='stats')})
    except Exception as error:
        report['result'] = 'failed'; report['error'] = str(error)
        raise
    finally:
        cleanup_errors = []
        report['actors'] = [actor.info for actor in actors]
        for index, actor in enumerate(reversed(actors)):
            try:
                if not (getattr(actor, 'intentionally_terminated', False) or getattr(actor, 'expected_failure', False)):
                    actor.stop()
            except Exception as error:
                cleanup_errors.append(str(error))
            (args.report_directory / f'{direction}-{index}-transcript.json').write_text(
                json.dumps({'info': actor.info, 'exitCode': actor.process.returncode,
                            'intentionallyTerminated': getattr(actor, 'intentionally_terminated', False),
                            'expectedFailure': getattr(actor, 'expected_failure', False),
                            'stderr': list(actor.errors), 'traffic': actor.log}, indent=2) + '\n', encoding='utf-8')
        (args.report_directory / 'worker-failure.json').write_text(json.dumps({
            'schemaVersion': 1, 'collectedUtc': datetime.now(timezone.utc).isoformat(),
            'evidenceClass': 'native-gpu-owned-spout-process-faults', 'reports': reports,
            'cleanupErrors': cleanup_errors, 'retryPolicy': 'one bounded fixture restart per injected fault',
            'deviceLoss': 'not_injected', 'physicalOutputRecovery': 'not_tested',
            'productionSupervisor': 'not_implemented'}, indent=2) + '\n', encoding='utf-8')
        if cleanup_errors:
            raise RuntimeError(f'Owned worker cleanup failed: {cleanup_errors}')
print(json.dumps({'result': 'passed', 'directions': len(reports), 'injectedOwnedProcessDeaths': 4}, indent=2))
