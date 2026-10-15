#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed the verified experiment shaders; no runtime disk read or compilation."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--directory', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
manifest = json.loads((args.directory / 'provenance.json').read_text(encoding='utf-8'))
hashes = {record['file']: record['sha256'] for record in manifest['files']}
parts = ['// Generated from BSD-2-Clause bgfx assets; retain LICENSE.bgfx.\n#pragma once\n#include <array>\n#include <cstdint>\nnamespace mapgyver::experiments::shaders {\n']
for symbol, name in [('vertex', 'vs_fullscreen.bin'), ('fragment', 'fs_bokeh_copy.bin')]:
    data = (args.directory / name).read_bytes()
    if hashlib.sha256(data).hexdigest() != hashes[name]:
        raise RuntimeError(f'Experiment shader checksum mismatch: {name}')
    parts.append(f'inline constexpr std::array<std::uint8_t, {len(data)}> {symbol} = {{\n')
    parts.extend('  ' + ','.join(f'0x{byte:02x}' for byte in data[index:index+20]) + ',\n' for index in range(0, len(data), 20))
    parts.append('};\n')
parts.append('}\n')
content = ''.join(parts)
args.output.parent.mkdir(parents=True, exist_ok=True)
if not args.output.exists() or args.output.read_text(encoding='utf-8') != content:
    args.output.write_text(content, encoding='utf-8')
