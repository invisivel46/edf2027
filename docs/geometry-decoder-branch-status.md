# Decoder branch: evidence and remaining limits

This branch was reached while tracing generic file reads that might alias model
geometry. It is not itself a complete geometry-writer inventory, and it does not
authorize disabling steady vertex/index comparisons.

## Established on inspected paths

- The cache reader can expose input through `82438B38`; `82460238` retains it.
- Immediate bit-reader consumption loads that input and updates reader state.
- Of 146 indexed direct output calls, 139 have stack destinations; four forward
  an output whose two direct callers supply stack locals; three target owner
  fields. The three fields have local constructor/dispatch provenance.
- Processing and codec objects, channel arrays, per-channel array slices,
  nested descriptor buffers and matrix buffers have traced non-physical heap
  allocation paths. See the buffer-provenance CSV for qualifications.
- The inspected parser setup does not install the optional refill callback.
- Matrix and nested-descriptor cleanup paths release allocations and clear their
  owner slots. Clearing a slot does not prove absence of other aliases.

## Not established

- Whole-program coverage of computed/rebased stores, indirect callers or writes
  performed by external providers.
- All later replacements of retained pointers, owner pointers and callbacks.
- All consumer bounds, integer-overflow conditions, object modes and failure paths.
- Instruction-complete coverage of every fragmented switch in this branch.
- Ownership of every scratch allocation and every eventual decoded-result copy.

No zero-hit runtime sample or call-index count closes these gaps. Do not revisit
the already documented initial allocations unless new evidence contradicts them;
new analysis should name the specific remaining alias or writer it resolves.

## Evidence indexes

- `geometry-cache-consumers.csv`: direct cache-reader consumers.
- `geometry-bit-reader-output-review.csv`: manual review of exceptional outputs.
- `geometry-decoder-owner-chain.csv`: local owner-preserving call chain.
- `geometry-decoder-buffer-provenance.csv`: buffer sources and qualifications.
- `geometry-decoder-shared-pointer-candidates.csv`: same-offset writes separated
  by actual object type.
- `geometry-writer-enumeration.md`: detailed chronological address evidence.
