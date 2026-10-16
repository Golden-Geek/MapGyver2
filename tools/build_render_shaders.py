#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline pinned shaderc build. Never called by a render/audio deadline path."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
parser = argparse.ArgumentParser()
parser.add_argument('--compiler', type=Path, required=True)
parser.add_argument('--include', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
parts, records = [], []
for name, kind, profile in [('fullscreen', 'vertex', 's_5_0'), ('composite', 'fragment', 's_5_0'), ('encode', 'fragment', 's_5_0')]:
    source = args.source/(name+'.sc')
    target = args.output/(name+'.bin')
    command = [str(args.compiler), '-f', str(source), '-o', str(target), '--type', kind,
               '--platform', 'windows', '-p', profile, '--varyingdef', str(args.source/'varying.def.sc'),
               '-i', str(args.include), '-O', '3', '--Werror']
    subprocess.run(command, check=True)
    data = target.read_bytes()
    parts.append('inline constexpr unsigned char '+name+'[] = {'+','.join(map(str, data))+'};')
    records.append({'name':name,'command':command,'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                    'binarySha256':hashlib.sha256(data).hexdigest()})
(args.output/'render_shaders.hpp').write_text('// SPDX-License-Identifier: GPL-3.0-or-later\n#pragma once\nnamespace mapgyver::windows::shaders {\n'+'\n'.join(parts)+'\n}\n')
version = subprocess.run([str(args.compiler), '--version'], check=True, capture_output=True, text=True).stdout.strip()
(args.output/'shader-build.json').write_text(json.dumps({'compilerVersion':version,
    'compilerSha256':hashlib.sha256(args.compiler.read_bytes()).hexdigest(),
    'varyingDefinitionSha256':hashlib.sha256((args.source/'varying.def.sc').read_bytes()).hexdigest(),
    'bgfxShaderHeaderSha256':hashlib.sha256((args.include/'bgfx_shader.sh').read_bytes()).hexdigest(),
    'shaders':records},indent=2)+'\n')
