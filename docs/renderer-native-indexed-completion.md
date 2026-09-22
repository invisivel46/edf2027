# Native indexed CPU completion

The packet-free indexed wrapper now uses authored C++ in
`native_indexed_completion.cpp` and `native_indexed_completion.h`. CMake compiles
it into the game and the differential fixture under the existing
`edf_native_indexed_cpu_tail` ABI symbol. The generated indexed prefix is no
longer a build input; the original indexed routine remains the test oracle and
the unsupported-rendering fallback.

## Effects owned by the native wrapper

The wrapper snapshots all five dirty banks before invoking any helper. It clears
the two originally dirty constant banks first, then invokes the retained native
main-state chain when bank 16 intersects `15 << 49`. It clears the originally
dirty main-state bank only after that callback succeeds. For entry bank 24 bit 1,
it writes the sign-extended render signature `0xffffffffff000000` at device+11568,
then clears originally dirty banks 24 and 32.

This preserves an important entry-snapshot rule: state newly published by a helper
into an initially clean bank is left pending. A helper exception leaves the main
and later banks unconsumed; successful earlier constant consumption stays done.
The handoff layer continues to decide whether a native draw was accepted and
completion is owed. Zero accepted draws do not invoke this wrapper.

Packet-only encoders and the Xbox index-buffer draw loop are absent. The wrapper
does not read index resources, emit packets, or advance the command cursor. The
main-state callback still performs required derived-state and shader effects,
including the authored native shader-cache completion. It remains an extracted
adapter and is not claimed as replaced by this change.

The helper runs in a copied PPC context with its original 240-byte caller frame,
backchain and call-site LR. Caller stack, LR and nonvolatile registers are retained
even on an exception. There is no defined indexed-draw return value; native
completion leaves caller registers unchanged. Stack scratch is excluded from the
CPU projection, as in the existing tests.

## Verification

The existing original-indexed differential matrix covers primitive/count changes,
dirty/clean state, both index widths, command-buffer rollover and invalid native
index bindings. The shared static-group fixture covers no draw, one/multiple
draws, controlled material changes, fallback ordering and failed-tail retry.

Added tests compare each of 320 individual bits across all five banks against
original indexed execution, then repeat native completion to check idempotence.
The reference uses controlled audited packet-only providers (including the special
encoder's mask return); this matrix does not validate Xbox packet encodings.
Each comparison checks the full CPU/cache projection below `0xe0000`, excluding
only the command cursor and packet buffer. Retail packet/helper tracing must stay
zero on the native path. Four controlled callback cases check write order,
entry snapshots, helper-published state, partial failure and retry through the
same production template.

Both retained WARP replays, the game build and the offline gate are part of the
integration checks. No gameplay boot is required for this bounded change.
Commands: `cmd /c out\static-group-focused.cmd` and
`cmd /c out\static-group-integration.cmd`.
Final outcomes and hashes: `out/indexed-completion-result.json`; detailed output:
`out/indexed-completion-focused.log`, `out/indexed-completion-test-details.log`,
`out/indexed-completion-integration.log`.
Epistemic session: `session-20260922192307-33b13633`.

## Remaining work

The indexed prefix and shader-cache bookkeeping now have authored native owners.
The main-state, upload and derived-state adapters still execute extracted CPU
logic. Geometry setup and material activation also remain to be replaced or
justified before a zero-legacy static-group claim. Historical source-pinned atlas
and plan results must be refreshed before being treated as current-source audits.
