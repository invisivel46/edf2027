// HUD, XUI and movies: the embedded XUI shaders and the movie decoder's texture locks.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../pause_menu.h"
#include "../../d3d11_backend.h"
#include "../../guest_sdk_readable_range.h"
#include "../../d3d11_texture.h"
#include "../../bridge/native_cvars.h"
#include "../../bridge/bridge_state.h"
#include "../../bridge/bridge_helpers.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace edf::native {
namespace {
void RecordEmbeddedShader(const GuestReader& reader,uint32_t output,uint32_t source,bool pixel,uint32_t caller) {
  const auto handle=reader.Word(output);
  if (!handle) throw std::runtime_error("middleware shader creation returned null");
  reader.Bytes(source,4);
  auto& state=State();
  std::lock_guard lock(state.mutex);
  state.embedded_shaders.insert_or_assign(handle,EmbeddedShader{source,pixel});
  ++state.embedded_shader_generation;
  REXLOG_INFO("Native middleware shader source: handle={:#x}, source={:#x}, pixel={}, caller={:#x}",handle,source,pixel,caller);
}
}
}
// XUI's device wrapper stores raw created shader handles directly through r5.
// These identities are diagnostic until the embedded programs are recovered;
// never register an arbitrary game effect as a substitute for an XUI shader.
REX_EXTERN(__imp__sub_8241E180);
REX_HOOK_RAW(sub_8241E180) {
  const auto source=ctx.r4.u32,output=ctx.r5.u32,caller=uint32_t(ctx.lr);
  __imp__sub_8241E180(ctx,base);
  if (EDF_NATIVE_FLAG(shader_bridge) && ctx.r3.s32>=0) {
    try { edf::native::RecordEmbeddedShader(edf::native::GuestReader(base),output,source,false,caller); }
    catch (const std::exception& error) { REXLOG_ERROR("Native middleware VS identity: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_8241E1D0);
REX_HOOK_RAW(sub_8241E1D0) {
  const auto source=ctx.r4.u32,output=ctx.r5.u32,caller=uint32_t(ctx.lr);
  __imp__sub_8241E1D0(ctx,base);
  if (EDF_NATIVE_FLAG(shader_bridge) && ctx.r3.s32>=0) {
    try { edf::native::RecordEmbeddedShader(edf::native::GuestReader(base),output,source,true,caller); }
    catch (const std::exception& error) { REXLOG_ERROR("Native middleware PS identity: {}",error.what()); }
  }
}
// The movie decoder calls LockRect directly, not through the XUI wrapper.
// Capture only locks nested in its decode call; ordinary resource locks do not
// establish video identity. 8213AE38 writes {row_pitch,pixel_address} to r5.
REX_EXTERN(__imp__sub_8213B5A0);
REX_HOOK_RAW(sub_8213B5A0) {
  const auto texture=ctx.r3.u32,level=ctx.r4.u32,output=ctx.r5.u32,rect=ctx.r6.u32;
  __imp__sub_8213B5A0(ctx,base);
  if (auto* capture=edf::native::active_movie_decode) {
    try {
      if (level || rect || capture->planes.size()>=3) throw std::runtime_error("unexpected movie plane lock");
      const edf::native::GuestReader reader(base);
      capture->planes.push_back({texture,reader.Word(output),reader.Word(reader.Add(output,4))});
    } catch (...) { capture->failed=true; }
  }
}
REX_EXTERN(__imp__sub_8242AF30);
REX_HOOK_RAW(sub_8242AF30) {
  if (!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_8242AF30(ctx,base); return; }
  const auto owner=ctx.r3.u32;
  edf::native::MovieDecodeLocks capture;
  struct CaptureScope {
    edf::native::MovieDecodeLocks* previous;
    explicit CaptureScope(edf::native::MovieDecodeLocks& current)
      : previous(edf::native::active_movie_decode) { edf::native::active_movie_decode=&current; }
    ~CaptureScope() { edf::native::active_movie_decode=previous; }
  };
  { CaptureScope scope(capture); __imp__sub_8242AF30(ctx,base); }
  // Only S_OK publishes the newly decoded buffer at owner+52. Other positive
  // statuses include end-of-stream, and must not upload unwritten pixels.
  if (ctx.r3.s32!=0) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  try {
    if (capture.failed || capture.planes.size()!=3) throw std::runtime_error("incomplete movie decode locks");
    const edf::native::GuestReader reader(base);
    const auto buffer=reader.Word(reader.Add(owner,52));
    const auto width=reader.Word(reader.Add(owner,56)),height=reader.Word(reader.Add(owner,60));
    if (buffer>1 || !width || !height || width>4096 || height>4096 || (width&1) || (height&1))
      throw std::runtime_error("invalid movie buffer dimensions/index");
    for (size_t i=0;i<3;++i) {
      const auto& plane=capture.planes[i];
      const auto w=i ? width/2 : width,h=i ? height/2 : height;
      if (plane.texture!=reader.Word(reader.Add(owner,20+uint32_t(i)*8+buffer*4)) || plane.pitch<w || plane.pitch>16384)
        throw std::runtime_error("movie lock does not match published YUV buffer");
      const auto creation=state.texture_creations.find(plane.texture);
      if (creation==state.texture_creations.end() || creation->second.format!=0x28000002 ||
          creation->second.width!=w || creation->second.height!=h)
        throw std::runtime_error("movie plane is not the verified linear 8-bit allocation");
      const auto* pixels=reader.Bytes(plane.pixels,size_t(plane.pitch)*(h-1)+w);
      auto& native=state.textures[plane.texture];
      native.content_valid=false;
      if (!native.backend || native.width!=w || native.height!=h) {
        edf::native::NativeTexture replacement;
        replacement.content_valid=false;
        edf::native::NativeBackendTextureDesc desc{};
        desc.width=w; desc.height=h; desc.levels=1;
        desc.format=DXGI_FORMAT_R8_UNORM; desc.sampled=true;
        replacement.backend=edf::native::EnsureSceneBackendLocked(state).CreateTexture(desc,{});
        if(!replacement.backend)
          throw std::runtime_error("native movie plane allocation failed");
        replacement.resource=edf::native::NativeD3D11TextureResource(*replacement.backend);
        replacement.view=edf::native::NativeD3D11TextureView(*replacement.backend);
        replacement.width=w; replacement.height=h; replacement.mip_count=1;
        replacement.format=DXGI_FORMAT_R8_UNORM;
        native=std::move(replacement);
      }
      std::vector<uint8_t> packed(size_t(w)*h);
      for(uint32_t row=0;row<h;++row)
        std::memcpy(packed.data()+size_t(row)*w,pixels+size_t(row)*plane.pitch,w);
      edf::native::SceneRecorderLocked(state).UpdateTexture(*native.backend,packed);
      native.content_valid=true;
      if (state.movie_uploads<3) REXLOG_INFO("Native movie plane upload: owner={:#x}, buffer={}, plane={}, texture={:#x}, {}x{}, pitch={}, pixels={:#x}",
        owner,buffer,i,plane.texture,w,h,plane.pitch,plane.pixels);
    }
    ++state.movie_uploads;
  } catch (const std::exception& error) {
    for (const auto& plane:capture.planes) if (const auto found=state.textures.find(plane.texture);found!=state.textures.end())
      found->second.content_valid=false;
    if (++state.movie_upload_errors<=8) REXLOG_ERROR("Native movie upload: {}",error.what());
  }
}
