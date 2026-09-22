> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Setter family expansion

The previous batch's 24 tested setter contracts are accepted as partial local
knowledge through `renderer-setter-acceptance.json`. The acceptance archives its
analyzer, harness, selection and test evidence before extending the live tools.
It continues to verify the current original function bodies and memory macros;
future analyzer development does not rewrite the historical evidence.

`python tools/rank-renderer-setter-families.py` produces the ranked backlog from
that fixed snapshot. All 63 previously unsupported functions appear exactly once
in 25 structural groups. Each group has a task, action, dependencies and completion
test in `out/renderer-automation-pilot/setter-backlog.json`; the readable ranking
is [renderer-setter-family-backlog.md](renderer-setter-family-backlog.md).
Mnemonic grouping retrieves candidates; it does not establish equivalence.

The selected group contains 12 functions, the largest straight-line integer
family. Its full-body pattern is:

```
lwz; rlwinm; li; rlwinm; rldicr; or; stw; ld; or; std; blr
```

The rule checks encodings, operand registers, displacements, masks, shifts,
condition-register behavior and exact return/boundary agreement. It writes one
32-bit field, then loads and updates a 64-bit dirty flag. Final r10, r11 and r12
are part of the contract. Other context bytes and unrelated memory stay unchanged.

The wrapping `rlwinm` in this family produces observable upper register bits in
the PPC64 representation. The harness models the 64-bit destination mask with
an independent bit loop and verifies the full register context. Comparing only
the 32-bit store would miss this. Dirty-mask construction is independently
evaluated from the immediate, doubleword rotation and mask fields.

The v1 fixture remains unchanged. The v2 fixture separately attests the exact
12 additional bodies against the pinned natural Ghidra export. Offline execution
uses those reviewed frozen attestations plus current original-body hashes; it
does not rerun Ghidra. Full-facts verification remains available:

```powershell
python tools/renderer_setter_analysis.py --ghidra-facts out/renderer-automation-pilot/ghidra/facts.json
.\validate-renderer-offline.cmd
python tools/rank-renderer-setter-families.py
```

No native replacement, receiver population, lifetime, mode, concurrency or
gameplay obligation is completed by the family rule. The nine floating/stack-store
functions excluded by the earlier high-p-code selection and the shared-tail
function remain separate investigations. Their unresolved status is deliberate.

## Results on 2026-09-22

The four setter rules now cover 36 of 87 candidates, leaving 51 unsupported.
The full offline gate passed all 24 suites in
`out/renderer-offline/20260922T171323.114719Z/report.json`. The setter harness
executed 21,384 invocations with 334 negative controls and 34 separate synthetic
alias-oracle checks in 0.631716 seconds. Nine accepted functions are in the
deterministic reserved partition, which is not a blinded benchmark.

The tally accepts the earlier 24 setters and now records 468 partial functions
and 8,748 untriaged. The new 12 are tested local-contract proposals, not an
additional tally acceptance. No whole function or broad boundary was closed.

Independent Sol review caught the need to keep v1's 24-body attestation separate
from the expanded 36-body output; the validators now enforce both generations
independently. The v2 classifier additionally restricts acceptance to the exact
12 reviewed instruction bodies. Applying the decoder to a new member requires
a reviewed fixture extension, not merely a matching mnemonic pattern.
