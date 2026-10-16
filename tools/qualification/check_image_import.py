#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real WIC/engine/image handoff qualification, separate from physical scanout."""
import argparse
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
sys.path.insert(0,str(ROOT/'tests/protocol'))
from pipe_client import Pipe
sys.path.insert(0,str(ROOT/'tests/media'))
from generate_images import generate

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine',type=Path,required=True)
parser.add_argument('--output-probe',type=Path,required=True)
parser.add_argument('--image-probe',type=Path,required=True)
parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args();args.report.parent.mkdir(parents=True,exist_ok=True)
fixtures=args.report.parent/'image-fixtures';generate(fixtures)
metadata=json.loads((fixtures/'fixtures.json').read_text(encoding='utf-8'))
cpu_command=[str(args.image_probe.resolve()),str(fixtures.resolve()),'--json']
cpu_run=subprocess.run(cpu_command,capture_output=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
args.report.with_suffix('.cpu.stdout.json').write_bytes(cpu_run.stdout)
args.report.with_suffix('.cpu.stderr.log').write_bytes(cpu_run.stderr)
assert cpu_run.returncode == 0,cpu_run.stderr.decode(errors='replace')
cpu=json.loads(cpu_run.stdout)
assert cpu['pngSha256'] == metadata['first.png']['sha256'] and cpu['rejectedRequests'] == 5
assert cpu['qualifiedExifOrientations'] == list(range(1,9))
assert all(cpu[key] for key in ('requestCapacityRejected','cancelBeforeRead','cancelDuringRead','foreignOwnerRejected','unicodeAndLongRelativePaths','jpegExifRotation','contentHashInvalidation'))
project=args.report.with_suffix('.project.json')
subprocess.run([str(args.output_probe.resolve()),'--write-project',str(project.resolve())],check=True,timeout=10)
original=json.loads(project.read_text(encoding='utf-8'))
native_report=args.report.with_suffix('.native.json')
command=[str(args.engine.resolve()),'--serve','--pipe-name','image-'+uuid.uuid4().hex,'--oscquery','--render','--render-hidden',
    '--initial-state',str(project.resolve()),'--media-root',str(fixtures.resolve()),'--image','first.png',
    '--native-frames','480','--native-report',str(native_report.resolve())]
stderr=args.report.with_suffix('.stderr.log').open('wb')
engine=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=stderr,text=True,encoding='utf-8',creationflags=subprocess.CREATE_NO_WINDOW)
lines=queue.Queue()
def read_lines():
    for line in engine.stdout: lines.put(line)
    lines.put(None)
reader=threading.Thread(target=read_lines,daemon=True);reader.start();client=None;phases=[]
class Client:
    def __init__(self,ready):
        self.pipe=Pipe(ready['endpoint']);self.id='image-client-'+uuid.uuid4().hex
        self.pipe.send({'protocolVersion':1,'clientId':self.id,'resumeSessionId':None,'requestEdit':True})
        self.hello=self.pipe.receive();assert self.hello['type']=='hello';self.sequence=int(self.hello['nextSequence'])
    def call(self,operation,payload=None,revision=None):
        request={'protocolVersion':1,'serviceId':self.hello['serviceId'],'clientId':self.id,'sessionId':self.hello['sessionId'],
            'requestId':'image-request-'+str(self.sequence),'sequence':str(self.sequence),'expectedRevision':None if revision is None else str(revision),
            'applyAt':None,'operation':operation,'payload':{} if payload is None else payload}
        self.pipe.send(request);reply=self.pipe.reply(request['requestId']);assert reply['status']=='accepted',reply
        if reply['consumed']: self.sequence+=1
        return reply
    def snapshot(self):return self.call('snapshot')['payload']
    def write(self,address,kind,value,revision):
        return self.call('parameters',{'writes':[{'schemaVersion':1,'write':{'address':address,'value':{'type':kind,'value':value}}}]},revision)
    def close(self):self.pipe.close()
def until(revision,reason='',after=-1):
    deadline=time.monotonic()+6
    while time.monotonic()<deadline:
        assert engine.poll() is None,'Owned image engine stopped early'
        state=client.snapshot();renderer=state['renderer']
        if renderer['available'] and renderer['appliedRevision']==str(revision) and renderer['reason']==reason and int(renderer['frame'])>after:return state
        time.sleep(.02)
    raise AssertionError('Image scene application/reason was not observed')
def osc_string(value):
    data=value.encode('utf-8')+b'\0';return data+b'\0'*((-len(data))%4)
