#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual GPU engine/control/browser-loss qualification; never a hosted GPU claim."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tests/protocol'))
from pipe_client import Pipe

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', type=Path, required=True)
parser.add_argument('--probe', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
args.report.parent.mkdir(parents=True, exist_ok=True)
project = args.report.with_suffix('.project.json')
native_report = args.report.with_suffix('.native.json')
subprocess.run([str(args.probe.resolve()), '--write-project', str(project.resolve())], check=True, timeout=10)
command = [str(args.engine.resolve()), '--serve', '--pipe-name', 'native-'+uuid.uuid4().hex, '--oscquery',
           '--render', '--render-hidden', '--initial-state', str(project.resolve()), '--native-frames', '900',
           '--native-report', str(native_report.resolve())]
stderr = args.report.with_suffix('.stderr.log').open('wb')
engine = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=stderr, text=True, encoding='utf-8',
                          creationflags=subprocess.CREATE_NO_WINDOW)
lines = queue.Queue()
def read_lines(stream, target):
    for line in stream: target.put(line)
    target.put(None)
reader = threading.Thread(target=read_lines, args=(engine.stdout, lines), daemon=True); reader.start()
browser = None
browser_job = None
clients = []
phases = []

class BrowserJob:
    """Contain only the helper and Chromium children it creates after assignment."""
    def __init__(self, process):
        class Basic(C.Structure):
            _fields_ = [('process_time', C.c_int64), ('job_time', C.c_int64), ('flags', C.c_uint32),
                ('min_working_set', C.c_size_t), ('max_working_set', C.c_size_t), ('active_processes', C.c_uint32),
                ('affinity', C.c_size_t), ('priority', C.c_uint32), ('scheduling', C.c_uint32)]
        class Extended(C.Structure):
            _fields_ = [('basic', Basic), ('io', C.c_uint64*6), ('process_memory', C.c_size_t),
                ('job_memory', C.c_size_t), ('peak_process_memory', C.c_size_t), ('peak_job_memory', C.c_size_t)]
        kernel = C.WinDLL('kernel32', use_last_error=True)
        create = kernel.CreateJobObjectW; create.argtypes = [C.c_void_p, C.c_wchar_p]; create.restype = C.c_void_p
        assign = kernel.AssignProcessToJobObject; assign.argtypes = [C.c_void_p, C.c_void_p]; assign.restype = C.c_int
        configure = kernel.SetInformationJobObject; configure.argtypes = [C.c_void_p, C.c_int, C.c_void_p, C.c_uint32]; configure.restype = C.c_int
        self.close_handle = kernel.CloseHandle; self.close_handle.argtypes = [C.c_void_p]; self.close_handle.restype = C.c_int
        self.handle = create(None, None)
        if not self.handle: raise C.WinError(C.get_last_error())
        limits = Extended(); limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not configure(self.handle, 9, C.byref(limits), C.sizeof(limits)) or not assign(self.handle, int(process._handle)):
            error = C.WinError(C.get_last_error()); self.close(); raise error
    def close(self):
        if self.handle:
            if not self.close_handle(self.handle): raise C.WinError(C.get_last_error())
            self.handle = None

class Client:
    def __init__(self, ready):
        self.pipe = Pipe(ready['endpoint']); self.name = 'native-client-'+uuid.uuid4().hex
        self.pipe.send({'protocolVersion': 1, 'clientId': self.name, 'resumeSessionId': None, 'requestEdit': True})
        self.hello = self.pipe.receive(); assert self.hello['type'] == 'hello'
        self.sequence = int(self.hello['nextSequence']); clients.append(self)
    def call(self, operation, payload=None, revision=None):
        message = {'protocolVersion': 1, 'serviceId': self.hello['serviceId'], 'clientId': self.name,
            'sessionId': self.hello['sessionId'], 'requestId': f'request-{self.sequence}', 'sequence': str(self.sequence),
            'expectedRevision': None if revision is None else str(revision), 'applyAt': None,
            'operation': operation, 'payload': {} if payload is None else payload}
        self.pipe.send(message); reply = self.pipe.reply(message['requestId'])
        assert reply['status'] == 'accepted', reply
        if reply['consumed']: self.sequence += 1
        return reply
    def snapshot(self): return self.call('snapshot')['payload']
    def close(self):
        if self.pipe: self.pipe.close(); self.pipe = None

