// EDF2027 - input-to-photon latency: the low-latency switch (edf_low_latency) and the
// optional trace (edf_native_input_latency_trace, input_latency_logic.h).
//
// Every On* call is a no-op unless the trace is on; they are cheap enough to leave in the
// input, engine, render and presenter paths. Any thread.
#pragma once

#include <cstdint>

#include "input_latency_logic.h"

namespace edf::latency {

// edf_low_latency: the presenter waits for the swap chain's frame-latency object on its
// ticker thread before a paint (just in time, and never on the UI thread, which delivers
// input) and shows the newest scene image; the scene producer keeps at most one image
// ahead of the presenter with VSync on and one frame of GPU work in flight; the soldier's
// aim takes the mouse motion that arrived after the step's pad poll too.
bool LowLatency();
// edf_native_input_latency_trace.
bool Enabled();
int64_t NowNs();

void OnInput(InputKind kind);
// A pad poll gave the game its input (or withheld it: OnInputDiscarded).
void OnInputConsumed();
void OnInputDiscarded();
// The engine heartbeat's tick (native_loop_budget.tick): the label of the steps it dispatches.
void OnHeartbeat(uint64_t tick);
void OnFrameRecorded(uint64_t tick);
void OnFrameSubmitted(uint64_t sequence);
void OnFrameAcquired(uint64_t sequence);
// The presenter's paint [paint_begin, present_end] ran on the UI thread and ended in DXGI
// present `present_id` (0 when unknown).
void OnFramePresented(uint64_t present_id, int64_t paint_begin, int64_t present_end);
// DXGI frame statistics: present `present_id` was put on screen at QPC time `sync_qpc`.
void OnPhotonQpc(uint64_t present_id, int64_t sync_qpc);
// The frame-latency waitable object returned at `t` after blocking (the estimated photon of
// the presents made before it).
void OnDisplaySignal(int64_t t);
// The presenter's just-in-time wait (edf_low_latency) returned at `t` after blocking: a flip.
// Feeds DisplayRefreshNs, and the trace's photon estimate when it is on.
void OnDisplayFlip(int64_t t);
// The display's refresh period seen by the just-in-time wait; 0 until known.
int64_t DisplayRefreshNs();

}  // namespace edf::latency
