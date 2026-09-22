"""Pure formatting shared by the generated guide and one-task CLI."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def render_task(task, include_workflow=False):
    e=task['execution']
    lines=[f"## {task['id']}: {task['title']}", '',
           f"**Kind:** {e['kind']}. **Audit status:** {task['status']}.", '',
           '**Goal:** '+task['change'], '',
           '**Scope dependencies:** '+(', '.join(task['dependencies']) or 'None')+'.', '',
           '**Prerequisite evidence for this slice:** '+e['prerequisite_evidence'], '',
           '**Start with:** '+e['first_slice'], '', '**Read these files:**', '']
    lines += ['- `'+p+'`' for p in e['input_paths']]
    lines += ['', '**Find the relevant code/evidence:**', '', '```powershell',
              "rg -n '"+e['search_symbols']+"' "+' '.join(e['input_paths']), '```', '',
              '**Steps:**', '']
    lines += [f'{i}. {step}' for i,step in enumerate(e['steps'],1)]
    lines += ['', '**Required outputs:**', '']+['- '+v for v in e['deliverables']]
    lines += ['', '**Cases to add or demonstrate:**', '']+['- '+v for v in e['acceptance_cases']]
    if e['test_targets']:
        lines += ['', '**Existing regression targets (run after the relevant changes):**', '', '```powershell']
        for target in e['test_targets']:
            lines += [f'cmake --build out/build/win-amd64-release --target {target} --parallel 2',
                      f"ctest --test-dir out/build/win-amd64-release -R '^{target}$' --output-on-failure --no-tests=error"]
        lines += ['```', '', 'Check each exit code. These existing tests are a baseline; the new cases and completion gate below still apply.']
    else:
        lines += ['', '**Verification:** inspect the evidence rows against source and the cases above; run the coverage validator. No unrelated game build is required for a read-only result. Runtime-validation tasks additionally require their scenario/measurement artifacts.']
    lines += ['', '**Task completion gate:** '+task['completion_test'], '',
              '**Do not:**', '']+['- '+v for v in e['do_not']]
    lines += ['', '**If blocked or uncertain:** '+e['stop_condition'], '']
    if include_workflow:
        lines += ['---','',(ROOT/'docs/renderer-task-workflow.md').read_text(encoding='utf-8')]
    return '\n'.join(lines)

def render_guide(tasks):
    intro=['# Renderer task execution guide', '',
           'Generated from the authored playbooks. Read [the common workflow](renderer-task-workflow.md) before executing a task. The audit status and task IDs are preserved; detailed instructions are not new implementation evidence.', '',
           'For a self-contained packet containing only one task and the common workflow:', '',
           '```powershell', 'python tools/show-renderer-task.py R10.capture --output out/R10.capture-task.md', '```', '',
           '`python tools/show-renderer-task.py --list` lists task IDs. Add `--rows 5` and optional `--match 821A5158` to include a small matching boundary excerpt; samples do not limit task scope. Research inputs under `out/` must already exist or be rebuilt from the inventory workflow.', '',
           'A useful standalone implementation starting point is R10.capture. R01.scope starts the bounded research path; follow its evidence into R01.callbacks/R09.effects. The full dependency graph remains in the catalog.', '',
           '| Task | Kind | First bounded slice |', '|---|---|---|']
    for t in tasks:
        # Explicit anchors avoid punctuation-dependent Markdown slugs.
        intro.append(f"| [{t['id']}](#{t['id'].replace('.','-').lower()}) | {t['execution']['kind']} | {t['execution']['first_slice']} |")
    return '\n'.join(intro)+'\n\n'+'\n\n'.join(
        f'<a id="{t["id"].replace(".","-").lower()}"></a>\n\n'+render_task(t) for t in tasks)+'\n'
