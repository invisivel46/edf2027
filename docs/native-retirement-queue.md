# Resource retirement queue ownership boundary

September 10, 2026. This concerns the setter retirement records rooted through
device+13140/+13144/+13148/+13152, not an assertion that all worker command
lists have the same format or ownership. Ghidra exports and generated bodies
were inspected; six allocator/fence seeds were added and headless analysis
completed. Unrelated incomplete-control-flow warnings remain in the project.

`82141440` requests 502 words aligned to 128 via `8213C328`, then links a
2008-byte block. The first block's address goes to descriptor+112, where the
descriptor is device+13140; subsequent blocks link from the previous block's
word zero. The old block's word+4 records its used eight-byte entry count while
preserving bit 31. Entries occupy [block+8, block+2008). The allocator-error
route uses device+15152 after flag 0x40 at device+10809 is set.

`8213C328` selects storage as follows (exact arguments require instructions;
Ghidra omits some in the displayed prototypes):

- No descriptor at device+13140: `8213C120` allocates from the device ring,
  with generation/wrap and current in-flight limits.
- Descriptor+152 nonzero: `82141340` allocates backward in descriptor-provided
  storage for mode 1, aligning the result and adjusting device+13496/+44/+48.
- Descriptor+152 zero: `821415A8` clamps requested bytes to descriptor+168 and
  calls the function at descriptor+172 with descriptor+164 as context, mode,
  requested-size pointer and alignment. It wraps that callback with the device
  critical-section exit/enter imports. Its target/lifetime is still unresolved.

The allocator also advances device+13512 byte accounting, changes command-space
limits under pressure, and sets the error flag when allocation returns zero.
Simply allocating a native vector would omit these shared allocator effects.
`82141500` separately links blocks through descriptor+116; they are not the
same head as the setter retirement records. `8213C928` is a fence wait/flush
helper, not itself the discovered retirement-record consumer.

Still required: identify the descriptor+112 traversal and record tag semantics,
descriptor publication/reset/destruction, callback configuration, and actual
completion ordering. The preserved setter tag bit cannot be dropped merely
because it comes from an uninitialized-looking stack slot. This audit changes
the next target from setters to descriptor-owned queue lifecycle; it does not
prove any queue is unused or authorize skipping its records.

## Descriptor reader follow-up

Expanded the literal device+13140 load search to every generated recompilation
unit, not just the 8213/8214 graphics range. The six matching functions are
8213C328, 8213CC20, 8213CDC0, 82141340, 82141440 and 82141500. This is a
literal-instruction inventory, not an alias or reachability proof: indexed
accesses, adjusted pointers, bulk copies and external consumers remain possible.
No direct `stw ...,13140(...)` appeared in that search; this does not prove the
descriptor stays null. Similar numeric offsets in other structures are not
evidence of device initialization.

Added 8213CC20 and 8213CDC0 to GeometryWriterTrace and completed headless Ghidra
analysis successfully. Checked the decompilation against generated instructions:

- 8213CC20 obtains new command storage with allocator mode 2 and alignment 32.
  It selects the same descriptor/default allocation routes as the mode-1
  retirement-block allocator. A configured descriptor callback can raise the
  request to device+13492. Successful allocation resets device+13496/+13512,
  sets command cursors/limits, and can wait on device+13520 before clearing it.
- 8213CDC0 is a command-range producer. When device+10808 bit 0x80 is set,
  device+13140 is nonzero and descriptor+152 is zero, it appends an
  eight-byte converted-address/word-count pair through device+13160/+13164,
  growing via 82141500. Thus descriptor+116's list carries command ranges;
  it is not evidence of descriptor+112 retirement consumption. Other routes
  emit worker opcode 0x82000000 or submit via 8213C868.

The examined worker offsets at 8214E8E0 (+108 synchronization flag) and
8214E640 (+116 copied worker state) likewise do not identify this descriptor's
retirement list. Do not merge their layouts based on matching offsets.

Next step: bounded live observation of setter retirement branch selection and
descriptor identity, including active-fence versus deferred-record paths. This
will prioritize the actual lifetime path without assuming the unresolved queue
is active or unused. Static descriptor+112 consumer/tag/alias tracing remains
required for any exercised deferred path. No runtime code or comparison policy
was changed by this follow-up, and no new game build or performance claim follows.

## Branch observation implemented

`--edf_native_retirement_audit=true` (launcher switch `-RetirementAudit`) samples
the actual native stream/index setter retirement branch after its lifetime writes
complete, before replacement binding publication. Branch IDs: 0 empty previous
binding, 1 fence update, 2 no matching retirement mask, 3 deferred queue record.
Value is the fence for branch 1 and the encoded record tag for branch 3; cursor
is the written record address for branch 3. Counts are separate per setter kind
and branch, logged for the first eight occurrences and subsequent powers of two.
They are checkpoints, not final totals. Descriptor identity is read only for
logged samples; observations do not establish its value between samples.

The option defaults off: no diagnostic counters or additional descriptor reads
are taken then. The shared retirement template supplies the already-used branch
values to the observer rather than selecting a branch again from mutable memory.
Mocked production-template tests cover one observation per branch, fence/record
write completion before observation and old binding preservation until afterward.
The full native target rebuilt and all 21 tests passed in 12.28 seconds. Runtime
coverage is recorded separately; these tests alone do not establish exercised
queue paths, descriptor lifetime or concurrent writer exclusion.

### Live branch result

Run `out/native-bridge-run/binding-validation-20260910-204949-0832da56/`, executable
SHA256 `1D6EAD6F86D8046FF9D2D1F65C31C6320851A4211D956AE23B76069ADAF3EB05`,
used a fresh profile with RetirementAudit enabled. Inspected `gameplay.png` shows
the player, NPCs and HUD in Mission 1 under the mothership (AF14 120/120).

Both stream and index fence branches reached the 1,048,576 checkpoint at
20:51:47.399. The log contains 26 sampled fence rows for each setter, eight
empty-binding stream rows and four empty-binding index rows. No branch 2 or 3
rows appeared; because each branch logs its first occurrence, this supports no
observed unmasked/deferred branch in these two native setters during this run,
not absence in other producers or missions. Every sampled descriptor was zero
on device 40002780. It is not an all-access watch on descriptor writes.

At 20:52:08.986, indexed telemetry reports 4,858,000 submissions, 718 builds,
379 resident entries and zero upload errors or VB/IB mismatches. No error,
critical or audit-read-failure rows were found before shutdown. This was a
startup/intro/gameplay-scene check, not a full mission, combat or FPS benchmark.
Owned PID 8104, start 20:49:49, was stopped after exact path/start verification;
WaitForExit returned true and the process was absent afterward.

This changes the immediate migration priority: follow the exercised resource+8
fence consumers and native completion/wait ownership instead of blocking active
resource migration on the unexercised descriptor queue. In particular, the
current 8213C928 bridge still executes its original CPU wait body even though
native signal capture/submission and an entry-policy differential fixture exist.
The deferred queue must still be understood or explicitly handled before claiming
complete replacement; this bounded run does not authorize deleting it. Per-draw
geometry comparisons remain unchanged.

Follow-up implementation: [native-resource-fence-wait.md](native-resource-fence-wait.md)
records replacement of the 8213C928 native-host control flow, differential tests,
live regression and the remaining wait-service ownership boundaries.
