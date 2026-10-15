#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the planning contract; this does not qualify application behavior."""
from pathlib import Path
import argparse
import json
import os
import re
import sys

STATUSES = {'planned', 'in_progress', 'blocked', 'complete'}

def owned_markdown(root):
    """Exclude generated output and downloaded dependency documentation."""
    excluded = {'.git', 'out', 'build', 'node_modules', '.venv', 'vcpkg_installed'}
    for current, directories, files in os.walk(root):
        directories[:] = [name for name in directories if name not in excluded]
        for name in files:
            if name.endswith('.md'):
                yield Path(current) / name

def validate(data, root, check_documents=True):
    errors = []
    def require(condition, message):
        if not condition:
            errors.append(message)
    milestones = data.get('milestones', [])
    scenarios = data.get('acceptance_scenarios', [])
    requirements = data.get('requirements', [])
    require(data.get('primary_platform') == 'windows-x64', 'Primary platform must be explicit windows-x64.')
    require(bool(milestones), 'No milestones found.')
    mids = [m.get('id') for m in milestones]
    aids = [a.get('id') for a in scenarios]
    rids = [r.get('id') for r in requirements]
    for name, ids in [('milestone', mids), ('acceptance', aids), ('requirement', rids)]:
        require(len(ids) == len(set(ids)), f'Duplicate {name} ID.')
        require(None not in ids, f'Missing {name} ID.')
    mmap = {m['id']: m for m in milestones if 'id' in m}
    aset = set(aids)
    all_commits = []
    covered_scenarios = set()
    for m in milestones:
        mid = m.get('id', '?')
        require(bool(re.fullmatch(r'M\d{2,}', mid)), f'Invalid milestone ID: {mid}.')
        require(m.get('status') in STATUSES, f'{mid}: invalid status.')
        require(bool(m.get('goal')), f'{mid}: missing goal.')
        require(bool(m.get('owned_paths')), f'{mid}: missing path ownership.')
        require(bool(m.get('acceptance')), f'{mid}: missing acceptance.')
        require(bool(m.get('commits')), f'{mid}: missing commit slices.')
        for dep in m.get('depends_on', []):
            require(dep in mmap, f'{mid}: unknown prerequisite {dep}.')
            require(dep != mid, f'{mid}: depends on itself.')
        for aid in m.get('acceptance', []):
            require(aid in aset, f'{mid}: unknown scenario {aid}.')
            covered_scenarios.add(aid)
        for path in m.get('owned_paths', []):
            require(not Path(path).is_absolute() and '..' not in Path(path).parts, f'{mid}: unsafe relative ownership path {path}.')
        for i, c in enumerate(m.get('commits', []), 1):
            cid = c.get('id')
            all_commits.append(cid)
            require(cid == f'{mid}.C{i:02}', f'{mid}: non-sequential commit ID {cid}.')
            require(bool(c.get('outcome')), f'{cid}: no observable outcome.')
            require(bool(re.match(r'^(build|feat|fix|perf|test|ci|docs|refactor|chore)\(m\d+\): .+', c.get('subject', ''))), f'{cid}: invalid proposed Conventional Commit subject.')
        if m.get('status') == 'complete' and check_documents:
            require((root / 'docs' / 'progress' / f'{mid}.md').is_file(), f'{mid}: complete without a progress evidence file.')
    require(len(all_commits) == len(set(all_commits)), 'Duplicate commit slice ID.')
    require(covered_scenarios >= aset, 'Acceptance scenarios exist without any owning milestone: ' + ', '.join(sorted(aset-covered_scenarios)))
    visiting, visited = set(), set()
    def visit(mid):
        if mid in visiting:
            errors.append(f'Prerequisite cycle reaches {mid}.')
            return
        if mid in visited or mid not in mmap:
            return
        visiting.add(mid)
        for dep in mmap[mid].get('depends_on', []):
            visit(dep)
        visiting.remove(mid)
        visited.add(mid)
    for mid in mmap:
        visit(mid)
    for r in requirements:
        rid = r.get('id', '?')
        require(bool(r.get('milestones')) and bool(r.get('acceptance')), f'{rid}: incomplete requirement traceability.')
        for mid in r.get('milestones', []):
            require(mid in mmap, f'{rid}: unknown milestone {mid}.')
        for aid in r.get('acceptance', []):
            require(aid in aset, f'{rid}: unknown acceptance {aid}.')
            owners = [mmap[mid] for mid in r.get('milestones', []) if mid in mmap]
            require(any(aid in m.get('acceptance', []) for m in owners), f'{rid}: {aid} is not covered by a linked milestone.')
    if check_documents:
        main = root / 'IMPLEMENTATION_PLAN.md'
        require(main.is_file(), 'Missing implementation plan.')
        content = main.read_text(encoding='utf-8') if main.exists() else ''
        for cid in all_commits:
            require(cid in content, f'Commit slice absent from human plan: {cid}.')
        for document in owned_markdown(root):
            text = document.read_text(encoding='utf-8')
            for match in re.finditer(r'\[[^\]\n]+\]\(([^)\s]+)\)', text):
                target = match.group(1).split('#', 1)[0]
                if not target or re.match(r'^[A-Za-z][A-Za-z0-9+.-]*:', target):
                    continue
                resolved = (document.parent / target).resolve()
                require(resolved.is_relative_to(root.resolve()), f'{document.relative_to(root)}: link escapes package: {target}.')
                require(resolved.exists(), f'{document.relative_to(root)}: broken link: {target}.')
        activation_path = root / 'ci' / 'activation.json'
        require(activation_path.is_file(), 'Missing CI activation contract.')
        if activation_path.exists():
            activation = json.loads(activation_path.read_text(encoding='utf-8'))
            for item in activation.get('templates', []):
                require((root/item['source']).is_file(), f'Missing template {item["source"]}.')
                require(item.get('activate_at') in mmap, 'Unknown CI activation milestone.')
                require(item['source'].endswith('.yml.in'), 'Inactive workflow template must end in .yml.in.')
        for workflow in list((root/'.github/workflows').glob('*.yml')) + list((root/'ci/templates').glob('*.yml.in')):
            text = workflow.read_text(encoding='utf-8')
            for line in text.splitlines():
                match = re.search(r'\buses:\s*([^\s#]+)', line)
                if match and not match.group(1).startswith('./'):
                    require(bool(re.fullmatch(r'[\w.-]+/[\w./-]+@[0-9a-f]{40}', match.group(1))), f'{workflow.name}: action not pinned to full commit: {match.group(1)}.')
            require(not re.search(r'^\s*pull_request_target\s*:', text, flags=re.M), f'{workflow.name}: unsafe privileged PR trigger.')
            require(not re.search(r'continue-on-error:\s*true', text), f'{workflow.name}: blanket ignored failure.')
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        data = json.loads((root/'docs/backlog.json').read_text(encoding='utf-8'))
        errors = validate(data, root)
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(f'Invalid planning data: {exc}', file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print('ERROR:', error, file=sys.stderr)
        return 1
    print(json.dumps(dict(result='passed',milestones=len(data['milestones']),commit_slices=sum(len(m['commits']) for m in data['milestones']),requirements=len(data['requirements']),acceptance_scenarios=len(data['acceptance_scenarios']),scope='planning contract only'),indent=2))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
