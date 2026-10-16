#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent stdlib HTTP/OSC/RFC6455 clients against actual native sockets."""
import argparse
import base64
import copy
import hashlib
from http.client import HTTPConnection
import json
from pathlib import Path
import queue
import socket
import struct
import subprocess
import threading
import time
import uuid


def until(check, timeout=8):
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(.01)
    raise AssertionError('Expected network state not observed before deadline')


def osc_string(value):
    raw = value.encode('utf-8')+b'\0'
    return raw+b'\0'*((-len(raw)) % 4)


def osc(address, value, tag='s'):
    payload = osc_string(value) if tag == 's' else struct.pack('>q', value)
    return osc_string(address)+osc_string(','+tag)+payload


def bundle(elements, timetag=1):
    return b'#bundle\0'+struct.pack('>Q', timetag)+b''.join(struct.pack('>I', len(e))+e for e in elements)


def drain_closed(stream):
    drained = 0
    while True:
        try:
            chunk = stream.recv(65536)
        except ConnectionResetError:
            return drained, 'native_connection_reset'
        if not chunk:
            return drained, 'native_eof'
        drained += len(chunk)
        assert drained <= 4194304


class WebSocket:
    def __init__(self, port, small=False, origin=None, path='/', capability=None, expected_status=101):
        self.socket = socket.socket()
        if small:
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        self.socket.settimeout(8)
        self.socket.connect(('127.0.0.1', port))
        key = base64.b64encode(uuid.uuid4().bytes).decode('ascii')
        request = (f'GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n'
                   f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n')
        if origin:
            request += 'Origin: '+origin+'\r\n'
        if capability is not None:
            request += 'Sec-WebSocket-Protocol: mapgyver.control.v1, token.'+capability+'\r\n'
        self.socket.sendall((request+'\r\n').encode('ascii'))
        response = bytearray()
        while not response.endswith(b'\r\n\r\n'):
            response.extend(self.exact(1))
            assert len(response) <= 16384
        assert response.startswith(f'HTTP/1.1 {expected_status}'.encode()), response
        self.response = bytes(response)
        if expected_status != 101:
            return
        accept = base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        headers = {key.lower(): value.strip() for line in bytes(response).split(b'\r\n')[1:] if b':' in line
                   for key, value in [line.split(b':', 1)]}
        assert headers[b'sec-websocket-accept'] == accept, response
        if path == '/control':
            assert headers[b'sec-websocket-protocol'] == b'mapgyver.control.v1'

    def exact(self, size):
        data = bytearray()
        while len(data) < size:
            part = self.socket.recv(size-len(data))
            if not part:
                raise EOFError('WebSocket closed')
            data.extend(part)
        return bytes(data)

    def frame(self, opcode, body=b'', final=True, masked=True):
        first = (128 if final else 0) | opcode
        length = len(body)
        header = bytes([first, (128 if masked else 0) | (length if length < 126 else 126 if length <= 65535 else 127)])
        if length >= 126:
            header += struct.pack('>H' if length <= 65535 else '>Q', length)
        if masked:
            mask = b'\x12\x37\xa1\x5c'
            header += mask
            body = bytes(value ^ mask[index % 4] for index, value in enumerate(body))
        self.socket.sendall(header+body)

    def message(self, value):
        self.frame(1, json.dumps(value).encode())

    def receive(self):
        opcode, result = None, bytearray()
        while True:
            first, second = self.exact(2)
            assert not second & 128, 'Server frames must not be masked'
            length = second & 127
            if length == 126:
                length = struct.unpack('>H', self.exact(2))[0]
            elif length == 127:
                length = struct.unpack('>Q', self.exact(8))[0]
            assert length <= 4194304
            body = self.exact(length)
            kind = first & 15
            if kind == 9:
                self.frame(10, body)
                continue
            if kind == 8:
                return kind, body
            if kind != 0:
                opcode = kind
            result.extend(body)
            assert len(result) <= 4194304
            if first & 128:
                return opcode, bytes(result)

    def barrier(self):
        self.frame(9, b'barrier')
        assert self.receive() == (10, b'barrier')

    def close(self):
        self.socket.close()


class PipeEditor:
    def __init__(self, ready):
        from pipe_client import Pipe
        self.pipe = Pipe(ready['endpoint'])
        self.pipe.send({'protocolVersion': 1, 'clientId': 'network-test', 'resumeSessionId': None, 'requestEdit': True})
        self.hello = self.pipe.receive()
        assert self.hello['type'] == 'hello' and self.hello['snapshot']['isEditor'], self.hello
        self.sequence = 0

    def call(self, operation, payload=None, revision=None):
        message = {'protocolVersion': 1, 'serviceId': self.hello['serviceId'], 'clientId': 'network-test',
                   'sessionId': self.hello['sessionId'], 'requestId': f'network-{self.sequence}',
                   'sequence': str(self.sequence), 'expectedRevision': revision, 'applyAt': None,
                   'operation': operation, 'payload': payload or {}}
        self.pipe.send(message)
        reply = self.pipe.reply(message['requestId'])
        if reply['consumed']:
            self.sequence += 1
        assert reply['status'] == 'accepted', reply
        return reply

    def close(self):
        self.pipe.close()


def run(args):
    phases, clients = [], []
    name = 'network-'+uuid.uuid4().hex
    command = ([str(args.engine), '--serve', '--pipe-name', name, '--oscquery'] if args.engine else [str(args.fixture)])
    stderr_path = args.report.with_suffix('.stderr.log')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with stderr_path.open('wb') as stderr:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr,
                                   text=True, encoding='utf-8', creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        lines = queue.Queue()
        def read_lines():
            for line in process.stdout:
                lines.put(line)
            lines.put(None)
        reader = threading.Thread(target=read_lines, daemon=True)
        reader.start()
        try:
            line = lines.get(timeout=10)
            assert line is not None, stderr_path.read_text()
            ready = json.loads(line)
            endpoint = ready['oscquery'] if args.engine else ready
            port = endpoint['httpPort']
            def http(path='/', headers=None, method='GET'):
                connection = HTTPConnection('127.0.0.1', port, timeout=8)
                try:
                    connection.request(method, path, headers=headers or {})
                    response = connection.getresponse()
                    body = response.read()
                    return response.status, json.loads(body) if body else None
                finally:
                    connection.close()
            def get(path):
                status, body = http(path)
                assert status == 200, (status, body)
                return body
            def info():
                return get('/ignored?HOST_INFO')['X_MAPGYVER']['ENDPOINT']
            def value(path):
                return get(path+'?VALUE')['VALUE'][0]
            root = get('/')
            methods = {}
            def collect(node):
                if 'TYPE' in node:
                    methods[node['FULL_PATH']] = node
                for child in node.get('CONTENTS', {}).values():
                    collect(child)
            collect(root)
            host = get('/nonexistent?HOST_INFO')
            assert host['OSC_PORT'] == endpoint['oscPort'] and host['WS_PORT'] == port
            assert host['EXTENSIONS']['LISTEN'] and host['EXTENSIONS']['PATH_CHANGED']
            assert http('/missing')[0] == 404 and http('/?unknown')[0] == 400
            assert http('/commands/history/undo?VALUE') == (204, None)
            assert http('/', {'Host': 'attacker.invalid'})[0] == 403
            assert http('/', {'Origin': 'https://attacker.invalid'})[0] == 403
            assert http('/', method='POST')[0] == 405
            address = next(path for path in methods if path.startswith('/stage/surfaces/') and path.endswith('/name'))
            original = value(address)
            datagram = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            clients.append(datagram)
            def send(packet):
                datagram.sendto(packet, ('127.0.0.1', endpoint['oscPort']))
            editor = None
            if args.engine:
                editor = PipeEditor(ready)
                clients.append(editor)
                registry = editor.call('registry')['payload']
                assert set(methods) == {p['address'] for p in registry['parameters']} | {a['address'] for a in registry['actions']}
                project = editor.hello['snapshot']['data']['project']
                before = info()['udpRejected']
                send(osc(address, 'Denied'))
                until(lambda: info()['udpRejected'] > before)
                assert value(address) == original
                # The authoritative model has a larger text range than OSC's
                # per-string capacity. Refusing a notification must isolate only
                # the subscriber, not reject an accepted model edit or kill IO.
                endpoint_path = next(path for path in methods if path.startswith('/outputs/') and path.endswith('/endpoint'))
                constrained = WebSocket(port)
                clients.append(constrained)
                constrained.message({'COMMAND': 'LISTEN', 'DATA': endpoint_path})
                constrained.barrier()
                counter = info()['notificationRejections']
                large_endpoint = 'endpoint-'+'x'*300000
                reply = editor.call('parameters', {'writes': [{'schemaVersion': 1, 'write': {'address': endpoint_path,
                    'value': {'type': 'text', 'value': large_endpoint}}}]}, info()['authoredRevision'])
                assert reply['renderer']['appliedRevision'] is None and reply['persistence']['savedRevision'] is None
                until(lambda: info()['notificationRejections'] == counter+1)
                assert drain_closed(constrained.socket)[0] == 0
                assert value(endpoint_path) == large_endpoint
                constrained.close()
                phases.append({'name': 'accepted-model-value-exceeding-OSC-capacity-isolates-subscriber', 'status': 'passed'})
                editor.call('transferEdit', {'expectedOwnershipGeneration': editor.hello['snapshot']['ownershipGeneration'],
                                            'targetSession': endpoint['oscSessionId']})
            else:
                assert ready['fixtureGrantsOsc']
                registry = ready['registry']
                assert set(methods) == {p['address'] for p in registry['parameters']} | {a['address'] for a in registry['actions']}
                project = ready['project']
            phases.append({'name': 'actual-HTTP-complete-tree-HOST_INFO-access-and-loopback-policy', 'status': 'passed', 'methods': len(methods)})
            send(osc(address, 'UDP caf\u00e9'))
            until(lambda: value(address) == 'UDP caf\u00e9')
            revision = info()['authoredRevision']
            for packet in [bundle([osc(address, 'Must roll back'), osc(address, 42, 'h')]),
                           bundle([osc(address, 'Future')], 2), b'/bad\0', osc(address, 'Bad padding')+b'\0']:
                before = info()['udpRejected']
                send(packet)
                until(lambda: info()['udpRejected'] > before)
                assert value(address) == 'UDP caf\u00e9' and info()['authoredRevision'] == revision
            phases.append({'name': 'actual-UDP-UTF8-whole-bundle-validation-and-unavailable-scheduling', 'status': 'passed'})
            listener, container, writer = WebSocket(port), WebSocket(port), WebSocket(port)
            clients += [listener, container, writer]
            listener.message({'COMMAND': 'LISTEN', 'DATA': address})
            listener.barrier()
            container.message({'COMMAND': 'LISTEN', 'DATA': address.rsplit('/', 1)[0]})
            container.barrier()
            packet = osc(address, 'Fragmented WebSocket')
            writer.frame(2, packet[:11], final=False)
            writer.frame(0, packet[11:])
            assert listener.receive() == (2, packet)
            container.barrier()  # A container LISTEN must not receive the descendant value.
            assert value(address) == 'Fragmented WebSocket'
            listener.message({'COMMAND': 'IGNORE', 'DATA': address})
            listener.barrier()
            writer.frame(2, osc(address, 'Ignored value'))
            until(lambda: value(address) == 'Ignored value')
            listener.barrier()
            listener.close()
            listener = WebSocket(port)
            clients.append(listener)
            writer.frame(2, osc(address, 'Reconnect needs LISTEN'))
            until(lambda: value(address) == 'Reconnect needs LISTEN')
            listener.barrier()
            listener.message({'COMMAND': 'LISTEN', 'DATA': address})
            listener.barrier()
            packet = osc(address, 'Resubscribed')
            writer.frame(2, packet)
            assert listener.receive() == (2, packet)
            phases.append({'name': 'RFC6455-fragments-exact-LISTEN-IGNORE-reconnect-binary-OSC', 'status': 'passed'})
            definitions = copy.deepcopy(project['definitions'])
            definitions['assets']['network-media'] = {'id': 'network-media', 'name': 'Configured unknown media',
                'source': {'type': 'ExternalSource', 'value': {'adapter': 'future.media', 'locator': 'configured',
                    'outputs': [{'id': 'picture', 'name': 'Picture', 'kind': 'video'}]}}}
            def action(path, command):
                payload = {'expectedRevision': info()['authoredRevision'], 'command': {'schemaVersion': 1, 'command': command}}
                packet = osc(path, json.dumps(payload, separators=(',', ':')))
                writer.frame(2, packet)
                return packet
            before = info()['authoredRevision']
            action('/commands/definitions/replace', {'type': 'ReplaceDefinitions', 'value': {'value': definitions}})
            until(lambda: info()['authoredRevision'] != before)
            for peer in [listener, container, writer]:
                kind, packet = peer.receive()
                assert kind == 1 and json.loads(packet) == {'COMMAND': 'PATH_CHANGED', 'DATA': '/'}
            locator = '/library/assets/network-media/locator'
            assert get(locator)['VALUE'] == ['configured']
            before = info()['authoredRevision']
            place = action('/commands/sources/place', {'type': 'PlaceSource', 'value': {'source': {'asset': 'network-media', 'port': 'picture'}, 'keepAlive': False}})
            until(lambda: info()['authoredRevision'] != before)
            assert json.loads(writer.receive()[1])['COMMAND'] == 'PATH_CHANGED'
            current = info()['authoredRevision']
            rejected = info()['udpRejected']
            send(place)
            until(lambda: info()['udpRejected'] > rejected)
            assert info()['authoredRevision'] == current, 'A stale structural packet executed twice'
            if editor:
                snapshot = editor.call('snapshot')['payload']
                assert snapshot['data']['project']['stage']['patches'] == project['stage']['patches']
                assert snapshot['data']['project']['definitions']['assets']['network-media']['source']['value']['adapter'] == 'future.media'
            phases.append({'name': 'real-structural-commands-PATH_CHANGED-and-preserved-configuration', 'status': 'passed'})
            # Close other listeners so this phase observes only its own stall.
            listener.close()
            container.close()
            slow = WebSocket(port, small=True)
            clients.append(slow)
            slow.message({'COMMAND': 'LISTEN', 'DATA': locator})
            slow.barrier()
            baseline = info()['websocketWriteTimeouts']
            # Socket autotuning differs across OSes. Fill real flow-control
            # capacity rather than assuming one 200 KB message must stall.
            # Sixteen 240 KB messages remain below the 4 MiB queue capacity even
            # if none complete. A whole-write timeout is still mandatory.
            sent = 0
            receive_buffer = slow.socket.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
            for index in range(16):
                if info()['websocketWriteTimeouts'] > baseline:
                    break
                large = str(index).zfill(4)+'x'*239996
                writer.frame(2, osc(locator, large))
                until(lambda: value(locator) == large)
                sent += len(osc(locator, large))
            until(lambda: info()['websocketWriteTimeouts'] > baseline, timeout=7)
            # Verify an actual socket EOF after draining the bounded kernel data.
            drained, close_class = drain_closed(slow.socket)
            assert drained < sent, 'All writes completed; this was not a stalled partial frame'
            writer.barrier()
            phases.append({'name': 'actual-kernel-write-stall-deadline-healthy-client-progress', 'status': 'passed', 'sentBytes': sent, 'drainedBytes': drained, 'closeClass': close_class, 'clientReceiveBufferBytes': receive_buffer, 'serverSendBufferBytes': info()['socketSendBufferObservedBytes']})
            partial = socket.create_connection(('127.0.0.1', port), timeout=8)
            clients.append(partial)
            partial.sendall(b'GET / HTTP/1.1\r\nHost: ')
            baseline = info()['httpReadTimeouts']
            until(lambda: info()['httpReadTimeouts'] > baseline, timeout=8)
            drained, close_class = drain_closed(partial)
            assert drained == 0, 'Partial HTTP request unexpectedly received a response'
            assert value(address) == 'Resubscribed'
            bad = WebSocket(port)
            clients.append(bad)
            bad.frame(1, b'{}', masked=False)
            try:
                kind, _ = bad.receive()
                assert kind == 8
            except (EOFError, ConnectionResetError):
                pass
            bad.close()
            oversized = WebSocket(port)
            clients.append(oversized)
            oversized.socket.sendall(b'\x82\xff'+struct.pack('>Q', endpoint['messageBytes']+1)+b'\x01\x02\x03\x04')
            kind, body = oversized.receive()
            assert kind == 8 and struct.unpack('>H', body[:2])[0] == 1009, (kind, body)
            oversized.close()
            assert get('/?HOST_INFO')['EXTENSIONS']['LISTEN']
            phases.append({'name': 'partial-HTTP-deadline-malformed-WebSocket-isolation', 'status': 'passed', 'closeClass': close_class})
            held = []
            for _ in range(endpoint['peerLimit']-1):
                peer = socket.create_connection(('127.0.0.1', port), timeout=8)
                clients.append(peer)
                held.append(peer)
                peer.sendall(b'GET / HTTP/1.1\r\n')
            writer.barrier()
            overflow = socket.create_connection(('127.0.0.1', port), timeout=8)
            clients.append(overflow)
            assert drain_closed(overflow)[0] == 0, 'Connection beyond the peer capacity was admitted'
            writer.barrier()
            for peer in held:
                peer.close()
            writer.barrier()
            assert info()['peerLimit'] == endpoint['peerLimit']
            phases.append({'name': 'actual-peer-capacity-refusal-existing-client-progress-recovery', 'status': 'passed', 'heldPartialRequests': len(held)})
            public_ready = copy.deepcopy(ready)
            public_ready.pop('editor', None)
            result = {'schemaVersion': 1, 'evidenceClass': 'native-loopback-sockets-independent-stdlib-clients',
                      'ready': public_ready, 'readyOmittedFields': ['editor'], 'phases': phases, 'scope': 'Actual HTTP/UDP/WebSocket sockets. Fixture ownership grant is test-only; engine mode grants over actual ACL-protected named pipe. No remote, Bonjour, GPU or durable-save qualification.'}
            args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
            print(f'Passed {len(phases)} native network phases')
        finally:
            for client in reversed(clients):
                client.close()
            unexpected = process.poll()
            process.stdin.close()
            if args.engine and unexpected is None:
                process.terminate()  # Only this test's own engine; no graceful-shutdown claim.
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
                raise AssertionError('Owned network fixture did not stop')
            reader.join(timeout=2)
            process.stdout.close()
            assert not reader.is_alive()
            assert unexpected is None, (unexpected, stderr_path.read_text())
            if not args.engine:
                assert process.returncode == 0, stderr_path.read_text()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--fixture', type=Path)
    selection.add_argument('--engine', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    run(parser.parse_args())
