#pragma once
#include <cstdint>
#include <stdexcept>

namespace edf::native {
// True means the ordinary frame-event path was handled. Capture states still
// require their own controller; never discard an active capture transition.
template<class Reader,class Dispatch>
bool DispatchNativePixIdle(const Reader& reader,uint32_t device,Dispatch dispatch) {
  const auto state=reader.Word(0x82578cf8);
  if(state>=11 && state<=17) return false;
  // Preserve certification priority and its repeated pointer sample. A present
  // certification object with a null handler does not fall back to debug.
  if(reader.Word(reader.Word(0x8200071c))) {
    const auto monitor=reader.Word(reader.Word(0x8200071c));
    if(monitor) {
      const auto callback=reader.Word(monitor);
      if(callback) dispatch(callback,reader.Word(reader.Add(device,20000)));
    }
  } else {
    const auto monitor=reader.Word(reader.Word(0x82000800));
    if(monitor) {
      // Retail dispatches even a null debug handler. Do not invent success
      // for that invalid configuration; the dispatcher retains error policy.
      const auto callback=reader.Word(reader.Add(monitor,24));
      dispatch(callback,reader.Word(reader.Add(device,20000)));
    }
  }
  return true;
}
template<class Context,class Reader,class Invoke>
bool RunNativePixIdle(Context& ctx,const Reader& reader,Invoke invoke) {
  return DispatchNativePixIdle(reader,ctx.r3.u32,[&](uint32_t callback,uint32_t frame) {
    auto work=ctx;
    if(work.r1.u32<128) throw std::runtime_error("invalid native monitor callback stack");
    work.r1.u64=work.r1.u32-128u;
    work.r27.s64=work.r28.s64=-2113929216;
    work.r29.s64=-2108191752; // sign-extended 82578BF8
    work.r30=ctx.r3;
    work.r3.u64=46; work.r4.u64=frame; work.lr=0x82138B8C;
    work.ctr.u64=callback; work.last_indirect_target=callback;
    invoke(callback,work);
    ctx.r3=work.r3;
  });
}
}
