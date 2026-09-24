// EDF2027 console - the engine-thread entry points, for the step dispatcher hook
// (src/native_graphics/edf/hooks/frame.cpp, sub_821A4BA0). Kept free of other headers so
// the hook file needs nothing else from the console.
#pragma once

#include <cstdint>

struct PPCContext;

namespace edf::console {
// Before the dispatcher runs `steps` simulation steps: runs due console commands.
void EngineStepBegin(PPCContext& ctx, uint8_t* base, uint32_t steps);
// After it: step timing for "stats".
void EngineStepEnd();
}  // namespace edf::console
