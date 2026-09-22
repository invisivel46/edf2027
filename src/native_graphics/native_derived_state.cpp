#include "edf2017_pch.h"

// CPU render-control effects of D750, without its Xbox transition packet.
DEFINE_REX_FUNC(edf_native_derived_cpu_tail) {
  const auto device=ctx.r3.u32;
  const auto dirty=ctx.r4.u64;
  bool shader_feature=false;
  const auto pixel=REX_LOAD_U32(device+12416);
  if(pixel) {
    const auto metadata=pixel+REX_LOAD_U32(pixel+64);
    shader_feature=(REX_LOAD_U32(metadata+48)&(1u<<27))!=0;
  }
  const auto control=REX_LOAD_U32(device+11580)>>20;
  const auto render=REX_LOAD_U32(device+10420);
  const auto flags=REX_LOAD_U8(device+10810);
  auto state=(REX_LOAD_U32(device+10432)&~1u)|(((flags>>5)&(render>>1))&1u);
  REX_STORE_U32(device+10432,state);
  const auto mode=(render>>4)&7;
  bool enabled=(control&1)!=0;
  if(control&2) {
    const bool mode_matches=mode==2 || ((state&16)?(mode==4 || mode==6):(mode==1 || mode==3));
    enabled=!shader_feature && (state&1) && (render&2) && mode_matches;
    if(render&1) {
      enabled=enabled && !(render&0xe3800);
      if(render&0x80) enabled=enabled && !(render&0xe3800000);
    }
  }
  state=(REX_LOAD_U32(device+10432)&~2u)|(enabled?2:0);
  REX_STORE_U32(device+10432,state);
  if(enabled) {
    const auto previous=REX_LOAD_U8(device+10809);
    if(previous&4) REX_STORE_U8(device+10809,previous&0xfb);
  } else if((render&6)==6 && (mode==7 || mode==5 || ((state&16)?(mode==1 || mode==3):(mode==4 || mode==6)))) {
    REX_STORE_U8(device+10809,REX_LOAD_U8(device+10809)|4);
  }
  ctx.r3.u64=dirty|256;
}
