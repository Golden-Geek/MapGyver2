#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real engine/independent SDK client IPC, failure, framing and access contracts."""
import argparse
import copy
import json
from pathlib import Path
import queue
import struct
import subprocess
import threading
import time
import uuid

from pipe_client import Pipe, acl_current_user_only, handle_count, first_instance_probe

parser = argparse.ArgumentParser()
parser.add_argument('--engine', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
args.report.parent.mkdir(parents=True, exist_ok=True)
phases = []

def until(check, timeout=7):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        value = check()
        if value: return value
        time.sleep(.01)
    raise AssertionError('Expected native state was not observed before its test deadline')

class Server:
    def __init__(self, name):
        self.name = name; self.stopped = False
        self.stderr_path = args.report.with_name(args.report.stem+'-'+uuid.uuid4().hex[:8]+'.stderr.log')
        self.stderr = self.stderr_path.open('wb')
        self.process = subprocess.Popen([str(args.engine), '--serve', '--pipe-name', name], stdout=subprocess.PIPE,
            stderr=self.stderr, text=True, encoding='utf-8', creationflags=subprocess.CREATE_NO_WINDOW)
        lines = queue.Queue()
        def read():
            for line in self.process.stdout: lines.put(line)
            lines.put(None)
        self.reader = threading.Thread(target=read, daemon=True); self.reader.start()
        try:
            line = lines.get(timeout=10)
            assert line is not None, self.logs()
            self.ready = json.loads(line)
            assert self.ready['event'] == 'control_ready' and self.ready['transport'] == 'windows_named_pipe', self.ready
            assert self.ready['endpoint'] == rf'\\.\pipe\MapGyver.{name}', self.ready
        except BaseException:
            self.stop(); raise

    def logs(self): return self.stderr_path.read_text(encoding='utf-8')
    def stop(self):
        if self.stopped: return
        self.stopped = True
        unexpected = self.process.poll()
        if unexpected is None: self.process.terminate()
        self.process.wait(timeout=10)
        self.reader.join(timeout=2)
        self.process.stdout.close(); self.stderr.close()
        assert not self.reader.is_alive(), 'Owned server stdout reader did not complete'
        assert unexpected is None, ('Owned server exited before the explicit test stop', unexpected, self.logs())

class Client:
    def __init__(self, server, name, edit=True, resume=None):
        self.pipe = Pipe(server.ready['endpoint']); self.name = name
        self.pipe.send({'protocolVersion': 1, 'clientId': name, 'resumeSessionId': resume, 'requestEdit': edit}, fragments=True)
        self.hello = self.pipe.receive()
        assert self.hello['type'] == 'hello', self.hello
        self.sequence = int(self.hello['nextSequence'])

    def message(self, operation, payload=None, revision=None):
        return {'protocolVersion': 1, 'serviceId': self.hello['serviceId'], 'clientId': self.name,
            'sessionId': self.hello['sessionId'], 'requestId': f'request-{self.sequence}', 'sequence': str(self.sequence),
            'expectedRevision': None if revision is None else str(revision), 'applyAt': None,
            'operation': operation, 'payload': {} if payload is None else payload}

    def call(self, operation, payload=None, revision=None):
        message = self.message(operation, payload, revision); self.pipe.send(message)
        reply = self.pipe.reply(message['requestId'])
        if reply['consumed']: self.sequence += 1
        return reply

    def snapshot(self):
        reply = self.call('snapshot'); accepted(reply); return reply['payload']
    def close(self): self.pipe.close()

def accepted(reply, revision=None):
    assert reply['status'] == 'accepted' and reply['consumed'], reply
    if revision is not None: assert reply['authoredRevision'] == str(revision), reply
    assert not reply['renderer']['available'] and reply['renderer']['appliedRevision'] is None and reply['renderer']['frame'] is None, reply
    assert not reply['persistence']['available'] and reply['persistence']['savedRevision'] is None, reply

def rejected(reply, code, consumed):
    assert reply['status'] == 'rejected' and reply['error']['code'] == code and reply['consumed'] is consumed, reply

def write(address, type_name, value):
    return {'schemaVersion': 1, 'write': {'address': address, 'value': {'type': type_name, 'value': value}}}

def expect_closed(pipe):
    try: pipe.receive()
    except (OSError, EOFError): return
    raise AssertionError('Native peer unexpectedly completed a frame after its rejection/deadline')

def run():
    name = 'qualification-'+uuid.uuid4().hex
    server = Server(name); clients = []; raw = []
    try:
        diagnostic = subprocess.run([str(args.engine), '--diagnose', '--json', '--no-gpu'], capture_output=True, text=True, timeout=10)
        assert diagnostic.returncode == 0, diagnostic.stderr
        build = json.loads(diagnostic.stdout)['build']
        assert server.ready['sourceRevision'] == build['revision'] and server.ready['sourceDirty'] == build['dirty']
        owner = Client(server, 'editor'); viewer = Client(server, 'viewer', edit=False); clients += [owner, viewer]
        phases.append({'name': 'real-process-handshake-fragmented-frames', 'status': 'passed', 'serverPid': server.process.pid})
        phases.append({'name': 'native-DACL-readback', 'status': 'passed', 'actualAcl': acl_current_user_only(owner.pipe),
                       'remotePolicy': server.ready['remoteClients'], 'remoteProbe': 'not-performed'})
        assert owner.hello['snapshot']['isEditor'] and not viewer.hello['snapshot']['isEditor']
        initial = owner.hello['snapshot']['data']['project']; patches = copy.deepcopy(initial['stage']['patches'])
        surface = next(iter(initial['stage']['surfaces'])); address = f'/stage/surfaces/{surface}/name'
        lost = owner.message('parameters', {'writes': [write(address, 'text', 'Renamed over native pipe')]}, 0)
        owner.pipe.send(lost, fragments=True)  # Intentionally do not read its acknowledgement.
        observed = until(lambda: (value if (value := viewer.snapshot())['authoredRevision'] == '1' else None))
        assert observed['data']['project']['stage']['surfaces'][surface]['name'] == 'Renamed over native pipe'
        owner.close(); until(lambda: viewer.snapshot()['editOwner'] is None)
        resumed = Client(server, 'editor', resume=owner.hello['sessionId']); clients.append(resumed)
        assert resumed.hello['nextSequence'] == '1'
        resumed.pipe.send(lost); replay = resumed.pipe.reply(lost['requestId']); accepted(replay, 1)
        assert replay['replayed'] and resumed.snapshot()['authoredRevision'] == '1'
        phases.append({'name': 'lost-ack-disconnect-resume-exact-replay', 'status': 'passed'})
        rejected(resumed.call('parameters', {'writes': [write(address, 'text', 'Stale')]}, 0), 'revision_conflict', True)
        rejected(viewer.call('undo', revision=1), 'not_editor', True)
        root = initial['placements'][initial['rootPlacement']]['source']['instance']
        rejected(resumed.call('parameters', {'writes': [write(f'/show/sources/{root}/ownerReferences', 'unsignedInteger', '999')]}, 1), 'read_only', True)
        phases.append({'name': 'real-client-revision-owner-readonly-parity', 'status': 'passed'})
        subscribe = viewer.call('subscribe', {'afterEvent': None}); accepted(subscribe, 1)
        assert subscribe['payload']['resyncRequired'] and subscribe['payload']['snapshot']['data']['project']['stage']['patches'] == patches
        accepted(resumed.call('parameters', {'writes': [write(address, 'text', 'Second edit')]}, 1), 2)
        notification = viewer.pipe.receive()
        assert notification['type'] == 'notification' and any(e['kind'] == 'authoredChanged' and e['authoredRevision'] == '2' for e in notification['payload']['events']), notification
        phases.append({'name': 'native-push-subscription-and-calibration-preservation', 'status': 'passed'})
        resumed.pipe.write(struct.pack('<I', 13)+b'{"a":1,"a":2}')
        rejected(resumed.pipe.receive(), 'malformed', False)
        accepted(resumed.call('snapshot'), 2)
        for length in (0, server.ready['requestBytes']+1):
            peer = Pipe(server.ready['endpoint']); raw.append(peer); peer.write(struct.pack('<I', length)); expect_closed(peer); peer.close()
        phases.append({'name': 'malformed-JSON-and-invalid-native-frame-lengths', 'status': 'passed'})
        partial_header = Pipe(server.ready['endpoint']); raw.append(partial_header); partial_header.write(b'\x20')
        partial_body = Pipe(server.ready['endpoint']); raw.append(partial_body); partial_body.write(struct.pack('<I', 30)+b'{}')
        start = time.monotonic(); accepted(resumed.call('snapshot'), 2)
        assert time.monotonic()-start < 2, 'Partial peers blocked a healthy connection'
        expect_closed(partial_header); expect_closed(partial_body); partial_header.close(); partial_body.close()
        phases.append({'name': 'partial-header-body-deadlines-healthy-client-progress', 'status': 'passed'})
        before = handle_count(server.process.pid)
        for _ in range(20):
            peer = Pipe(server.ready['endpoint']); raw.append(peer)
            peer.send({'protocolVersion': 2, 'clientId': 'invalid', 'resumeSessionId': None, 'requestEdit': False})
            assert peer.receive()['type'] == 'hello_error'; peer.close()
        until(lambda: handle_count(server.process.pid) == before, timeout=3)
        phases.append({'name': 'native-disconnect-recycle-handle-count', 'status': 'passed', 'before': before, 'after': handle_count(server.process.pid), 'recycledPeers': 20})
        state = resumed.snapshot(); definitions = state['data']['project']['definitions']
        definitions['assets']['large'] = {'id': 'large', 'name': 'Large configured source', 'source': {'type': 'ExternalSource', 'value': {
            'adapter': 'future.file', 'locator': 'x'*120000, 'outputs': [{'id': 'picture', 'name': 'Picture', 'kind': 'video'}]}}}
        command = {'schemaVersion': 1, 'command': {'type': 'ReplaceDefinitions', 'value': {'value': definitions}}}
        accepted(resumed.call('transaction', {'commands': [command]}, 2), 3)
        slow = Pipe(server.ready['endpoint']); raw.append(slow)
        slow.send({'protocolVersion': 1, 'clientId': 'stalled-reader', 'resumeSessionId': None, 'requestEdit': False})
        # No reads: the >64 KiB hello exhausts the actual pipe output buffer.
        start = time.monotonic(); current = resumed.snapshot()
        assert current['authoredRevision'] == '3' and current['data']['project']['stage']['patches'] == patches
        assert time.monotonic()-start < 2, 'Stalled writer blocked healthy client progress'
        until(lambda: '"reason":"write_timeout"' in server.logs(), timeout=5)
        expect_closed(slow); slow.close()
        phases.append({'name': 'real-output-buffer-backpressure-write-deadline', 'status': 'passed', 'configuredWriteTimeoutMs': server.ready['writeTimeoutMs']})
        stalled = Client(server, 'stalled-response', edit=False); clients.append(stalled)
        pending_reply = stalled.message('snapshot'); previous_timeouts = server.logs().count('"reason":"write_timeout"')
        stalled.pipe.send(pending_reply)  # Accepted read; deliberately leave the large reply unread.
        until(lambda: server.logs().count('"reason":"write_timeout"') > previous_timeouts, timeout=5)
        expect_closed(stalled.pipe); stalled.close()
        recovered = Client(server, 'stalled-response', edit=False, resume=stalled.hello['sessionId']); clients.append(recovered)
        assert recovered.hello['nextSequence'] == '1'
        recovered.pipe.send(pending_reply); retained = recovered.pipe.reply(pending_reply['requestId']); accepted(retained, 3)
        assert retained['replayed'] and retained['payload']['data']['project']['stage']['patches'] == patches
        phases.append({'name': 'cancelled-large-reply-resume-preserves-terminal-cache', 'status': 'passed'})
        probe = first_instance_probe(server.ready['endpoint']+'-flag-probe')
        collision = subprocess.run([str(args.engine), '--serve', '--pipe-name', name], capture_output=True, text=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        expected_errors = {5, probe['fullPoolCollision']}
        assert collision.returncode == 1 and any(f'CreateNamedPipe failed, Win32={code}\n' in collision.stderr for code in expected_errors) and not collision.stdout, collision
        assert resumed.snapshot()['authoredRevision'] == '3'
        phases.append({'name': 'first-instance-collision-does-not-hijack-live-server', 'status': 'passed', 'nativeFlagProbe': probe, 'engineError': collision.stderr.strip()})
        old_request = resumed.message('snapshot'); old_id = resumed.hello['serviceId']
        for client in clients: client.close()
        server.stop(); first_ready = server.ready; first_logs = server.logs()
        server = Server(name)
        fresh = Client(server, 'editor'); clients.append(fresh)
        assert fresh.hello['serviceId'] != old_id and fresh.hello['snapshot']['authoredRevision'] == '0'
        fresh.pipe.send(old_request); rejected(fresh.pipe.reply(old_request['requestId']), 'service_mismatch', False)
        accepted(fresh.call('snapshot'), 0)
        phases.append({'name': 'owned-server-forced-restart-old-identity-rejected', 'status': 'passed', 'persistenceClaim': 'none; new default project as reported by unavailable persistence'})
        assert 'Fatal:' not in first_logs and 'frame_timeout' in first_logs and 'invalid_frame_length' in first_logs
        return {'schemaVersion': 1, 'evidenceClass': 'native-Windows-named-pipe-separate-process-integration',
                'ready': first_ready, 'phases': phases, 'scope': 'Real Windows SDK IO and independent Python clients; no GPU/rendered-frame/durable-save/remote-SMB qualification'}
    finally:
        for client in clients: client.close()
        for peer in raw: peer.close()
        server.stop()

if __name__ == '__main__':
    report = run(); args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'result': 'passed', 'phases': len(report['phases']), 'report': str(args.report)}))
