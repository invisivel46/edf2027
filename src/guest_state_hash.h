// EDF2027 - guest state hash trace: the exactness gate for guest codegen changes
// (docs/codegen-overhead.md, "Exactness gate").
//
// With --edf_guest_hash_trace=<csv> the engine thread hashes every committed guest page
// (virtual heaps, the XEX image heap and the physical heaps, each physical page once)
// after each step dispatch (the sub_821A4BA0 hook, frame.cpp) and appends one CSV line:
// the dispatch sequence number, the simulation tick, the steps just run, the bytes hashed,
// the whole-memory hash and one hash per heap. Two runs of one seeded scenario then compare
// line by line (tools/compare-guest-hash-trace.py): the first differing line is the first
// step whose guest state differs, and --edf_guest_hash_dump_at=<seq,...> writes per-chunk
// hashes at chosen dispatches so a divergence can be pinned to address ranges.
//
// --edf_deterministic_steps=true makes the engine heartbeat grant exactly one simulation
// tick per heartbeat, with no wall-clock wait (the retail clock words 0x8257C300/0x8257C308
// then hold a virtual tick count, not wall time), so the number of steps and the game clock
// no longer depend on host speed. Everything here is off by default and costs nothing then.
#pragma once
#include <cstdint>

namespace edf::gate {
// Called on the engine thread around every step dispatch (frame.cpp, sub_821A4BA0), with the
// steps being dispatched. BeforeStepDispatch returns a start stamp for --edf_step_timing.
uint64_t BeforeStepDispatch();
void AfterStepDispatch(uint8_t* base, uint32_t steps, uint64_t started);
// --edf_deterministic_steps.
bool DeterministicSteps();
}  // namespace edf::gate
