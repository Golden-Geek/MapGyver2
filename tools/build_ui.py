#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproducible static editor build and owned output-file copy; no Node server."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]


def npm_command(*arguments):
    if os.name == 'nt':
        npm = shutil.which('npm.cmd')
        node = shutil.which('node')
        if not npm or not node:
            raise RuntimeError('Node.js and npm are required to build the editor')
        cli = Path(npm).parent/'node_modules/npm/bin/npm-cli.js'
        if not cli.is_file():
            raise RuntimeError('Cannot locate npm-cli.js beside npm.cmd')
        return [node, str(cli), *arguments]
    return ['npm', *arguments]


def run(command):
    print('+ '+' '.join(map(str, command)), flush=True)
    subprocess.run(command, cwd=ROOT/'ui', check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT/'out'):
        raise RuntimeError('Generated editor output must stay inside the workspace out directory')
    run([sys.executable, str(ROOT/'tools/generate_types.py'), '--check'])
    run(['node', '--version'])
    run(npm_command('--version'))
    run(npm_command('ci', '--ignore-scripts', '--no-fund', '--no-audit'))
    run(npm_command('run', 'check'))
    run(npm_command('run', 'build'))
    source = ROOT/'ui/dist'
    # Runtime notices, separate from the larger development dependency closure.
    notices = []
    for package in ('svelte', 'esm-env', 'clsx'):
        directory = ROOT/'ui/node_modules'/package
        metadata = json.loads((directory/'package.json').read_text(encoding='utf-8'))
        for notice in directory.iterdir():
            if notice.is_file() and notice.name.lower().startswith('license'):
                notices.append(package+' '+metadata['version']+'\n'+notice.read_text(encoding='utf-8'))
    (source/'THIRD_PARTY_NOTICES.txt').write_text('\n\n'.join(notices), encoding='utf-8')
    (source/'LICENSE.txt').write_bytes((ROOT/'LICENSE').read_bytes())
    current = []
    for file in sorted(source.rglob('*')):
        if not file.is_file():
            continue
        if file.is_symlink():
            raise RuntimeError('Generated editor assets cannot be symlinks')
        relative = file.relative_to(source)
        destination = output/relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(file.read_bytes())
        current.append(relative.as_posix())
    manifest = output/'build-files.json'
    previous = json.loads(manifest.read_text()) if manifest.exists() else []
    for relative in previous:
        target = (output/relative).resolve()
        if not target.is_relative_to(output):
            raise RuntimeError('Invalid previous editor asset manifest')
        if relative not in current and target.is_file():
            target.unlink()  # Only previously owned individual files, validated above.
    manifest.write_text(json.dumps(current, indent=2)+'\n')


if __name__ == '__main__':
    main()