def until(check, seconds=7):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        assert engine.poll() is None, 'Owned engine stopped before its frame budget'
        result = check()
        if result: return result
        time.sleep(.02)
    raise AssertionError('Expected native application state was not observed')

def observed(client, revision=None, reason=None, after=None):
    state = client.snapshot(); render = state['renderer']
    if not render['available']: return None
    if revision is not None and render['appliedRevision'] != str(revision): return None
    if reason is not None and render['reason'] != reason: return None
    if after is not None and int(render['frame']) <= after: return None
    return state

def writes(client, revision, values):
    return client.call('parameters', {'writes': [{'schemaVersion': 1, 'write': {'address': address,
        'value': {'type': 'boolean', 'value': value}}} for address, value in values]}, revision)

try:
    ready_line = lines.get(timeout=10); assert ready_line is not None
    ready = json.loads(ready_line); assert ready['event'] == 'control_ready' and ready['nativeProgram']['configured']
    client = Client(ready)
    first = until(lambda: observed(client, 0))
    assert first['renderer']['generation'] == '1' and first['persistence']['savedRevision'] is None
    outputs = list(first['data']['project']['stage']['outputs'])
    assert len(outputs) == 3
    paths = [f'/outputs/{id}' for id in outputs]
    phases.append({'name': 'native_without_browser', 'renderer': first['renderer']})
    client.call('releaseEdit', {'expectedOwnershipGeneration': first['ownershipGeneration']})

    browser_stderr = args.report.with_suffix('.browser.stderr.log').open('wb')
    browser = subprocess.Popen(['node', str(ROOT/'ui/tests/native-program.mjs')], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=browser_stderr, text=True, encoding='utf-8', creationflags=subprocess.CREATE_NO_WINDOW)
    browser_job = BrowserJob(browser)  # Chromium is not launched until the next stdin command.
    browser_lines = queue.Queue()
    browser_reader = threading.Thread(target=read_lines, args=(browser.stdout, browser_lines), daemon=True); browser_reader.start()
    def browser_command(operation, **fields):
        browser.stdin.write(json.dumps({'operation': operation, **fields})+'\n'); browser.stdin.flush()
        line = browser_lines.get(timeout=15); assert line is not None, 'Owned browser helper stopped unexpectedly'
        return json.loads(line)
    opened = browser_command('open', url=ready['editor']['url'])
    assert opened['event'] == 'opened'
    screenshot = args.report.with_suffix('.ui.png')
    ui_control = browser_command('set_control', output=outputs[0], parameter='freeze', value=True, revision='1', screenshot=str(screenshot.resolve()))
    assert ui_control['event'] == 'ui_control_applied'
    ui_applied = until(lambda: observed(client, 1))
    phases.append({'name': 'Svelte_checkbox_shared_validator_native_application', 'renderer': ui_applied['renderer']})
    assert browser_command('release_edit')['event'] == 'editing_released'
    ownership = client.snapshot()['ownershipGeneration']
    client.call('claimEdit', {'expectedOwnershipGeneration': ownership})
    before = until(lambda: observed(client, 1))['renderer']
    reloaded = browser_command('reload'); assert reloaded['event'] == 'reloaded'
    after = until(lambda: observed(client, 1, after=int(before['frame'])))['renderer']
    assert int(after['showTime']['ticks']) > int(before['showTime']['ticks'])
    phases.append({'name': 'browser_reload', 'before': before, 'after': after})

    reply = writes(client, 1, [(paths[0]+'/freeze', True), (paths[1]+'/blackout', True), (paths[2]+'/enabled', False)])
    assert reply['authoredRevision'] == '2'
    applied = until(lambda: observed(client, 2))
    phases.append({'name': 'atomic_native_output_controls', 'renderer': applied['renderer']})
    crashed = browser_command('crash'); assert crashed['event'] == 'crashed'
    browser.stdin.close()  # EOF also releases the helper's owned stdin pipe.
    browser.wait(timeout=10); assert browser.returncode == 0
    browser.stdout.close(); browser_stderr.close(); browser_reader.join(timeout=2)
    assert not browser_reader.is_alive()
    client.close()
    time.sleep(2)  # No browser, no control clients and no preview subscribers.
    client = Client(ready)
    continued = until(lambda: observed(client, 2, after=int(applied['renderer']['frame'])))
    assert int(continued['renderer']['showTime']['ticks']) > int(applied['renderer']['showTime']['ticks'])
    phases.append({'name': 'all_clients_absent_after_browser_process_death', 'browser': crashed,
                   'before': applied['renderer'], 'after': continued['renderer']})

    writes(client, 2, [(paths[0]+'/freeze', False), (paths[1]+'/blackout', False), (paths[2]+'/enabled', True)])
    second = until(lambda: observed(client, 3))
    width = next(p['address'] for p in second['data']['registry']['parameters'] if '/canvases/' in p['address'] and p['address'].endswith('/width'))
    changed = client.call('parameters', {'writes': [{'schemaVersion': 1, 'write': {'address': width,
        'value': {'type': 'unsignedInteger', 'value': '129'}}}]}, 3)
    assert changed['authoredRevision'] == '4'
    pending = until(lambda: observed(client, 3, 'render_state_requires_preparation', after=int(second['renderer']['frame'])))
    assert pending['authoredRevision'] == '4'
    client.call('undo', revision=4)
    restored = until(lambda: observed(client, 5, ''))
    phases.append({'name': 'resource_change_retains_last_applied_then_undo', 'pending': pending['renderer'], 'restored': restored['renderer']})
    owner = client.snapshot()
    client.call('transferEdit', {'expectedOwnershipGeneration': owner['ownershipGeneration'], 'targetSession': ready['oscquery']['oscSessionId']})
    def osc_string(value):
        data = value.encode('utf-8')+b'\0'; return data+b'\0'*((-len(data)) % 4)
    messages = [osc_string(path)+osc_string(',T' if value else ',F') for path, value in
        [(paths[0]+'/freeze', True), (paths[1]+'/blackout', True), (paths[2]+'/enabled', False)]]
    bundle = b'#bundle\0'+struct.pack('>Q', 1)+b''.join(struct.pack('>I', len(message))+message for message in messages)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as osc: osc.sendto(bundle, ('127.0.0.1', ready['oscquery']['oscPort']))
    final = until(lambda: observed(client, 6))
    phases.append({'name': 'operator_granted_OSC_bundle_same_native_validator', 'renderer': final['renderer']})
    client.close()
    engine.wait(timeout=25); assert engine.returncode == 0
    reader.join(timeout=2); assert not reader.is_alive()
    native = json.loads(native_report.read_text(encoding='utf-8'))
    assert native['framesSubmitted'] == 900 and native['appliedRevision'] == 6 and len(native['captures']) == 3
    assert native['vendorId'] and all(c['maxChannelError'] <= c['tolerance'] == 2 for c in native['captures'])
    captures = {c['output']: c for c in native['captures']}
    assert captures[outputs[0]]['heldSoftwareFrame'] < 900 and captures[outputs[1]]['heldSoftwareFrame'] == 900
    assert captures[outputs[2]]['heldSoftwareFrame'] < 900
    phases.append({'name': 'normal_native_shutdown_and_offline_gpu_goldens', 'finalObserved': final['renderer'], 'native': native})
    report = {'evidenceClass': 'native-hardware-engine-control-Chromium-process-death', 'command': command,
        'sourceRevision': ready['sourceRevision'], 'sourceDirty': ready['sourceDirty'], 'phases': phases,
        'engineSha256': hashlib.sha256(args.engine.read_bytes()).hexdigest(),
        'fixtureSha256': hashlib.sha256(project.read_bytes()).hexdigest(),
        'screenshotSha256': hashlib.sha256(screenshot.read_bytes()).hexdigest(),
        'scope': 'Static test patterns on three hidden native windows. Real browser reload/process death and no-client interval; fixed-tolerance offline final intermediate captures. Not physical scanout, animated media playback, hot GPU-resource preparation or encoded preview qualification.'}
    args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Passed native engine/control/browser-loss phases, 900 frames and three offline GPU goldens')
finally:
    for client in clients: client.close()
    if browser_job: browser_job.close()
    if browser and browser.poll() is None: browser.terminate(); browser.wait(timeout=10)
    if engine.poll() is None: engine.terminate(); engine.wait(timeout=10)
    engine.stdout.close(); stderr.close()
