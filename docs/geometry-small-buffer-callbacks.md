# Small-buffer callback resolution

Scope: the object constructed by `8242E7E0`, not every possible caller of
`8242D2B0` or every runtime replacement of its vptr.

## Constructor evidence

The generated direct-call index identifies `8242C2E8` as the sole direct
caller of `8242E7E0`. At `8242C41C` it requests 376 bytes through `8242E668`,
then invokes the constructor at `8242C428`, retaining the result in r20.
At `8242C560` that value is passed to `8242DA08`, which sets object+0x38
to the device and calls buffer creation `8242D2B0` at `8242DA48`.
Intervening callees and indirect entries are not certified by this direct chain.

Ghidra and generated instructions agree: `8242E7E0` calls base initialization,
stores **0x82064C78 at object+0 at 8242E804**, then calls `8242E678`.
The inspected initializers `8242D100`, `8242CEE0`, `82433B58` and `8242E678`
initialize fields without replacing that installed vptr. `824338F8` initializes
list/refcount fields and the critical section at object+0x10.

Reading big-endian words from `guest_image.bin` at the installed table gives:

| Slot | Table location | Target | Inspected effect |
| --- | --- | --- | --- |
| 0x3c | 82064CB4 | 8242D0B0 | Calls slot +8 on object+0x2c's referenced object, then clears +0x2c |
| 0x58 | 82064CD0 | 8242E678 | Resets base fields, inline 120-byte source at +0x90, and extended fields |
| 0x60 | 82064CD8 | 8242EF88 | Creates three returned handles using fixed image addresses; stores them at +0x48/+0x144/+0x148 |

This table is supported by an actual vptr store, unlike the alternating
function/metadata records around 0x82097C00.

## Writer consequences and limits

`8242D2B0` calls slot +0x60 at `8242D420` after publishing its IB/VB handles.
For the installed table this reaches `8242EF88`. Its calls at `8242EFAC`,
`8242EFC0`, `8242EFD4` pass fixed addresses `82064770`, `82064888`, `82064A80`
to `8214AFD0`/`8214AED8`. Its only direct non-stack stores publish returned
handles (`8242EFB4`, `8242EFC8`, `8242EFDC`); it does not load the VB/IB
handles or pass a geometry payload pointer. Callee/global effects remain separate.

The cleanup slot +0x58 reaches a reset of the object's inline source bytes,
not a dereference of the VB/IB payload. The +0x3c cleanup method introduces
a further indirect call through the referenced object at +0x2c. That object's
concrete type/lifetime and slot +8 remain to be resolved.

The actual VB update remains `8242D440`: copy 120 bytes from object+0x90
through a VB lock. Resetting its inline source is not a direct write into an
already uploaded VB, but later updates can copy the changed source.

Eight new Ghidra targets were exported (277 total seeds); both headless runs
exited zero and saved. The known physical-free import warning remains. These
findings do not remove runtime geometry comparisons or establish all aliases.

## Retained-reference follow-up

`82064C78+0x38` contains `8242D048`. Its inspected setter calls the incoming
object's slot +4 at `8242D074`, invokes the renderer's cleanup slot +0x3c
at `8242D088`, and stores the incoming pointer to renderer+0x2c at
`8242D090`. This identifies a publication boundary, but not its caller-supplied
object type. Do not infer that type from the shared release implementation.

The coordinating object in `8242C2E8` is separately constructed by `82431840`
at `8242C340` and retained in r23. Its constructor installs `82064EE0` at
`82431864`. That table gives these concrete setup targets:

| Slot | Target | Role |
| --- | --- | --- |
| 0x24 | 82431150 | Dispatches setup through slots +0x28/+0x30/+0x34 |
| 0x28 | 82431140 | Setup prerequisite; body not reviewed here |
| 0x30 | 824312A0 | First setup branch; body not reviewed here |
| 0x34 | 82431390 | Replaces three retained references at coordinator+0x38/+0x3c/+0x40 |

At `8242C598`, the factory passes the small-buffer renderer in r7 to the
coordinator's +0x24 method. `82431150` saves that argument and forwards it
as r5 at the +0x34 call (`824311D4`). `82431390` retains it through its
slot +4, then stores it at coordinator+0x3c at `8243144C`. **This is a
reference to the renderer, not publication into renderer+0x2c.** Thus this
setup branch does not resolve the cleanup reference's identity.

Both installed tables have slot +8 = `82433978`. Instructions show that it
calls virtual slot zero with r4=1 when the observed refcount at +0xc is one;
otherwise it atomically decrements +0xc. Do not describe this as an atomic
decrement-to-zero test: the one-count branch precedes the atomic loop.
For the coordinator table, slot zero is `82431DD0`, which calls cleanup
`824311E0`, destroys three embedded objects via `82439B08`, calls `82433940`,
and conditionally frees the object through `8212F4B8` with flags `208C8031`.
Those nested cleanup effects are not closed by this wrapper inspection.

Remaining: find concrete callers of the renderer's +0x38 setter (including
virtual calls), trace their incoming pointers, and inspect final-release
effects for the resulting types. Six additional targets exported successfully
(283 seeds total); runtime comparisons remain unchanged.

