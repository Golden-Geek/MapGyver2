#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual GPU engine/control/browser-loss qualification; never a hosted GPU claim."""
import argparse
import copy
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
parser.add_argument('--hot-images', action='store_true', help='Also qualify pixel-changing source edits, supersession and recovery')
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
        'value': {'type': 'boolean' if isinstance(value, bool) else 'text', 'value': value}}} for address, value in values]}, revision)

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
    resized = until(lambda: observed(client, 4, '', after=int(second['renderer']['frame'])))
    assert resized['authoredRevision'] == '4'
    assert int(resized['renderer']['showTime']['ticks']) > int(second['renderer']['showTime']['ticks'])
    client.call('undo', revision=4)
    restored = until(lambda: observed(client, 5, ''))
    phases.append({'name': 'prepared_canvas_resize_and_undo', 'resized': resized['renderer'], 'restored': restored['renderer']})
    final_revision = 6
    if args.hot_images:
        locator = next(p['address'] for p in restored['data']['registry']['parameters'] if p['address'].endswith('/locator'))
        # The latest desired scene wins; intermediate candidates may legitimately
        # apply before the next command arrives or be cancelled before GPU use.
        writes(client, 5, [(locator, 'checker')])
        writes(client, 6, [(locator, 'alpha')])
        writes(client, 7, [(locator, 'bars'), (paths[1]+'/blackout', True)])
        bars = until(lambda: observed(client, 8, ''))
        assert bars['authoredRevision'] == '8'
        phases.append({'name': 'source_burst_and_atomic_source_output_controls', 'renderer': bars['renderer']})
        writes(client, 8, [(locator, 'unavailable-configured-pattern')])
        refused = until(lambda: observed(client, 8, 'render_state_requires_preparation', after=int(bars['renderer']['frame'])))
        assert refused['authoredRevision'] == '9'
        assert any(a['source']['value'].get('locator') == 'unavailable-configured-pattern' for a in refused['data']['project']['definitions']['assets'].values())
        client.call('undo', revision=9)
        recovered = until(lambda: observed(client, 10, ''))
        assert int(recovered['renderer']['showTime']['ticks']) > int(bars['renderer']['showTime']['ticks'])
        phases.append({'name': 'unavailable_source_retains_last_scene_then_prepared_undo', 'pending': refused['renderer'], 'recovered': recovered['renderer']})
        project_state = recovered['data']['project']
        definitions = project_state['definitions']
        owner_id = project_state['placements'][project_state['rootPlacement']]['source']['instance']
        root_asset = project_state['sources'][owner_id]['asset']
        root_composition = definitions['compositions'][definitions['assets'][root_asset]['source']['value']['composition']]
        input_id = root_composition['inputs'][0]['id']
        external_id = next(id for id, asset in definitions['assets'].items() if asset['source']['type'] == 'ExternalSource')
        canvas = dict(next(iter(root_composition['canvases'].values()))); canvas.update(width=64, height=32)
        definitions['compositions']['native-nested-comp'] = {'id': 'native-nested-comp', 'name': 'Nested during playback',
            'inputs': [], 'outputs': [{'id': 'picture', 'name': 'Picture', 'kind': 'video'}],
            'layers': [{'id': 'content', 'name': 'Content', 'kind': 'video', 'sharedGroup': None,
                'source': {'type': 'AssetPort', 'value': {'asset': external_id, 'port': 'picture'}}}],
            'canvases': {'picture': canvas}}
        definitions['assets']['native-nested-asset'] = {'id': 'native-nested-asset', 'name': 'Nested source',
            'source': {'type': 'CompositionSource', 'value': {'composition': 'native-nested-comp'}}}
        client.call('transaction', {'commands': [
            {'schemaVersion': 1, 'command': {'type': 'ReplaceDefinitions', 'value': {'value': definitions}}},
            {'schemaVersion': 1, 'command': {'type': 'BindInput', 'value': {'owner': owner_id, 'input': input_id,
                'source': {'type': 'AssetPort', 'value': {'asset': 'native-nested-asset', 'port': 'picture'}}, 'keepAlive': False}}}]}, 10)
        nested = until(lambda: observed(client, 11, ''))
        client.call('undo', revision=11)
        original_graph = until(lambda: observed(client, 12, ''))
        phases.append({'name': 'structural_nested_composition_graph_handoff_and_undo', 'nested': nested['renderer'], 'restored': original_graph['renderer']})
        stage = copy.deepcopy(original_graph['data']['project']['stage'])
        calibration = copy.deepcopy(stage['patches'])
        stage['outputs'][outputs[0]]['desired'].update(width=113, height=59)
        stage['outputs'][outputs[0]]['freeze'] = True
        replacement = stage['outputs'].pop(outputs[2]);replacement.update(id='native-replacement-output', name='Replacement during playback')
        stage['outputs'][replacement['id']] = replacement
        removed_patch = next(id for id, patch in stage['patches'].items() if patch['output'] == outputs[2])
        patch = stage['patches'].pop(removed_patch);patch.update(id='native-replacement-patch', output=replacement['id'])
        stage['patches'][patch['id']] = patch
        client.call('transaction', {'commands': [{'schemaVersion': 1, 'command': {'type': 'ReplaceStage', 'value': {'value': stage}}}]}, 12)
        replaced = until(lambda: observed(client, 13, '', after=int(original_graph['renderer']['frame'])))
        assert outputs[2] not in replaced['data']['project']['stage']['outputs']
        for id, old in calibration.items():
            if id != removed_patch: assert replaced['data']['project']['stage']['patches'][id]['calibration'] == old['calibration']
        client.call('undo', revision=13)
        restored_windows = until(lambda: observed(client, 14, ''))
        assert restored_windows['data']['project']['stage'] == original_graph['data']['project']['stage']
        phases.append({'name': 'atomic_window_resize_remove_add_frozen_carry_and_undo', 'replaced': replaced['renderer'], 'restored': restored_windows['renderer']})
        calibrated = copy.deepcopy(restored_windows['data']['project']['stage'])
        calibration = next(iter(calibrated['patches'].values()))['calibration']
        calibration['vertices'][1]['projectorPixels']['y'] = calibration['referenceHeight']*.1
        client.call('transaction', {'commands': [{'schemaVersion': 1, 'command': {'type': 'ReplaceStage', 'value': {'value': calibrated}}}]}, 14)
        quad_applied = until(lambda: observed(client, 15, '', after=int(restored_windows['renderer']['frame'])))
        client.call('undo', revision=15)
        quad_restored = until(lambda: observed(client, 16, ''))
        phases.append({'name': 'authored_four_corner_calibration_handoff_and_undo', 'applied': quad_applied['renderer'], 'restored': quad_restored['renderer']})
        unsupported = copy.deepcopy(quad_restored['data']['project']['stage'])
        next(iter(unsupported['patches'].values()))['calibration']['mode'] = 'mesh'
        client.call('transaction', {'commands': [{'schemaVersion': 1, 'command': {'type': 'ReplaceStage', 'value': {'value': unsupported}}}]}, 16)
        held_mapping = until(lambda: observed(client, 16, 'render_state_requires_preparation', after=int(quad_restored['renderer']['frame'])))
        assert held_mapping['authoredRevision'] == '17'
        client.call('undo', revision=17)
        mapping_recovered = until(lambda: observed(client, 18, ''))
        phases.append({'name': 'unsupported_mapping_retains_scene_and_authored_calibration_then_undo', 'pending': held_mapping['renderer'], 'recovered': mapping_recovered['renderer']})
        visual_project = mapping_recovered['data']['project'];visual_surface = next(iter(visual_project['stage']['surfaces']))
        visual_patch = next(iter(visual_project['stage']['patches']))
        visual_composition = next(iter(visual_project['definitions']['compositions']))
        visual_layer = visual_project['definitions']['compositions'][visual_composition]['layers'][0]['id']
        surface_visual = f'/stage/surfaces/{visual_surface}/content';patch_visual = f'/stage/patches/{visual_patch}/visual'
        layer_visual = f'/show/compositions/{visual_composition}/layers/{visual_layer}/visual'
        visual_values = [(surface_visual+'/fit','contentFit','fill'),(surface_visual+'/cropMinimum','Vec2',{'x':.125,'y':.125}),
            (surface_visual+'/cropMaximum','Vec2',{'x':.875,'y':.875}),(patch_visual+'/mask/shape','maskShape','ellipse'),
            (patch_visual+'/mask/feather','real',.1),(patch_visual+'/opacity','real',.8),(patch_visual+'/blend','blendMode','screen'),
            (layer_visual+'/opacity','real',.75),(layer_visual+'/blend','blendMode','add')]
        registered = {item['address'] for item in mapping_recovered['data']['registry']['parameters']}
        assert all(address in registered for address,kind,value in visual_values)
        client.call('parameters', {'writes':[{'schemaVersion':1,'write':{'address':address,'value':{'type':kind,'value':value}}}
            for address,kind,value in visual_values]}, 18)
        visual_applied = until(lambda: observed(client, 19, ''))
        assert visual_applied['data']['project']['sources'] == visual_project['sources']
        assert visual_applied['data']['project']['placements'] == visual_project['placements']
        assert visual_applied['data']['project']['stage']['patches'][visual_patch]['calibration'] == visual_project['stage']['patches'][visual_patch]['calibration']
        client.call('undo', revision=19)
        visual_restored = until(lambda: observed(client, 20, ''))
        assert visual_restored['data']['project']['stage'] == visual_project['stage']
        phases.append({'name':'registry_crop_mask_layer_patch_blends_and_undo','applied':visual_applied['renderer'],'restored':visual_restored['renderer']})
        final_revision = 22
    owner = client.snapshot()
    client.call('transferEdit', {'expectedOwnershipGeneration': owner['ownershipGeneration'], 'targetSession': ready['oscquery']['oscSessionId']})
    def osc_string(value):
        data = value.encode('utf-8')+b'\0'; return data+b'\0'*((-len(data)) % 4)
    messages = [osc_string(path)+osc_string(',T' if value else ',F') for path, value in
        [(paths[0]+'/freeze', not args.hot_images), (paths[1]+'/blackout', True), (paths[2]+'/enabled', False)]]
    if args.hot_images: messages.append(osc_string(locator)+osc_string(',s')+osc_string('alpha'))
    if args.hot_images:
        messages.extend([osc_string(patch_visual+'/opacity')+osc_string(',d')+struct.pack('>d',.75),
            osc_string(patch_visual+'/mask/shape')+osc_string(',s')+osc_string('rectangle'),
            osc_string(patch_visual+'/blend')+osc_string(',s')+osc_string('screen')])
    bundle = b'#bundle\0'+struct.pack('>Q', 1)+b''.join(struct.pack('>I', len(message))+message for message in messages)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as osc: osc.sendto(bundle, ('127.0.0.1', ready['oscquery']['oscPort']))
    if args.hot_images:
        osc_source = until(lambda: observed(client, 21, ''))
        phases.append({'name': 'granted_OSC_atomic_source_and_output_handoff', 'renderer': osc_source['renderer']})
        # Hold only after the new alpha pixels were submitted; the final golden
        # can then compare that held image against the final compiled source.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as osc:
            osc.sendto(osc_string(paths[0]+'/freeze')+osc_string(',T'), ('127.0.0.1', ready['oscquery']['oscPort']))
    final = until(lambda: observed(client, final_revision))
    phases.append({'name': 'operator_granted_OSC_bundle_same_native_validator', 'renderer': final['renderer']})
    client.close()
    engine.wait(timeout=25); assert engine.returncode == 0
    reader.join(timeout=2); assert not reader.is_alive()
    native = json.loads(native_report.read_text(encoding='utf-8'))
    assert native['framesSubmitted'] == 900 and native['appliedRevision'] == final_revision and len(native['captures']) == 3
    if args.hot_images:
        assert native['sceneGeneration'] >= 7 and native['sourceGpuCopySubmissions'] >= 3
        assert native['graphGenerationsApplied'] >= 10 and native['internalConstantCpuUploadBytes'] == 16
        assert native['windowGenerationsApplied'] == 2
        assert native['preparedGenerations'] >= native['sourceGpuCopySubmissions']+native['graphGenerationsApplied']
        assert native['sourceCopiedPayloadBytes'] == native['sourceGpuCopySubmissions']*131072
        assert native['sourceCpuUploadBytes'] >= (native['sourceGpuCopySubmissions']+1)*131072
        assert native['captures'][0]['maxChannelError'] <= 2  # Final alpha content differs from the initial gradient.
    assert native['vendorId'] and all(c['maxChannelError'] <= c['tolerance'] == 2 for c in native['captures'])
    captures = {c['output']: c for c in native['captures']}
    assert captures[outputs[0]]['heldSoftwareFrame'] < 900 and captures[outputs[1]]['heldSoftwareFrame'] == 900
    assert captures[outputs[2]]['heldSoftwareFrame'] < 900
    if args.hot_images:
        # A loaded unavailable object remains authored; preparing a supported
        # locator later must clear the initial readonly unavailable status.
        engine.stdout.close()
        unavailable_project = args.report.with_suffix('.unavailable.project.json')
        unavailable = json.loads(project.read_text(encoding='utf-8'))
        for asset in unavailable['definitions']['assets'].values():
            if asset['source']['type'] == 'ExternalSource': asset['source']['value']['locator'] = 'unavailable-configured-pattern'
        unavailable_project.write_text(json.dumps(unavailable), encoding='utf-8')
        recovery_report = args.report.with_suffix('.recovery.native.json')
        recovery_command = [str(args.engine.resolve()), '--serve', '--pipe-name', 'recover-'+uuid.uuid4().hex,
            '--render', '--render-hidden', '--initial-state', str(unavailable_project.resolve()), '--native-frames', '120',
            '--native-report', str(recovery_report.resolve())]
        engine = subprocess.Popen(recovery_command, stdout=subprocess.PIPE, stderr=stderr, text=True, encoding='utf-8', creationflags=subprocess.CREATE_NO_WINDOW)
        lines = queue.Queue();reader = threading.Thread(target=read_lines, args=(engine.stdout, lines), daemon=True);reader.start()
        recovery_ready = json.loads(lines.get(timeout=10)); recovery_client = Client(recovery_ready)
        initial_missing = until(lambda: observed(recovery_client, 0, 'source_frames_unavailable'))
        recovery_locator = next(p['address'] for p in initial_missing['data']['registry']['parameters'] if p['address'].endswith('/locator'))
        writes(recovery_client, 0, [(recovery_locator, 'gradient')])
        initial_recovered = until(lambda: observed(recovery_client, 1, ''))
        recovery_client.close();engine.wait(timeout=10);assert engine.returncode == 0;reader.join(timeout=2);assert not reader.is_alive()
        recovery = json.loads(recovery_report.read_text(encoding='utf-8'))
        assert recovery['framesSubmitted'] == 120 and recovery['graphGenerationsApplied'] == 1 and recovery['appliedRevision'] == 1
        assert all(c['maxChannelError'] <= c['tolerance'] == 2 for c in recovery['captures'])
        phases.append({'name': 'loaded_unavailable_source_prepared_recovery', 'initial': initial_missing['renderer'],
            'recovered': initial_recovered['renderer'], 'command': recovery_command, 'native': recovery})
    phases.append({'name': 'normal_native_shutdown_and_offline_gpu_goldens', 'finalObserved': final['renderer'], 'native': native})
    report = {'evidenceClass': 'native-hardware-engine-control-Chromium-process-death', 'command': command,
        'sourceRevision': ready['sourceRevision'], 'sourceDirty': ready['sourceDirty'], 'phases': phases,
        'engineSha256': hashlib.sha256(args.engine.read_bytes()).hexdigest(),
        'fixtureSha256': hashlib.sha256(project.read_bytes()).hexdigest(),
        'screenshotSha256': hashlib.sha256(screenshot.read_bytes()).hexdigest(),
        'hotImageChanges': args.hot_images,
        'scope': 'Stateless test patterns on three hidden native windows. Real browser reload/process death and no-client interval; fixed-tolerance offline final intermediate captures. Optional hot image changes qualify bounded fixed-layout preparation, supersession and recovery. Not physical scanout, animated media playback, graph/window resource handoff or encoded preview qualification.'}
    args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('Passed native engine window/graph/source/browser-loss phases and offline GPU goldens')
finally:
    for client in clients: client.close()
    if browser_job: browser_job.close()
    if browser and browser.poll() is None: browser.terminate(); browser.wait(timeout=10)
    if engine.poll() is None: engine.terminate(); engine.wait(timeout=10)
    engine.stdout.close(); stderr.close()
