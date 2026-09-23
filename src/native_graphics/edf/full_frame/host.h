#pragma once
// The full frame (native_full_frame.h) as the 821A5080 hook runs it: the five scene passes, each made
// through its factory (<pass>_pass.cpp), and the host, NativeFullFrameHost (host.cpp), which
// RunNativeFullFrame builds and runs for one render helper call.
#include "full_frame_shared.h"
#include "../../native_full_frame.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace edf::native {
// The passes (the classes are local to their files).
std::unique_ptr<NativeFramePass> MakeNativeFullFrameStaticWorldPass(uint8_t* base);
std::unique_ptr<NativeFramePass> MakeNativeFullFrameModelsPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared);
std::unique_ptr<NativeFramePass> MakeNativeFullFrameSkyPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared);
std::unique_ptr<NativeFramePass> MakeNativeFullFrameEffectsPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared);
std::unique_ptr<NativeFramePass> MakeNativeFullFrameTransparentPass(uint8_t* base,std::shared_ptr<NativeFullFrameModelsShared> shared);
// A remaining guest call: function, object (r3), argument (r4), index (r5) and the return address.
using NativeFullFrameGuestCall=std::function<void(uint32_t function,uint32_t object,uint32_t argument,uint32_t index,uint32_t lr)>;
void RunNativeFullFrame(NativeFullFrame& full_frame,uint8_t* base,uint32_t owner,uint32_t context,
    const std::shared_ptr<NativeFullFrameModelsShared>& models,NativeFullFrameGuestCall guest);
}  // namespace edf::native
