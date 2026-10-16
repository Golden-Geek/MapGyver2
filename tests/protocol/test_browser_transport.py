#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual capability-bound reliable WebSocket control, independent native clients."""
import argparse
import copy
import json
from pathlib import Path
from http.client import HTTPConnection
import queue
import subprocess
import tempfile
import threading
import uuid
from test_network import WebSocket, until, drain_closed


class Client:
    def __init__(self, port, capability, name, edit=True, resume=None, origin=None):
        self.ws = WebSocket(port, path='/control', capability=capability, origin=origin)
        self.name = name
        self.ws.message({'protocolVersion': 1, 'clientId': name, 'resumeSessionId': resume, 'requestEdit': edit})
        self.hello = self.read()
        assert self.hello['type'] == 'hello', self.hello
        self.sequence = int(self.hello['nextSequence'])

    def read(self):
        opcode, body = self.ws.receive()
        assert opcode == 1, (opcode, body)
        return json.loads(body)

    def message(self, operation, payload=None, revision=None):
        return {'protocolVersion': 1, 'serviceId': self.hello['serviceId'], 'clientId': self.name,
                'sessionId': self.hello['sessionId'], 'requestId': f'request-{self.sequence}', 'sequence': str(self.sequence),
                'expectedRevision': revision, 'applyAt': None, 'operation': operation, 'payload': payload or {}}

    def call(self, operation, payload=None, revision=None):
        message = self.message(operation, payload, revision)
        self.ws.message(message)
        reply = self.reply(message['requestId'])
        if reply['consumed']:
            self.sequence += 1
        return reply

    def reply(self, request):
        for _ in range(64):
            reply = self.read()
            if reply['type'] == 'reply':
                assert reply['requestId'] == request, reply
                return reply
            assert reply['type'] == 'notification'
        raise AssertionError('Reply starved by notifications')

    def snapshot(self):
        reply = self.call('snapshot')
        assert reply['status'] == 'accepted', reply
        return reply['payload']

    def close(self):
        self.ws.close()


def write(address, value):
    return {'schemaVersion': 1, 'write': {'address': address, 'value': {'type': 'text', 'value': value}}}


