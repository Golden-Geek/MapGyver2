#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded owned-process helpers for manual native Spout qualification."""
from collections import deque
import hashlib
import json
from pathlib import Path
import queue
import struct
import subprocess
import threading
import time

root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'tests/hardware/spout-reference.json').read_text())
prepared = root / 'out/qualification' / f'spout-peer-{manifest["sdkVersion"]}' / 'inputs.json'
def reference_dll_hash():
    if not prepared.exists():
        raise RuntimeError('Build the windows-spout profile first to verify and prepare the reference SDK')
    inputs = json.loads(prepared.read_text())
    return next(item['sha256'] for item in inputs['files'] if item['file'] == 'MD/bin/SpoutDX.dll')


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
            if self.info['sdkVersion'] == manifest['sdkVersion'] and self.dll_hash != reference_dll_hash():
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

