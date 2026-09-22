// Sky capture for the native full-frame sky pass (native_full_frame_sky.h).
// Observation only: both hooks run the original unchanged. The constructor's
// r3 is the object 820B3C28 just allocated; it is registered after the body
// returns, so the pass never sees a half-built sky. If 820B3C28 then destroys
// it (no manager at +28), the destructor hook clears it again.
#include <rex/hook.h>
#include <rex/logging.h>
#include "native_full_frame_sky.h"

namespace edf::native {
NativeSkyRegistry& NativeSkyObjects() { static NativeSkyRegistry registry; return registry; }
}

REX_EXTERN(__imp__sub_820BB460);
REX_HOOK_RAW(sub_820BB460) {
  const auto sky=ctx.r3.u32;
  __imp__sub_820BB460(ctx,base);
  auto& registry=edf::native::NativeSkyObjects();
  registry.Constructed(sky);
  const auto count=registry.constructions();
  if(count<=4 || !(count&(count-1))) REXLOG_INFO("Native sky: constructed clSky {:#x} (constructions={})",sky,count);
}
REX_EXTERN(__imp__sub_820BB3D8);
REX_HOOK_RAW(sub_820BB3D8) {
  edf::native::NativeSkyObjects().Destroyed(ctx.r3.u32);
  __imp__sub_820BB3D8(ctx,base);
}
