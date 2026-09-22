"""Export one renderer task as a self-contained Markdown work packet."""
import argparse
import csv
import json
from pathlib import Path
from renderer_task_packet import ROOT, render_task

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('task',nargs='?')
parser.add_argument('--list',action='store_true')
parser.add_argument('--output',type=Path)
parser.add_argument('--rows',type=int,default=0,help='Include at most this many assigned boundary rows (0..20).')
parser.add_argument('--match',default='',help='Literal filter on subject/locator/action; applies only to row excerpts.')
args=parser.parse_args()
if not 0<=args.rows<=20: parser.error('--rows must be between0 and20')
catalog=json.loads((ROOT/'docs/renderer-coverage.json').read_text(encoding='utf-8'))
tasks={t['id']:t for t in catalog['tasks']}
if args.list:
    print('\n'.join(t['id']+' - '+t['title'] for t in tasks.values()))
    raise SystemExit(0)
if args.task not in tasks: parser.error('Choose a task ID from --list.')
output=render_task(tasks[args.task],include_workflow=True)
if args.rows:
    path=ROOT/'out/renderer-coverage/boundary-tasks.csv'
    if not path.is_file(): parser.error('Boundary ledger missing; regenerate with tools/build-renderer-coverage.py.')
    selected=[]
    with path.open(encoding='utf-8-sig',newline='') as f:
        for row in csv.DictReader(f):
            if row['task']!=args.task: continue
            if args.match.lower() not in (row['subject']+' '+row['locator']+' '+row['action']).lower(): continue
            selected.append(row)
            if len(selected)==args.rows: break
    output+='\n\n## Selected boundary excerpt (not the full task scope)\n\n'
    if not selected: output+='No matching boundary rows. Feature/runtime-only tasks may have no assigned boundary rows.\n'
    for row in selected:
        output+=f"- {row['id']}: `{row['source']}` row{row['row']}; `{row['locator']}`\n  Action: {row['action']}\n  Gate: {row['completion_test']}\n"
if args.output:
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(output+'\n',encoding='utf-8')
    print(str(args.output.resolve()))
else:
    print(output)