def run(args):
    root = Path(__file__).resolve().parents[2]
    workspace = root/'out/qualification/browser-assets'
    workspace.mkdir(parents=True, exist_ok=True)
    phases = []
    with tempfile.TemporaryDirectory(prefix='utf8-\u00e9\u6f22-', dir=workspace) as directory:
        asset_dir = Path(directory).resolve()
        assert asset_dir.is_relative_to(workspace.resolve())
        original = '<!doctype html><title>Native assets</title><p>Static caf\u00e9</p>'
        (asset_dir/'index.html').write_text(original, encoding='utf-8')
        dev_origin = 'http://127.0.0.1:5173'
        command = [str(args.engine), '--serve', '--pipe-name', 'browser-'+uuid.uuid4().hex, '--oscquery',
                   '--editor-dir', str(asset_dir), '--dev-origin', dev_origin] if args.engine else [str(args.fixture), str(asset_dir), '--browser-mode', dev_origin]
        args.report.parent.mkdir(parents=True, exist_ok=True)
        clients = []
        with args.report.with_suffix('.stderr.log').open('wb') as stderr:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr,
                text=True, encoding='utf-8', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            lines = queue.Queue()
            def read():
                for line in process.stdout:
                    lines.put(line)
                lines.put(None)
            reader = threading.Thread(target=read, daemon=True)
            reader.start()
            try:
                line = lines.get(timeout=10)
                assert line is not None, 'Native server exited before ready'
                ready = json.loads(line)
                endpoint = ready['oscquery'] if args.engine else ready
                port, capability = endpoint['httpPort'], ready['editor']['capability']
                origin = f'http://127.0.0.1:{port}'
                def http(path):
                    connection = HTTPConnection('127.0.0.1', port, timeout=8)
                    try:
                        connection.request('GET', path)
                        response = connection.getresponse()
                        return response.status, response.read().decode('utf-8')
                    finally:
                        connection.close()
                for supplied, source in [(None, origin), ('0'*64, origin), (capability, 'https://attacker.invalid'), (capability, 'null')]:
                    denied = WebSocket(port, path='/control', capability=supplied, origin=source, expected_status=403)
                    assert capability.encode() not in denied.response
                    denied.close()
                assert http('/editor/')[1] == original
                (asset_dir/'index.html').write_text('Changed on disk', encoding='utf-8')
                assert http('/editor/')[1] == original, 'Static serving reread disk after startup'
                assert http('/editor/../LICENSE')[0] == 404 and http('/editor/%2e%2e/LICENSE')[0] == 404
                assert capability not in http('/?HOST_INFO')[1] and capability not in http('/editor/')[1]
                preview_status, preview = http('/preview')
                assert preview_status == 503 and json.loads(preview)['frame'] is None
                phases.append({'name': 'OS-capability-Origin-denial-private-bootstrap-UTF8-preloaded-static-assets', 'status': 'passed'})
                owner = Client(port, capability, 'web-owner', origin=origin)
                viewer = Client(port, capability, 'web-viewer', edit=False, origin=dev_origin)
                clients += [owner, viewer]
                assert owner.hello['snapshot']['isEditor'] and not viewer.hello['snapshot']['isEditor']
                project = owner.hello['snapshot']['data']['project']
                patches = copy.deepcopy(project['stage']['patches'])
                surface = next(iter(project['stage']['surfaces']))
                address = f'/stage/surfaces/{surface}/name'
                lost = owner.message('parameters', {'writes': [write(address, 'Lost ACK caf\u00e9')]}, '0')
                owner.ws.message(lost)  # Actual client deliberately does not read the terminal response.
                until(lambda: viewer.snapshot()['authoredRevision'] == '1')
                owner.close()
                until(lambda: viewer.snapshot()['editOwner'] is None)
                resumed = Client(port, capability, 'web-owner', resume=owner.hello['sessionId'], origin=origin)
                clients.append(resumed)
                resumed.ws.message(lost)
                reply = resumed.reply(lost['requestId'])
                assert reply['status'] == 'accepted' and reply['replayed'] and reply['authoredRevision'] == '1', reply
                assert reply['renderer']['appliedRevision'] is None and reply['persistence']['savedRevision'] is None
                assert resumed.snapshot()['data']['project']['stage']['surfaces'][surface]['name'] == 'Lost ACK caf\u00e9'
                phases.append({'name': 'real-WebSocket-lost-ACK-resume-exact-terminal-replay', 'status': 'passed'})
                denied = viewer.call('parameters', {'writes': [write(address, 'Denied')]}, '1')
                assert denied['status'] == 'rejected' and denied['error']['code'] == 'not_editor'
                stale = resumed.call('parameters', {'writes': [write(address, 'Stale')]}, '0')
                assert stale['status'] == 'rejected' and stale['error']['code'] == 'revision_conflict'
                root_instance = project['placements'][project['rootPlacement']]['source']['instance']
                readonly = {'schemaVersion': 1, 'write': {'address': f'/show/sources/{root_instance}/ownerReferences', 'value': {'type': 'unsignedInteger', 'value': '999'}}}
                invalid = resumed.call('parameters', {'writes': [write(address, 'Roll back'), readonly]}, '1')
                assert invalid['status'] == 'rejected' and invalid['error']['code'] == 'read_only'
                assert resumed.snapshot()['authoredRevision'] == '1'
                saved = resumed.call('save', {}, '1')
                assert saved['status'] == 'rejected' and saved['error']['code'] == 'unsupported_operation'
                phases.append({'name': 'same-owner-revision-readonly-atomic-validator-unavailable-save', 'status': 'passed'})
                subscribed = viewer.call('subscribe', {'afterEvent': None})
                assert subscribed['status'] == 'accepted'
                placement = project['placements'][project['rootPlacement']]['source']
                asset = project['sources'][placement['instance']]['asset']
                outputs = project['definitions']['compositions'][project['definitions']['assets'][asset]['source']['value']['composition']]['outputs']
                command = {'schemaVersion': 1, 'command': {'type': 'PlaceSource', 'value': {'source': {'asset': asset, 'port': outputs[0]['id']}, 'keepAlive': False}}}
                created = resumed.call('transaction', {'commands': [command]}, '1')
                assert created['status'] == 'accepted' and created['authoredRevision'] == '2'
                event = viewer.read()
                assert event['type'] == 'notification' and any(e['kind'] == 'authoredChanged' for e in event['payload']['events'])
                current = resumed.snapshot()
                added = set(current['data']['project']['placements'])-set(project['placements'])
                assert len(added) == 1 and current['data']['project']['stage']['patches'] == patches
                command = {'schemaVersion': 1, 'command': {'type': 'RemovePlacement', 'value': {'placement': added.pop()}}}
                removed = resumed.call('transaction', {'commands': [command]}, '2')
                assert removed['status'] == 'accepted' and len(resumed.snapshot()['data']['project']['sources']) == len(project['sources'])
                viewer.close()
                viewer = Client(port, capability, 'web-viewer', edit=False, resume=viewer.hello['sessionId'])
                clients.append(viewer)
                subscribed = viewer.call('subscribe', {'afterEvent': None})
                assert subscribed['payload']['resyncRequired'] and subscribed['payload']['snapshot']['authoredRevision'] == '3'
                phases.append({'name': 'actual-structural-create-delete-notifications-reconnect-resubscription', 'status': 'passed'})
                viewer.ws.frame(1, b'{"x":1,"x":2}')
                malformed = viewer.read()
                assert malformed['type'] == 'reply' and not malformed['consumed'] and malformed['error']['code'] == 'malformed'
                assert viewer.snapshot()['authoredRevision'] == '3'
                binary = Client(port, capability, 'binary-refusal', edit=False)
                clients.append(binary)
                binary.ws.frame(2, b'not-a-control-text-envelope')
                assert drain_closed(binary.ws.socket)[0] == 0
                phases.append({'name': 'malformed-duplicate-JSON-and-binary-control-refusal-isolation', 'status': 'passed'})
                partial = WebSocket(port, path='/control', capability=capability, origin=origin)
                clients.append(partial)
                assert drain_closed(partial.socket)[0] == 0
                assert viewer.snapshot()['authoredRevision'] == '3'
                phases.append({'name': 'actual-control-hello-deadline-healthy-peer-progress', 'status': 'passed'})
                args.report.write_text(json.dumps({'schemaVersion': 1, 'sourceRevision': ready['sourceRevision'],
                    'sourceDirty': ready['sourceDirty'], 'evidenceClass': 'native-capability-bound-WebSocket-independent-stdlib-clients',
                    'phases': phases, 'scope': 'Loopback native socket and real protocol/ownership validation. No remote TLS, rendering or durable-save claim. Bootstrap capability omitted.'}, indent=2)+'\n')
                print(f'Passed {len(phases)} actual browser-channel native phases')
            finally:
                unexpected = process.poll()
                process.stdin.close()
                if args.engine and unexpected is None:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    raise AssertionError('Owned fixture did not stop')
                reader.join(timeout=2)
                process.stdout.close()
                for client in reversed(clients):
                    client.close()
                assert unexpected is None and not reader.is_alive()
                if not args.engine:
                    assert process.returncode == 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument('--engine', type=Path)
    choice.add_argument('--fixture', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    run(parser.parse_args())