## Coordinator consumer and escape review

Nine further bodies were exported (292 total seeds). The inspected method
bodies do not call the renderer's +0x38 reference setter; their callees and
external consumers are not thereby excluded.

| Coordinator method | Direct effect / remaining route |
| --- | --- |
| 824312A0 | Replaces retained pointers at +0x2c/+0x30/+0x34; does not assign renderer+0x2c |
| 82431480 | Calls renderer slot +0x48 at 824314FC, configures embedded buffers via 82439B48, stores timing/configuration fields |
| 824318F0 | Retains and returns coordinator+0x2c/+0x30/+0x34 through caller-provided outputs |
| 824319A8 | Retains and returns coordinator+0x38/+0x3c/+0x40 through caller-provided outputs |
| 82431A60 | Retains and returns embedded objects at +0x44/+0x8c/+0xd4 |
| 82431AF8 | Writes status and timing value through caller outputs |
| 82431B50 | Updates timing fields and caller output; invokes coordinator slot +0x74 |
| 824317B0 | Writes the 64-bit field at +0x120 through caller output |
| 824316C0 | Calls input objects' slot +0x40, resets three embedded buffers and timing fields |

The renderer alias has an explicit escape: `824319A8` loads coordinator+0x3c
at `824319FC`, retains it through slot +4 at `82431A18`, reloads the pointer,
and publishes it through entry r5 at `82431A20`. This is a pointer-value
store, not a write through the renderer's geometry payload. Its consumers
must be traced before treating the renderer as private to the coordinator.

For the installed renderer table `82064C78`, the configuration call at
`824314FC` resolves to `8242E978` (slot +0x48), not `8242D048` (slot +0x38).
The buffer-helper allocations and other configuration callee effects remain
separate; do not conclude immutability from absence of a direct payload store.

Likewise, the output pointers accepted by the getter/status methods are not
automatically stack-local. Their destination provenance remains caller-specific.
The next setter search must include consumers of the returned renderer alias,
not only methods on this coordinating object. Both headless runs exited zero
and saved; no runtime comparisons or game behavior changed.

## Getter-to-setter consumer found

The selected instruction paths below connect the two previously separate
boundaries, conditional on worker+0x2c holding the coordinator with table
`82064EE0` and its renderer retaining table `82064C78`. Worker construction,
later table replacement, and all indirect entries remain separate obligations.

| Consumer | Coordinator getter | Renderer setter | Incoming reference |
| --- | --- | --- | --- |
| 8243D6A0 | 8243D700: slot +0x40, r5=SP+0x50 | 8243D968: slot +0x38 on SP+0x50 | r30, returned by 824398D8 at 8243D8DC |
| 8243C4E0 (selected generated path only) | 8243C9B0: slot +0x40, r5=SP+0x5c | 8243C9C8: slot +0x38 on SP+0x5c | r21, returned by 824398D8 at 8243C7E0 |

In `8243D6A0`, a second coordinator call at `8243D73C` uses slot +0x44
with r5=SP+0x54. For the installed table this is `82431A60`, which returns
the embedded queue at coordinator+0x8c. `824398D8` takes that queue and
computes **queue[0x30] + 60 * queue[0x38]**, invokes the entry's slot +4,
and returns the entry address. It does not return a VB/IB payload pointer.
Thus the renderer's retained reference on this path is a 60-byte queue entry.
Its vptr construction, associated payload storage, and final-release behavior
are now the next concrete type/lifetime questions.

This is not a blanket exclusion of writes by the entry's methods: the worker
calls its slots +0x58/+0x38/+0x50 before assigning it, then other methods
after assignment. The numeric +0x38 slot on a queue entry must not be confused
with the same slot on the renderer.

Other candidate checks prevented incorrect attribution:

- `8243AD48` passes its stack output as r4 and zero as r5 to coordinator
  slot +0x40, selecting the first retained object, not the renderer.
- `8243A810` passes SP+0x58 as r5 at `8243A8D8`; it can obtain the renderer
  and invoke its control/lock/release methods. Its full callee effects remain open.
- `8243D148` likewise requests the first output (r4), not the renderer.
- `8243B710`'s +0x38 call at `8243B7FC` is on an entry obtained from
  `82439928`, with r4=1, not the renderer reference setter.

Seven new targets exported (299 seeds total). **New Ghidra limitation:**
`8243C4E0` reports incomplete control flow and its inferred body omits a
region between 8243CC9C and 8243CF27. Only the explicitly cross-checked
generated sequence above is used; the whole function is not certified.
The other selected exports completed and the runs saved with exit zero;
the older physical-free import warning also remains. No runtime checks removed.

Queue construction/release follow-up: the entry constructor installs table
820650F0, and its initial payload is a slice of separately allocated CPU-heap
queue storage. See [render queue ownership](geometry-render-queue-ownership.md)
for allocation flags, entry methods, conditional destruction and outstanding
reference-ordering limits. This closes the initial entry type/payload source
on the inspected path, not every writer or final-release scenario.
