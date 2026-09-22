# Complete renderer census atlas

Open [the atlas](../out/renderer-atlas/index.html) locally. No service, game boot,
network access or LLM is required to browse it. The index searches function names
and root reasons, filters analysis lanes, recorded status and warnings, and links
to all 9,216 functions. Each page includes decompiled C, unchanged generated C,
raw instructions, instruction CFG edges, callers/callees, native source mentions,
global-symbol candidates, body ranges, provenance and its recorded task.

## Reproduce

```powershell
.\build-renderer-atlas.ps1
```

This uses the installed Ghidra 12.1.3 and existing `edf2027-geometry` project in
read-only mode. It defines census entries transiently, exports per-function facts
and C, retries timed-out decompilations with a bounded 120-second limit, builds
SQLite and static pages, then audits census, hashes and database integrity.
The default timeout is 30 seconds. Two initial timeouts completed on retry.

Per-function exports are cached under a context hash of the exporter, census and
Ghidra project files. A fresh export after a context change does not reuse old
facts. For rebuilding the database/pages from the current export alone:

```powershell
.\build-renderer-atlas.ps1 -ReuseExport
python tools/renderer_atlas.py audit
python tools/renderer_atlas.py search 'iRam8257bfb4'
python -m unittest tests.test_renderer_atlas
node tests/test_renderer_atlas_ui.cjs
python tools/check-renderer-atlas-links.py
```

`atlas.sqlite` has FTS5 search across decompiled/generated C, existing findings,
root reasons and native mentions. SQL tables include `functions`, `instructions`,
`cfg`, `calls`, `refs`, `global_symbols`, `native_refs`, `clusters`, `tasks` and
`metadata`. For example:

```sql
SELECT caller, site, kind, provenance FROM calls WHERE callee='sub_821BE8D0';
SELECT function, symbol FROM global_symbols WHERE address='8257BFB4';
SELECT symbol, lane, quality FROM functions WHERE status='untriaged';
```

## Current coverage

- 9,216 census functions, 9,216 generated bodies and 9,216 returned C outputs.
- 704,731 decoded instruction records; 109,281 call records across distinct
  generated-source and Ghidra provenance. These are not unique runtime calls.
- 6,674 unresolved call records; 389 named targets outside this census remain
  external references, not silently added as completed renderer work.
- 2,907 distinct decompiler global-symbol addresses; 207 functions have native
  source mentions. Mentions include declarations/comments as well as call sites.
- 28 dependency-cycle groups, 9 identical-byte groups, 626 structural-similarity
  groups and 486 shared-global-symbol groups. These support navigation and
  candidate reuse; they do not prove equivalence or identify semantic subsystems.

The existing task overlay assigns the census to `R01.scope`; its 511 partial and
8,705 untriaged statuses are retained. More specific native boundary routes and
findings are displayed separately. Feature/mode/scenario acceptance is available
from the coverage page. This atlas does not change project completion credit.

## Quality and limits

Returned C is not proof of a correct decompilation: 4,723 functions have recorded
quality flags and 162 contain `halt_baddata`. `sub_8223E418` has an explicit raw
instruction-coverage gap; its generated body and decompiler diagnostic remain
available. Its C output therefore does not turn that gap into a pass.

Instruction references in the existing project are primarily control-flow
references. Decompiler auto-named globals are indexed separately with their own
provenance; neither source enumerates every possible dynamically computed global.
CFGs are instruction-level, and indirect targets remain unresolved. A function's
presence in the reachable census is not proof it belongs in the replacement scope.

The exported Ghidra definitions matched the prior inventory's body sizes; this is
a consistency check, not independent proof of exact boundaries. The raw byte and
structural cluster hashes are candidates only: addresses, ownership and callees
can still make superficially identical bodies behave differently.

`manifest.json` pins source inputs, each export, the database and generated pages.
`audit.json` verifies these pins and census/task coverage. The browser-control tool
was unavailable during development; page logic, filtering, pagination and links
were tested programmatically, without claiming visual browser verification.
