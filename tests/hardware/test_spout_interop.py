#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real cross-version Spout processes, pixel identity, resize/restart and stalls."""
import argparse
from collections import deque
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import queue
import struct
import subprocess
import threading
import time
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--participant', required=True, type=Path)
parser.add_argument('--reference', required=True, type=Path)
parser.add_argument('--report-directory', required=True, type=Path)
args = parser.parse_args()
args.report_directory.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'tests/hardware/spout-reference.json').read_text())
prepared = root / 'out/qualification' / f'spout-peer-{manifest["sdkVersion"]}' / 'inputs.json'
reference_inputs = json.loads(prepared.read_text())
reference_dll_hash = next(item['sha256'] for item in reference_inputs['files'] if item['file'] == 'MD/bin/SpoutDX.dll')


class Actor:
    def __init__(self, executable, role, name):
        self.lines = queue.Queue(maxsize=8)
        self.errors = deque(maxlen=64)
        self.reader_error = None
        self.log = []
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
        self.process = subprocess.Popen([str(executable.resolve()), '--role', role, '--name', name],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        text=True, encoding='utf-8', startupinfo=startup,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
        def output():
            try:
                for line in self.process.stdout:
                    self.lines.put_nowait(line)
            except Exception as error:
                self.reader_error = str(error)
            finally:
                try:
                    self.lines.put_nowait(None)
                except queue.Full:
                    self.reader_error = 'Output queue capacity exceeded'
        def errors():
            for line in self.process.stderr:
                self.errors.append(line[:1024])
        self.output_thread = threading.Thread(target=output, daemon=True)
        self.error_thread = threading.Thread(target=errors, daemon=True)
        self.output_thread.start(); self.error_thread.start()
        try:
            self.info = self.read(timeout=10)
            if self.info.get('event') != 'ready' or self.info['role'] != role or self.info['name'] != name:
                raise RuntimeError(f'Unexpected participant startup: {self.info}')
            if self.info['adapter']['software'] or self.info['sdkFrameCountEnabled']:
                raise RuntimeError('Wrong hardware/frame-count profile')
            self.dll_hash = hashlib.sha256(Path(self.info['sdkModulePath']).read_bytes()).hexdigest()
            self.info['sdkModuleSha256'] = self.dll_hash
            if self.info['sdkVersion'] == manifest['sdkVersion'] and self.dll_hash != reference_dll_hash:
                raise RuntimeError('Reference peer loaded a different SDK DLL')
        except Exception:
            self.process.kill(); self.process.wait(timeout=5)
            raise

    def read(self, timeout=6):
        try:
            line = self.lines.get(timeout=timeout)
        except queue.Empty:
            raise RuntimeError(f'Participant response timeout, PID={self.process.pid}, stderr={"".join(self.errors)}')
        if line is None or self.reader_error:
            raise RuntimeError(f'Participant exited/overflowed: {self.process.poll()}, {self.reader_error}, {"".join(self.errors)}')
        response = json.loads(line)
        self.log.append({'response': response})
        return response

    def command(self, **command):
        if len(self.log) >= 512:
            raise RuntimeError('Fixture command/report capacity exceeded')
        self.log.append({'command': command})
        self.process.stdin.write(json.dumps(command) + '\n')
        self.process.stdin.flush()
        return self.read()

    def stop(self):
        if self.process.poll() is None:
            try:
                result = self.command(op='quit')
                if result != {'stopped': True}:
                    raise RuntimeError(f'Unexpected shutdown acknowledgement: {result}')
                self.process.wait(timeout=5)
            finally:
                if self.process.poll() is None:
                    self.process.kill(); self.process.wait(timeout=5)
        self.output_thread.join(timeout=1); self.error_thread.join(timeout=1)
        if self.process.returncode != 0 or self.errors or self.reader_error:
            raise RuntimeError(f'Participant failed: {self.process.returncode}, {self.reader_error}, {"".join(self.errors)}')


def expected_hash(side, marker):
    value = 2166136261
    for y in range(side):
        for x in range(side):
            word = (marker ^ ((x * 0x9e3779b9) & 0xffffffff) ^ ((y * 0x85ebca6b) & 0xffffffff)) & 0xffffffff
            for byte in struct.pack('<I', word):
                value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def wait_frame(actor, side, marker, fmt):
    deadline = time.monotonic() + 8
    last = None
    while time.monotonic() < deadline:
        last = actor.command(op='poll')
        if last.get('ready') and last.get('marker') == marker:
            if last['side'] != side or last['format'] != fmt or last['pixelHash'] != expected_hash(side, marker):
                raise RuntimeError(f'Frame pixels/geometry/format do not match independent Python reference: {last}')
            return last
        time.sleep(0.01)
    raise RuntimeError(f'Expected frame did not arrive: marker={marker}, last={last}')


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
