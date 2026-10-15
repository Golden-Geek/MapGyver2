#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare pinned independent Spout peer SDK inputs; this never claims SDK/GPU success."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import struct
import sys
from urllib.request import urlopen
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='Use an already downloaded, checksum-verified official archive')
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'tests/hardware/spout-reference.json').read_text(encoding='utf-8'))
    output = (ROOT / 'out/qualification' / f'spout-peer-{manifest["sdkVersion"]}').resolve()
    if not output.is_relative_to(ROOT.resolve()):
        raise RuntimeError('Reference fixture output escapes the workspace')
    archive = args.archive or (ROOT / 'out/downloads' / f'spout-reference-{manifest["sdkVersion"]}.zip')
    if not archive.exists():
        if args.archive:
            raise FileNotFoundError(archive)
        archive.parent.mkdir(parents=True, exist_ok=True)
        with urlopen(manifest['archive']['url'], timeout=60) as response:
            data = response.read(manifest['archive']['size'] + 1)
        if len(data) != manifest['archive']['size'] or digest(data) != manifest['archive']['sha256']:
            raise RuntimeError('Downloaded Spout reference archive size/checksum mismatch')
        archive.write_bytes(data)
    if archive.stat().st_size != manifest['archive']['size']:
        raise RuntimeError('Spout reference archive size/checksum mismatch')
    with archive.open('rb') as source:
        data = source.read(manifest['archive']['size'] + 1)
    if len(data) != manifest['archive']['size'] or digest(data) != manifest['archive']['sha256']:
        raise RuntimeError('Spout reference archive size/checksum mismatch')
    source_root = manifest['archive']['root']
    inputs = {}
    with zipfile.ZipFile(io.BytesIO(data)) as source:
        for name in source.namelist():
            if not name.startswith(source_root):
                continue
            relative = PurePosixPath(name.removeprefix(source_root))
            selected = (relative.as_posix() in {'MD/bin/SpoutDX.dll', 'MD/lib/SpoutDX.lib'} or
                        (relative.as_posix().startswith('include/SpoutDX/') and relative.suffix == '.h'))
            if not selected:
                continue
            if relative.is_absolute() or '..' in relative.parts:
                raise RuntimeError('Unsafe reference SDK archive member')
            if relative.as_posix() in inputs:
                raise RuntimeError('Duplicate reference SDK archive member')
            inputs[relative.as_posix()] = source.read(name)
    if not {'MD/bin/SpoutDX.dll', 'MD/lib/SpoutDX.lib', 'include/SpoutDX/SpoutDX.h'} <= inputs.keys():
        raise RuntimeError('Reference SDK inputs are incomplete')
    dll = inputs['MD/bin/SpoutDX.dll']
    offset = struct.unpack_from('<I', dll, 0x3c)[0]
    if (dll[:2] != b'MZ' or dll[offset:offset+4] != b'PE\0\0' or
            struct.unpack_from('<H', dll, offset+4)[0] != 0x8664 or
            struct.unpack_from('<H', dll, offset+24)[0] != 0x20b):
        raise RuntimeError('Reference SpoutDX DLL is not native x64 PE32+')
    license_path = output / 'LICENSE.spout2'
    if license_path.exists():
        license_data = license_path.read_bytes()
    else:
        with urlopen(manifest['licenseUrl'], timeout=30) as response:
            license_data = response.read(65537)
    if len(license_data) > 65536 or digest(license_data) != manifest['licenseSha256']:
        raise RuntimeError('Reference source license checksum mismatch')
    inputs['LICENSE.spout2'] = license_data
    # Check every existing generated input before writing, preserving investigation edits.
    for relative, contents in inputs.items():
        target = (output / relative).resolve()
        if not target.is_relative_to(output):
            raise RuntimeError('Reference SDK output escapes its fixture directory')
        if target.exists() and target.read_bytes() != contents:
            raise RuntimeError(f'Refusing to overwrite modified reference input: {target}')
    records = []
    for relative, contents in sorted(inputs.items()):
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(contents)
        records.append({'file': relative, 'sha256': digest(contents)})
    report = {'schemaVersion': 1, 'evidenceClass': 'fixture-input-validation', 'status': 'prepared',
              'sdkVersion': manifest['sdkVersion'], 'sourceCommit': manifest['sourceCommit'],
              'architecture': 'x64', 'runtime': manifest['runtime'], 'sdkExecutionTested': False,
              'archiveSha256': manifest['archive']['sha256'], 'files': records}
    (output / 'inputs.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output': str(output), **report}, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, struct.error, zipfile.BadZipFile) as error:
        print(f'Reference fixture preparation failed: {error}', file=sys.stderr)
        sys.exit(1)