try:
    ready=json.loads(lines.get(timeout=10));assert ready['event']=='control_ready'
    client=Client(ready);loaded=until(1)
    state=loaded['data']['project'];assert state['stage']==original['stage']
    asset=next(id for id,item in state['definitions']['assets'].items() if item['source']['type']=='ExternalSource' and item['source']['value']['adapter']=='mapgyver.image')
    locator=f'/library/assets/{asset}/locator'
    phases.append({'name':'relative_image_import_without_browser','renderer':loaded['renderer']})
    client.write(locator,'text','second.png',1);changed=until(2,after=int(loaded['renderer']['frame']))
    phases.append({'name':'live_file_relink','renderer':changed['renderer']})
    client.write(locator,'text','missing.png',2);missing=until(2,'image_file_missing',int(changed['renderer']['frame']))
    assert missing['authoredRevision']=='3' and missing['data']['project']['definitions']['assets'][asset]['source']['value']['locator']=='missing.png'
    assert int(missing['renderer']['showTime']['ticks'])>int(changed['renderer']['showTime']['ticks'])
    client.call('undo',revision=3);restored=until(4)
    phases.append({'name':'missing_file_retains_configuration_frame_clock_then_undo','missing':missing['renderer'],'restored':restored['renderer']})
    fit=next(item['address'] for item in restored['data']['registry']['parameters'] if '/layers/' in item['address'] and item['address'].endswith('/visual/fit'))
    client.write(fit,'contentFit','fill',4);filled=until(5)
    assert filled['data']['project']['stage']['patches']==original['stage']['patches']
    phases.append({'name':'cached_pixels_new_layer_fit_without_calibration_changes','renderer':filled['renderer']})
    owner=client.snapshot();client.call('transferEdit',{'expectedOwnershipGeneration':owner['ownershipGeneration'],'targetSession':ready['oscquery']['oscSessionId']})
    outputs=list(original['stage']['outputs']);messages=[osc_string(locator)+osc_string(',s')+osc_string('first.png'),
        osc_string(fit)+osc_string(',s')+osc_string('fit'),osc_string(f'/outputs/{outputs[1]}/blackout')+osc_string(',T'),
        osc_string(f'/outputs/{outputs[2]}/enabled')+osc_string(',F')]
    bundle=b'#bundle\0'+struct.pack('>Q',1)+b''.join(struct.pack('>I',len(message))+message for message in messages)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as osc:osc.sendto(bundle,('127.0.0.1',ready['oscquery']['oscPort']))
    osc_applied=until(6)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as osc:osc.sendto(osc_string(f'/outputs/{outputs[0]}/freeze')+osc_string(',T'),('127.0.0.1',ready['oscquery']['oscPort']))
    final=until(7);phases.append({'name':'granted_OSC_file_fit_and_output_flags','source':osc_applied['renderer'],'held':final['renderer']})
    client.close();client=None;engine.wait(timeout=20);assert engine.returncode==0
    reader.join(timeout=2);assert not reader.is_alive()
    native=json.loads(native_report.read_text(encoding='utf-8'))
    assert native['framesSubmitted']==480 and native['appliedRevision']==7 and len(native['captures'])==3
    assert native['decodedFiles']==2 and native['imageCacheHits']==3
    assert native['decodedRgba8CpuBytes']==2*257*131*4
    assert native['fileReadCpuBytes']==metadata['first.png']['bytes']*2+metadata['second.png']['bytes']*3
    assert native['fileAssets'][0]['locator']=='first.png' and native['fileAssets'][0]['sha256']==metadata['first.png']['sha256']
    assert native['sourceGpuCopySubmissions']==2 and native['sourceCopiedPayloadBytes']==2*257*131*16
    assert all(item['maxChannelError']<=item['tolerance']==2 for item in native['captures'])
    phases.append({'name':'normal_shutdown_and_offline_file_gpu_goldens','native':native})
    quick_report=args.report.with_suffix('.quick.native.json')
    quick_command=[str(args.engine.resolve()),'--serve','--pipe-name','image-quick-'+uuid.uuid4().hex,'--render','--render-hidden',
        '--image',str((fixtures/'éclairage_雪.png').resolve()),'--native-frames','120','--native-report',str(quick_report.resolve())]
    quick=subprocess.run(quick_command,capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
    args.report.with_suffix('.quick.stderr.log').write_bytes(quick.stderr);assert quick.returncode==0,quick.stderr.decode(errors='replace')
    quick_native=json.loads(quick_report.read_text(encoding='utf-8'))
    assert quick_native['framesSubmitted']==120 and quick_native['appliedRevision']==1 and len(quick_native['captures'])==1
    assert quick_native['decodedFiles']==1 and quick_native['fileAssets'][0]['sha256']==metadata['first.png']['sha256']
    assert quick_native['captures'][0]['maxChannelError']<=2
    codec_path=Path(cpu['codecModule']);assert codec_path.is_file()
    report={'schemaVersion':1,'evidenceClass':'native-image-import-background-WIC-and-hidden-GPU-output',
        'sourceRevision':ready['sourceRevision'],'sourceDirty':ready['sourceDirty'],'command':command,'cpuCommand':cpu_command,
        'engineSha256':hashlib.sha256(args.engine.read_bytes()).hexdigest(),'imageProbeSha256':hashlib.sha256(args.image_probe.read_bytes()).hexdigest(),
        'outputProbeSha256':hashlib.sha256(args.output_probe.read_bytes()).hexdigest(),'codecModule':str(codec_path),'codecModuleSha256':hashlib.sha256(codec_path.read_bytes()).hexdigest(),
        'fixtures':metadata,'cpu':cpu,'phases':phases,'quickCommand':quick_command,'quickNative':quick_native,
        'scope':'Actual decoded file pixels reach an independent native owner through immutable handoffs; relative/unicode/long paths, hashes, cache, relink/undo/failure, granted OSC and source fit. CPU decode and hidden-window intermediate GPU captures, not hardware decode, physical scanout, browser upload or video/audio qualification.'}
    args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Passed real image import, relink/cache/failure/OSC phases and direct one-surface GPU path')
finally:
    if client is not None:client.close()
    if engine.poll() is None:engine.kill();engine.wait(timeout=5)
    reader.join(timeout=2);engine.stdout.close();stderr.close()
