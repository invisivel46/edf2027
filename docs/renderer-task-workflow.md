> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Working instructions for a renderer task

Work from `D:/roms2/edf2027` in PowerShell. The project is a Windows native
renderer around recompiled Xbox360 gameplay. Native GPU submission already
works, but many original render callbacks and CPU side effects remain. Your
assignment is one named task and one explicitly selected slice of its scope.
Do not start by redesigning the whole renderer.

For dispatched work, read the assigned packet from
`tools/dispatch-renderer.py` and follow `docs/renderer-dispatch.md`. Its bounded
slice and allowed files define your assignment; parent context is broader scope.
Do not edit the coordinator ledger or close a parent task from a slice result.

## Start

1. Read the selected task packet, its starting files, and the existing changes
   shown by `git status --short`. Preserve unrelated and uncommitted work.
2. Run `python tools/audit-renderer-coverage.py`. Require exit0 and `passed:true`
   before relying on current-source coverage. A green planning audit does not
   mean the renderer or your task is complete.
3. Use Epistemic: status, search for the task ID and selected symbols, then start
   your own session at the current commit/worktree version. Record observations
   and experiments as evidence. Interpretations remain hypotheses unless
   promotion succeeds. Checkpoint before handoff and close your own session.
4. Start with the packet's **first slice**. Record the selected source filename,
   source-row identity, symbol/callsite and source hash. Generated B-number IDs
   and line numbers can change after regeneration; they are not stable identity.
5. Check the listed prerequisite evidence for that slice. Dependencies name
   contracts produced by other tasks. Read-only investigation may proceed while
   those tasks are open; implementation may proceed only with the relevant
   evidenced contract. Do not demand classification of all9,216 functions before
   investigating one callback. Do not invent missing contracts to bypass a gate.

Use `rg -n` with the packet's symbols and named files. Search existing exported
Ghidra bodies under `out/ghidra/renderer-inventory` before rerunning analysis.
`generated/default` and generated adapters are evidence; make lasting changes
in native source or the appropriate extractor, not a generated body.

## Keep the unit of work bounded

A publication is a retained snapshot associated with a simulation tick or
generation. A producer updates game state; a consumer renders published state.
Retirement releases a resource only after the required users/completion have
finished. A local-effect review describes one function's own actions; transitive
effects include everything it calls. A receiver population is the set of actual
objects/targets that can reach an indirect call, not just plausible vtables.

For investigations, finish one connected function/callback/receiver contract at
a time. For implementation, finish one behavior and its regression cases before
expanding to another family or mode. The108,048 census rows include instructions,
shared utilities and duplicate evidence surfaces; they are not108,048 code
changes. A suggested first slice is a starting point, not permission to close
the whole task after one fixture.

If evidence runs out, stop changing that boundary, not all useful work. Save the
last proven facts, the precise missing writer/receiver/provider, searches already
tried, and the next bounded query. Continue independent steps within the selected
task. Never report a guessed contract, unvisited scenario or zero-check audit as
verified.

## Deliverables

Create a new directory `out/renderer-tasks/<task-id>/<unique-run>/`. Keep evidence
outside generated coverage files so regeneration cannot erase your results.

- `contract.csv`: source locator/hash, guard/mode/receiver, inputs, writes or
  outputs, owner, lifetime/ordering, evidence IDs, unresolved question.
- `results.md`: selected slice, starting revision, prerequisite evidence,
  changes/findings, exact commands and exit codes, every acceptance case marked
  passed/failed/not-run, remaining source rows, and next action.
- Tests/captures/traces used by the packet, with executable/source identity and
  actual scenario timing. Save a durable summary and essential artifacts through
  Epistemic; raw files only under `out/` are not the durable knowledge record.

For a proposed exclusion or producer-only disposition, include the exact mode,
registration/reachability or writer/reader evidence and why no render-consumer
change is required. Do not use an empty capture, no-op slot or missing log line
as that proof.

## Build and verify changes

The default per-change renderer regression gate is:

```powershell
.\validate-renderer-offline.cmd
```

It rebuilds and runs the curated offline suites, including the static-pass
replay, without booting the game. Attach its printed report path to your results.
See `docs/renderer-offline-validation.md` for scope, focused groups and milestone
boot policy. Add cases for the changed contract and run any applicable packet
tests not included in the offline gate. A passing replay does not complete a
runtime-only acceptance case. Ordinary changes do not require mission play;
group runtime checks at the documented integration milestones.

Run each command separately and inspect its exit code; do not let a later
successful command hide an earlier failure. Tests listed in a packet are
existing regression targets. Add the packet's new cases when changing behavior;
passing old tests alone does not establish its completion test. Research-only
or documentation-only work does not need a full game build.

From the configured Visual Studio developer environment:

```powershell
cmake --build out/build/win-amd64-release --target <packet-test-target> --parallel 2
ctest --test-dir out/build/win-amd64-release -R '^<packet-test-target>$' --output-on-failure --no-tests=error
```

The packet replaces those target placeholders with existing names. After a game
source change, also build `edf2027`. If the current shell lacks the configured
compiler environment, inspect `build.cmd` and the existing CMake cache first.
`cmd /c build.cmd win-amd64-release <target>` initializes Visual Studio but also
reconfigures with its SDK default. Preserve the existing SDK selection; do not
silently switch SDKs or edit machine setup to get a build through.

After hook/CPU-tail changes, run the hook-boundary audit:

```powershell
cmake -DSOURCE=D:/roms2/edf2027/src/native_graphics/guest_shader_bridge.cpp -P tools/audit-native-hook-boundaries.cmake
```

For runtime work, follow `docs/renderer-coverage-runtime.md` and the existing
launcher. Use a new output directory and isolated userdata. Record the effective
arguments and executable hash. An existing user game is not yours to stop.
Stop only a process you launched, after verifying its actual executable path.
Partial scene captures are not complete-frame captures; a delivered input is
not proof of its intended effect. Full visual parity requires a matched reference.

## Finish or hand off

Run `git diff --check` and the relevant checks. Coverage source hashes will change
after native code edits: inspect failures and regenerate the affected inventories
using their real generators, rather than editing hashes. For a source-only
refresh that changes no generated guest graph, the existing sequence begins with
`complete-renderer-inventory.py`, `inventory-native-reentry.py`, then
`resolve-renderer-inventory-sites.py`. The last step restores the Ghidra join
columns; omitting it breaks the audit. Other changed surfaces may require their
own generators. Use the failures to identify them; do not claim a stale audit pass.

After justified inventory refresh, run `python tools/build-renderer-coverage.py`
and `python tools/audit-renderer-coverage.py`. The catalog and guide are generated;
task descriptions are authored in `tools/renderer_task_playbooks.py`, and the
package scope/dependencies are in `tools/build-renderer-coverage.py`.

Report **slice complete**, **task complete**, or **incomplete**, with the evidence
for that exact scope. Task complete requires all its assigned boundaries and
acceptance cases, not just the first slice. This catalog is the original open
audit snapshot: preserve execution outcomes in `results.md` and Epistemic rather
than silently marking generated rows complete. A later status-ledger update must
cite those results and preserve every remaining obligation.

End with changed files, verified behavior, tests and exit codes, untested cases,
Epistemic IDs, and the next precise action. Do not claim full-renderer completion
from a task-local result.
