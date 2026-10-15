#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Print only one milestone and its acceptance contracts for a focused Codex task."""
from pathlib import Path
import argparse
import json

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('milestone',help='For example M00 or M08')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
data=json.loads((root/'docs/backlog.json').read_text(encoding='utf-8'))
selected=next((m for m in data['milestones'] if m['id']==args.milestone.upper()),None)
if selected is None:
    parser.error('Unknown milestone; inspect docs/backlog.json.')
result=dict(milestone=selected,dependencies=[dict(id=m['id'],title=m['title'],status=m['status']) for m in data['milestones'] if m['id'] in selected['depends_on']],scenarios=[a for a in data['acceptance_scenarios'] if a['id'] in selected['acceptance']])
print(json.dumps(result,indent=2,ensure_ascii=False))
