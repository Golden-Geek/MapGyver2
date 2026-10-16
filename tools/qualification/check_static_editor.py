#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check an owned Windows install tree serves packaged assets without Node on PATH."""
import argparse
import hashlib
from http.client import HTTPConnection
import json
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import threading
import uuid

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
if sys.platform != 'win32':
    raise SystemExit('This production-engine check requires native Windows; portable socket/UI fixtures are separate evidence')
prefix = args.prefix.resolve()
if not prefix.is_relative_to(ROOT/'out'):
    raise SystemExit('Qualification install prefix must stay inside this workspace out directory')
engine = prefix/'bin/mapgyver-engine.exe'
environment = os.environ.copy()
environment['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')
assert shutil.which('node', path=environment['PATH']) is None
args.report.parent.mkdir(parents=True, exist_ok=True)
with args.report.with_suffix('.stderr.log').open('wb') as stderr:
    process = subprocess.Popen([str(engine), '--serve', '--pipe-name', 'installed-'+uuid.uuid4().hex, '--oscquery'],
        stdout=subprocess.PIPE, stderr=stderr, text=True, encoding='utf-8', env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
    lines = queue.Queue()
    def read():
        for line in process.stdout:
            lines.put(line)
        lines.put(None)
    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    try:
        line = lines.get(timeout=10)
        assert line is not None, 'Installed engine did not become ready'
        ready = json.loads(line)
        assert ready['editor']['staticAssets']
        port = ready['oscquery']['httpPort']
        def get(path):
            connection = HTTPConnection('127.0.0.1', port, timeout=5)
            try:
                connection.request('GET', path)
                response = connection.getresponse()
                assert response.status == 200, (path, response.status)
                return response.read()
            finally:
                connection.close()
        index = get('/editor/')
        paths = re.findall(r'(?:src|href)="([^"]+)"', index.decode('utf-8'))
        assert any(path.endswith('.js') for path in paths) and any(path.endswith('.css') for path in paths)
        assets = [{'path':path, 'sha256':hashlib.sha256(get(path)).hexdigest()} for path in paths]
        assert b'GNU GENERAL PUBLIC LICENSE' in get('/editor/LICENSE.txt')
        assert b'Svelte' in get('/editor/THIRD_PARTY_NOTICES.txt')
        args.report.write_text(json.dumps({'schemaVersion':1,'sourceRevision':ready['sourceRevision'],
            'sourceDirty':ready['sourceDirty'],'evidenceClass':'native-Windows-installed-static-editor-without-Node-on-child-PATH',
            'nodeOnChildPath':False,'staticAssets':True,'assets':assets,'engineSha256':hashlib.sha256(engine.read_bytes()).hexdigest(),
            'scope':'Actual installed Windows engine serves compiled static assets and notices on this laptop. Node remains installed on the host; only the engine child PATH excludes it. No clean-machine/installer/GPU/rendered/durable-save qualification.'},indent=2)+'\n')
        print('Installed native engine served static HTML/JS/CSS/notices with Node absent from its PATH')
    finally:
        unexpected = process.poll()
        if unexpected is None:
            process.terminate()
        process.wait(timeout=10)
        reader.join(timeout=2)
        process.stdout.close()
        assert unexpected is None and not reader.is_alive()
