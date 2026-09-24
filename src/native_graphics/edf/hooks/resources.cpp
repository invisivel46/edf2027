// Texture, shader and render-target registration and device resources: imports, allocation, the surface placement (EDRAM), render-state objects, destruction.
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../pause_menu.h"
#include "../../d3d12_backend.h"
#include "../../native_scene_sources.h"
#include "../../native_scene_adapter.h"
#include "../../native_decode_workers.h"
#include "../../guest_sdk_readable_range.h"
#include "../../native_model_buffers.h"
#include "../../native_render_state_snapshot.h"
#include "../../native_declarations.h"
#include "../../native_font_bindings.h"
#include "../../native_load_trace.h"
#include "../../native_shader_precompile.h"
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
void ForgetOwner(uint32_t owner) {
  auto& state = State();
  // Registry/cache retirement does not submit commands. The registry mutex
  // excludes native users; bound GPU resources retain their D3D references.
  std::lock_guard lock(state.mutex);
  const auto erased = std::erase_if(state.shaders, [owner](const auto& item) { return item.second.owner == owner; });
  ++state.shader_registry_generation;
  state.active_vertex = state.linked_vertex = state.linked_pixel = 0;
  state.validated_links.clear();
  state.active_vertex_parameters.reset();
  if (erased) REXLOG_INFO("Native shader bridge: released {} shaders for owner={:#x}", erased, owner);
  if (erased) { state.meshes.Clear(); state.immediate_meshes.Clear(); }
}
template<class Original>
void ImportTexture(PPCContext& ctx, uint8_t* base, Original original) {
  if (!EDF_NATIVE_FLAG(shader_bridge)) { original(ctx, base); return; }
  const GuestReader reader(base);
  uint32_t output = 0;
  std::vector<uint8_t> image;
  try {
    HookTiming snapshot_timing(HookPhase::TextureSnapshot);
    LoadTraceScope snapshot_trace(::LoadTraceOn(),LoadTraceKind::TextureSnapshot,ctx.r5.u32);
    // Shared D3DX image loader sub_82201458, reached by both engine loaders
    // and direct callers. The output parameter is at entry SP+148; the
    // function's 1520-byte frame accesses it at SP+1668.
    output = reader.Word(reader.Add(ctx.r1.u32, 148));
    const auto address = ctx.r4.u32;
    const auto size = ctx.r5.u32;
    if (size > 256 * 1024 * 1024) throw std::runtime_error("texture source exceeds size limit");
    const auto* bytes = reader.Bytes(address, size);
    image.assign(bytes, bytes + size);
  } catch (const std::exception& error) {
    REXLOG_ERROR("Native texture bridge: snapshot failed: {}", error.what());
  }
  {
    struct LoaderScope {
      LoaderScope() { ++texture_loader_depth; }
      ~LoaderScope() { --texture_loader_depth; }
    } scope;
    HookTiming original_timing(HookPhase::TextureOriginal);
    LoadTraceScope original_trace(::LoadTraceOn(),LoadTraceKind::TextureGuest);
    original(ctx, base);
  }
  if (image.empty() || ctx.r3.s32 < 0) return;
  auto& state = State();
  HookTiming lock_timing(HookPhase::TextureLock);
  LoadTraceScope lock_trace(::LoadTraceOn(),LoadTraceKind::TextureLockWait);
  // CreateNativeDdsTexture uses device resource creation with initial data,
  // not immediate-context uploads. Registry publication is serialized below;
  // it must not wait for the rendering submission barrier's refresh sleeps.
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  lock_trace.Finish();
  try {
    const auto handle = reader.Word(output);
    if (!handle) throw std::runtime_error("guest texture creation returned null");
    // Erase first: a failed replacement must not leave an old native image
    // associated with an address the guest has reused for a new resource.
    state.textures.erase(handle);
    if (!state.initialized) throw std::runtime_error("native texture bridge not initialized");
    HookTiming create_timing(HookPhase::TextureCreate);
    LoadTraceScope create_trace(::LoadTraceOn(),LoadTraceKind::TextureCreate,image.size());
    auto native = CreateNativeDdsTexture(EnsureSceneBackendLocked(state), image);
    create_timing.Finish();
    create_trace.Finish();
    ++state.texture_loads;
    REXLOG_INFO("Native texture bridge: handle={:#x}, {}x{}, mips={}, cube={}, loads={}",
                handle, native.width, native.height, native.mip_count, native.cube, state.texture_loads);
    state.textures.emplace(handle, std::move(native));
  } catch (const std::exception& error) {
    ++state.texture_errors;
    REXLOG_ERROR("Native texture bridge: {} (errors={})", error.what(), state.texture_errors);
  }
}
Effect SnapshotEffect(const GuestReader& reader, uint32_t data) {
  Effect result;
  result.source = reader.String(reader.Add(data, reader.Word(reader.Add(data, 4))), 16 * 1024 * 1024);
  const auto count = reader.Word(reader.Add(data, 16));
  if (count > 4096) throw std::runtime_error("invalid guest shader count");
  const auto table = reader.Add(data, reader.Word(reader.Add(data, 20)));
  reader.Bytes(table, size_t(count) * 12);
  for (uint32_t i = 0; i < count; ++i) {
    const auto record = reader.Add(table, i * 12);
    const auto stage = reader.Word(record);
    if (stage > 1) throw std::runtime_error("invalid guest shader stage");
    result.entries.push_back({stage == 1,
      reader.String(reader.Add(record, reader.Word(reader.Add(record, 4))), 256),
      reader.String(reader.Add(record, reader.Word(reader.Add(record, 8))), 64)});
  }
  return result;
}
// One pool for the run. Created on first registration so a run that loads no
// shaders never starts a thread.
NativeDecodeWorkers& ShaderWorkers() {
  static NativeDecodeWorkers workers([] {
    const auto requested=REXCVAR_GET(edf_native_shader_workers);
    if(requested==0) return uint32_t(0);
    if(requested>0) return uint32_t((std::min)(requested,64));
    // One per core, less the one doing the loading, capped: compilation is
    // memory-bound enough that more threads stop helping well before the core
    // count on a large machine.
    const auto cores=std::thread::hardware_concurrency();
    return uint32_t((std::min)(cores>1?cores-1:1u,8u));
  }());
  return workers;
}
void RegisterShaders(const GuestReader& reader, uint32_t owner, const Effect& effect) {
  HookTiming registration_timing(HookPhase::ShaderRegistration);
  LoadTraceScope registration_trace(::LoadTraceOn(),LoadTraceKind::ShaderRegistration);
  auto& state = State();
  HookTiming lock_timing(HookPhase::ShaderLock);
  // Compilation, device-only creation and registry replacement issue no
  // immediate-context commands. Keep registry serialization, not refresh waits.
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  if (!state.initialized) throw std::runtime_error("native shader bridge not initialized");
  const auto count = reader.Word(reader.Add(owner, 8));
  if (count != effect.entries.size()) throw std::runtime_error("guest compiled shader count mismatch");
  const auto records = reader.Word(owner);
  reader.Bytes(records, size_t(count) * 72);
  // Guest memory is read here, on the calling thread, before anything is
  // handed to a worker: the reads must happen while the game is inside this
  // call, and a worker touching guest memory would be reading it at a time the
  // game never agreed to.
  std::vector<uint32_t> handles(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& entry = effect.entries[i];
    const auto record = reader.Add(records, i * 72);
    if (reader.Word(reader.Add(record, 68)) != uint32_t(entry.pixel))
      throw std::runtime_error("guest compiled shader stage mismatch");
    handles[i] = reader.Word(reader.Add(record, entry.pixel ? 4 : 0));
    if (!handles[i]) throw std::runtime_error("guest compiled shader handle is null");
  }

  // Compilation is the part worth threading: each entry costs milliseconds and
  // a registration brings several, which is the batch the per-draw path does
  // not have. D3DCompile is thread-safe and ID3D11Device resource creation is
  // free-threaded; nothing below touches the immediate context or the bridge
  // state, both of which are not.
  std::vector<RegisteredShader> built(count);
  std::vector<std::exception_ptr> failures(count);
  auto& workers = ShaderWorkers();
  std::vector<uint64_t> tickets;
  tickets.reserve(count);
  // The boot precompile (native_shader_precompile.h) compiles the disc's
  // effects under this same path and in the same variants as the body below
  // (PrepareGuestShaderEntry), so a registration after it is a cache hit. A
  // variant added here must be added there, or it compiles here on a cold run.
  const auto source_path = GuestEffectSourcePath(state.root);
  auto* device = state.device.Get();
  for (uint32_t i = 0; i < count; ++i)
    tickets.push_back(workers.Submit([&, i] {
      HookTiming entry_timing(HookPhase::ShaderEntry);
      try {
        const auto& entry = effect.entries[i];
        auto shader = CompileNativeShader(device, effect, entry, source_path);
        if(!entry.pixel) AddNativeWorldInstancing(shader,effect,source_path);
        built[i] = RegisteredShader{owner, std::make_unique<ShaderBindings>(device, std::move(shader))};
        built[i].source_fingerprint = EffectSourceFingerprint(effect.source);
        if (!entry.pixel) {
          auto reversed=CompileNativeShader(device,effect,entry,source_path,true);
          AddNativeWorldInstancing(reversed,effect,source_path,true);
          built[i].reversed_bindings = std::make_unique<ShaderBindings>(device,std::move(reversed));
          built[i].reversed_mirrors=built[i].reversed_bindings->SharesConstantLayout(*built[i].bindings);
        }
      } catch (...) {
        failures[i] = std::current_exception();
      }
    }));
  for (const auto ticket : tickets) workers.Wait(ticket);
  // Rethrown in entry order, so which entry is blamed does not depend on which
  // worker happened to finish first.
  for (uint32_t i = 0; i < count; ++i) if (failures[i]) std::rethrow_exception(failures[i]);
  for (uint32_t i = 0; i < count; ++i)
    if (built[i].reversed_bindings && !built[i].reversed_mirrors)
      REXLOG_INFO("Native shader bridge: reversed variant of {} reflects a different constant layout; both variants take their own uploads",
                  effect.entries[i].name);

  std::unordered_map<uint32_t, RegisteredShader> fresh;
  for (uint32_t i = 0; i < count; ++i)
    if (!fresh.emplace(handles[i], std::move(built[i])).second)
      throw std::runtime_error("duplicate guest shader handle");
  // Handle equality is not shader-generation equality. The linked pair/active
  // ranges must not survive a replacement at the same guest address. Invalidate
  // before mutating the registry, including a handle reused by another owner.
  const bool replacing=std::any_of(state.shaders.begin(),state.shaders.end(),
    [&](const auto& item){return item.second.owner==owner || fresh.contains(item.first);});
  if(replacing) {
    state.active_vertex=state.linked_vertex=state.linked_pixel=0;
    state.active_vertex_parameters.reset();
    // Mesh caches independently compare owned bytecode identity on acquisition.
  }
  // Replace the entire owner on reload so address reuse cannot select old code.
  std::erase_if(state.shaders, [owner](const auto& item) { return item.second.owner == owner; });
  for (auto& [handle, shader] : fresh) state.shaders.insert_or_assign(handle, std::move(shader));
  ++state.shader_registry_generation;
  // A handle may now name different code; every pair is checked again.
  state.validated_links.clear();
  REXLOG_INFO("Native shader bridge: owner={:#x}, {} guest shaders registered, {} resident",
              owner, count, state.shaders.size());
}
void RegisterRenderTarget(const GuestReader& reader, uint32_t owner) {
  const auto format = reader.Word(reader.Add(owner,16));
  const auto surface_format = reader.Word(reader.Add(owner,20));
  // Verified post-processing pairs in sub_820B1028. Other pairs need their
  // component swizzles and resolve conversions recovered before enabling them.
  const bool luminance=format==0x2da2ab5e && surface_format==0x2da2aba4;
  const bool bloom=format==0x18280186 && surface_format==0x1a2201bf;
  const bool rgba16=(format==0x1a22ab60 || format==0x1a22ab5d) && surface_format==0x1a2201bf;
  if (!luminance && !bloom && !rgba16) return;
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto width = reader.Word(reader.Add(owner,24));
  const auto height = reader.Word(reader.Add(owner,28));
  const auto texture = reader.Word(reader.Add(owner,4));
  const auto surface = reader.Word(reader.Add(owner,12));
  if (!texture || !surface || !state.initialized) throw std::runtime_error("invalid native render target registration");
  auto native = luminance ? CreateNativeLuminanceTarget(EnsureSceneBackendLocked(state),width,height) :
    bloom ? CreateNativeBloomTarget(EnsureSceneBackendLocked(state),width,height) :
    CreateNativeRenderTarget(EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_R16G16B16A16_FLOAT);
  if (luminance && width==1 && height==1) {
    // 8213B730 inserts the allocation address in the header's upper 20 bits
    // at +32. Snapshot at creation, before any GPU writes; never reread stale
    // CPU backing memory as if it were a later rendered history value.
    const auto allocation=reader.Word(reader.Add(texture,32))&0xfffff000u;
    try {
      const bool imported=ImportZeroLuminanceHistory(SceneRecorderLocked(state),native,
        {reader.Bytes(allocation,4096),4096});
      REXLOG_INFO("Native tone initial allocation: texture={:#x}, page={:#x}, uniform_zero_import={}",texture,allocation,imported);
    } catch (const std::exception& error) {
      REXLOG_INFO("Native tone initial allocation not imported: {}",error.what());
    }
  }
  state.textures.insert_or_assign(texture,native.sampled);
  state.render_targets.insert_or_assign(owner,RegisteredTarget{texture,surface,std::move(native)});
  REXLOG_INFO("Native render target: owner={:#x}, texture={:#x}, surface={:#x}, {}x{}",owner,texture,surface,width,height);
}
void EndRenderTarget(uint32_t owner) {
  auto& state = State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  if (state.target_stack.empty() || state.target_stack.back().first != owner)
    throw std::runtime_error("unbalanced native render target scope");
  auto found = state.render_targets.find(owner);
  if (found != state.render_targets.end()) {
    // Unbind SRVs before writing the resolved resource. The next material
    // activation rebinds its own resources by name.
    ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
    if(state.context) {
      state.context->PSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
      state.context->VSSetShaderResources(0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,empty);
    }
    auto& target = found->second;
    // Through the recorder, always: a converting target's resolve is a draw,
    // and on the adopted D3D11 backend the recorder issues straight to the same
    // immediate context, so the ordering against the direct paths is exact.
    ResolveNativeRenderTarget(SceneRecorderLocked(state),target.native);
    // A converting resolve is a draw and binds its own targets, so whatever
    // this code last bound - on the context or on the recorder - is no longer
    // what is set.
    ++state.bind_generation;
    state.recorded={};
    state.textures.insert_or_assign(target.texture_handle,target.native.sampled);
    if (target.native.sampled.content_valid) ++state.target_resolves;
    else ++state.target_unwritten;
  }
  state.active_target = state.target_stack.back().second;
  state.target_stack.pop_back();
  BindActiveTarget(state);
  if (state.target_begins <= 5 || state.target_begins % 1000 == 0)
    REXLOG_INFO("Native render target: begins={}, valid_resolves={}, unwritten={}",
                state.target_begins,state.target_resolves,state.target_unwritten);
}
}
}
REX_EXTERN(sub_821B88B0);
REX_EXTERN(__imp__sub_821B6880);
REX_HOOK_RAW(sub_821B6880) {
  if (!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_821B6880(ctx, base); return; }
  const uint32_t owner = ctx.r3.u32;
  edf::native::ForgetOwner(owner);
  edf::native::Effect effect;
  bool captured = false;
  try {
    effect = edf::native::SnapshotEffect(edf::native::GuestReader(base), ctx.r4.u32);
    captured = true;
  } catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  __imp__sub_821B6880(ctx, base);
  if (captured && ctx.r3.u8) {
    try { edf::native::RegisterShaders(edf::native::GuestReader(base), owner, effect); }
    catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  }
}
REX_EXTERN(__imp__sub_82139638);
REX_HOOK_RAW(sub_82139638) {
  // 82139A40 allocates and zero-fills the device before either initializer.
  // Publish the known zero result here, not a later draw-time memory snapshot.
  const bool device_allocation=uint32_t(ctx.lr)==0x82139a70 &&
    ctx.r3.u32==20480 && ctx.r4.u32==128;
  __imp__sub_82139638(ctx,base);
  if(device_allocation && ctx.r3.u32 &&
      (REXCVAR_GET(edf_native_render_state_audit) || REXCVAR_GET(edf_native_owned_render_state)) &&
      EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.render_state_snapshots.Retire(ctx.r3.u32);
    state.render_state_snapshots.Publish(ctx.r3.u32,{},0x82139638);
    state.render_state_snapshots.PublishBlend(ctx.r3.u32,{},0x82139638);
    REXLOG_INFO("Native render-state zeroed device: device={:#x}",ctx.r3.u32);
  }
}
REX_EXTERN(__imp__sub_82139760);
REX_HOOK_RAW(sub_82139760) {
  if((REXCVAR_GET(edf_native_render_state_audit) || REXCVAR_GET(edf_native_owned_render_state)) &&
      EDF_NATIVE_FLAG(shader_bridge)) {
    const edf::native::GuestReader reader(base);
    // Final Release invokes destruction/free; non-final Release keeps ownership.
    if(reader.Word(reader.Add(ctx.r3.u32,52))==1) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      state.render_state_snapshots.Retire(ctx.r3.u32);
    }
  }
  __imp__sub_82139760(ctx,base);
}
REX_EXTERN(__imp__sub_82135418);
REX_HOOK_RAW(sub_82135418) {
  const auto device=ctx.r3.u32;
  __imp__sub_82135418(ctx,base);
  if((REXCVAR_GET(edf_native_owned_render_state) || REXCVAR_GET(edf_native_render_state_audit)) &&
      EDF_NATIVE_FLAG(shader_bridge)) {
    const edf::native::GuestReader reader(base);
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    state.render_state_snapshots.PublishBlend(device,
      edf::native::ReadGuestWords<4>(reader,reader.Add(device,10336)),0x82135418);
  }
}
// sub_820AB2F8 destroys the 72-byte shader-record array and clears its owner
// pointer/count/capacity. Native resources must not outlive that ownership.
REX_EXTERN(__imp__sub_820AB2F8);
REX_HOOK_RAW(sub_820AB2F8) {
  if (EDF_NATIVE_FLAG(shader_bridge)) edf::native::ForgetOwner(ctx.r3.u32);
  __imp__sub_820AB2F8(ctx, base);
}
REX_EXTERN(__imp__sub_82201458);
REX_HOOK_RAW(sub_82201458) { edf::native::ImportTexture(ctx, base, __imp__sub_82201458); }
// Attribution only: retain every original call and limit nested measurements
// to this thread's active image-loader scope. These phases overlap its total.
REX_EXTERN(__imp__sub_821FFB38);
REX_HOOK_RAW(sub_821FFB38) {
  edf::native::HookTiming timing(edf::native::HookPhase::TextureUpload2D,edf::native::texture_loader_depth!=0);
  __imp__sub_821FFB38(ctx,base);
}
REX_EXTERN(__imp__sub_822001E0);
REX_HOOK_RAW(sub_822001E0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TextureUploadVolume,edf::native::texture_loader_depth!=0);
  __imp__sub_822001E0(ctx,base);
}
REX_EXTERN(__imp__sub_822009B0);
REX_HOOK_RAW(sub_822009B0) {
  edf::native::HookTiming timing(edf::native::HookPhase::TexturePrepare,edf::native::texture_loader_depth!=0);
  __imp__sub_822009B0(ctx,base);
}
// Final resource destruction, reached after sub_821347C0 decrements the
// reference count to zero. Remove the mapping before the address can be reused.
REX_EXTERN(__imp__sub_82134220);
REX_HOOK_RAW(sub_82134220) {
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    edf::native::LoadTraceScope trace(LoadTraceOn(),edf::native::LoadTraceKind::ResourceDestroy);
    auto& state = edf::native::State();
    // Registry retirement issues no context commands. Submitted D3D work owns
    // its resource references; registry users are serialized by state.mutex.
    // Do not wait behind the swap's refresh-pacing submission barrier.
    std::lock_guard lock(state.mutex);
    state.texture_creations.erase(ctx.r3.u32);
    for(auto& [owner,scene]:state.scenes) scene.direct_outputs.erase(ctx.r3.u32);
    state.meshes.Invalidate(ctx.r3.u32);
    state.model_buffers.Retire(ctx.r3.u32);
    state.declarations.Retire(ctx.r3.u32);
    state.depth_targets.erase(ctx.r3.u32);
    state.surface_creations.erase(ctx.r3.u32);
    if(state.embedded_shaders.erase(ctx.r3.u32)) ++state.embedded_shader_generation;
    for (auto& [owner,scene]:state.scenes) if (scene.output_surface==ctx.r3.u32) {
      scene.output={}; scene.output_surface=0;
      if (state.active_output==owner) state.active_output=0;
    }
    std::erase_if(state.render_targets,[handle=ctx.r3.u32](const auto& item) {
      return item.second.texture_handle == handle || item.second.surface_handle == handle;
    });
    if (state.textures.erase(ctx.r3.u32))
      REXLOG_INFO("Native texture bridge: released handle={:#x}, resident={}", ctx.r3.u32, state.textures.size());
  }
  __imp__sub_82134220(ctx, base);
}
// Surface creation uses width/height/format/MSAA in r3-r6. Floating depth
// uses host D32S8 in this development bridge; this does not reproduce the
// guest's 24-bit floating quantization or establish frame equivalence.
REX_EXTERN(__imp__sub_8213B850);
REX_HOOK_RAW(sub_8213B850) {
  const auto width=ctx.r3.u32,height=ctx.r4.u32,format=ctx.r5.u32,msaa=ctx.r6.u32;
  // The device's own surfaces (82147318's back buffer and auto depth) take
  // their EDRAM from the allocator, which refuses anything above 2048 tiles
  // (native_display_layout.h). Larger ones, only possible at an overridden
  // render size, get the caller placement {0,0,0} the engine's own targets
  // use, so the descriptor keeps the full extent; the surface is then not
  // marked as allocator-owned and its release (82134220) frees no tiles.
  // Surfaces that fit, 1280x720 among them, keep the original path.
  const auto caller=uint32_t(ctx.lr);
  if(ctx.r7.u32==0 && (caller==0x82147418u || caller==0x82147454u) &&
     !edf::native::GuestEdramSurfaceFits(width,height,format,int32_t(msaa))) {
    const edf::native::GuestReader reader(base);
    if(ctx.r1.u32<4096) throw std::runtime_error("invalid stack for native device surface placement");
    auto work=ctx;
    // A 32-byte frame below the caller's: back chain, then the 12-byte
    // placement at +16, above the callee's own (negative-offset) saves.
    work.r1.u64=(ctx.r1.u32-32u)&~15u;
    reader.StoreWord(work.r1.u32,ctx.r1.u32);
    reader.StoreCpuWords(work.r1.u32+16u,std::array<uint32_t,3>{0u,0u,0u});
    work.r7.u64=work.r1.u32+16u;
    __imp__sub_8213B850(work,base);
    work.r1.u64=ctx.r1.u64;
    ctx=work;
    static std::atomic<uint32_t> reports{0};
    if(reports.fetch_add(1,std::memory_order_relaxed)<4)
      REXLOG_INFO("Native device surface beyond guest EDRAM: caller={:#x}, {}x{}, format={:#x}, MSAA={}, tiles={} > {}; caller placement, handle={:#x}",
        caller,width,height,format,msaa,edf::native::GuestEdramSurfaceTiles(width,height,format,int32_t(msaa)),
        edf::native::kGuestEdramTiles,ctx.r3.u32);
  } else {
    __imp__sub_8213B850(ctx,base);
  }
  if (EDF_NATIVE_FLAG(shader_bridge) && ctx.r3.u32) {
    auto& state=edf::native::State();
    // Device-only creation/registry replacement; no context submission here.
    std::lock_guard lock(state.mutex);
    state.surface_creations.insert_or_assign(ctx.r3.u32,edf::native::SurfaceCreation{width,height,format,msaa});
    state.depth_targets.erase(ctx.r3.u32);
    if (format==0x1a220197) {
      try {
        if (msaa) {
          REXLOG_INFO("Native depth allocation: {}x{}, MSAA={} not supported",width,height,msaa);
        } else {
          auto target=edf::native::CreateNativeDepthTarget(edf::native::EnsureSceneBackendLocked(state),width,height,DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
          state.depth_targets.insert_or_assign(ctx.r3.u32,std::move(target));
          REXLOG_INFO("Native depth allocation: handle={:#x}, {}x{}, host=D32S8 (development)",ctx.r3.u32,width,height);
        }
      } catch (const std::exception& error) { REXLOG_ERROR("Native depth allocation: {}",error.what()); }
    }
  }
}
// The engine ignores video-mode dimensions and initializes its renderer to
// 1280x720/960. Replace that choice before device/resource initialization, so
// all consumers (scene, depth, post pyramid and UI) see one consistent extent.
// Audited call: 8219E3B8 -> 82139A40 at LR 8219E4D4; r7 is presentation
// parameters, r8 is &renderer.device at renderer+8. Later consumers reload
// renderer+84/+88 rather than retaining the fixed dimensions in registers.
// The presentation size also sizes the device's back buffer, whose EDRAM
// allocation refuses more than 2048 tiles; the 8213B850 hook places a larger
// one itself (GuestEdramSurfaceFits in native_display_layout.h).
REX_EXTERN(__imp__sub_82139A40);
REX_HOOK_RAW(sub_82139A40) {
  if (uint32_t(ctx.lr)==0x8219E4D4u) {
    const auto width=edf::native::NativeRenderDimensions()[0];
    const auto height=edf::native::NativeRenderDimensions()[1];
    if(width || height) {
      // ResolveNativeRenderSize already applied the limits; this only refuses
      // a size that could not have come from it.
      if(!edf::native::ValidNativeRenderRequest(width,height))
        throw std::runtime_error("native render dimensions outside the render size limits");
      const edf::native::GuestReader reader(base);
      if(ctx.r8.u32<8 || reader.Word(ctx.r7.u32)!=1280 || reader.Word(reader.Add(ctx.r8.u32,76))!=1280)
        throw std::runtime_error("native resolution initialization contract changed");
      const std::array<uint32_t,2> dimensions{uint32_t(width),uint32_t(height)};
      reader.StoreCpuWords(ctx.r7.u32,dimensions);
      reader.StoreCpuWords(reader.Add(ctx.r8.u32,76),dimensions);
      REXLOG_INFO("Native render resolution selected: {}x{} (engine resource initialization)",width,height);
    }
  }
  __imp__sub_82139A40(ctx,base);
}
REX_EXTERN(__imp__sub_821B8C30);
REX_HOOK_RAW(sub_821B8C30) {
  const auto owner = ctx.r3.u32;
  __imp__sub_821B8C30(ctx,base);
  if (EDF_NATIVE_FLAG(shader_bridge) && ctx.r3.u8) {
    try { edf::native::RegisterRenderTarget(edf::native::GuestReader(base),owner); }
    catch (const std::exception& error) { REXLOG_ERROR("Native render target: {}",error.what()); }
  }
}
REX_EXTERN(__imp__sub_821B88B0);
REX_HOOK_RAW(sub_821B88B0) {
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    try {
      const edf::native::GuestReader reader(base);
      if (*reader.Bytes(reader.Add(ctx.r3.u32,40),1)) edf::native::EndRenderTarget(ctx.r3.u32);
    } catch (const std::exception& error) { REXLOG_ERROR("Native render target end: {}",error.what()); }
  }
  __imp__sub_821B88B0(ctx,base);
}
// D3D texture allocation: dimensions in r3-r5, mip count r6, usage r7,
// format r8, pool r9, resource type r10; returns the new resource pointer.
REX_EXTERN(__imp__sub_8213B730);
REX_HOOK_RAW(sub_8213B730) {
  if (!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_8213B730(ctx, base); return; }
  const edf::native::TextureCreation creation{ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
    ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32, uint32_t(ctx.lr)};
  {
    edf::native::HookTiming timing(edf::native::HookPhase::TextureAllocate,edf::native::texture_loader_depth!=0);
    __imp__sub_8213B730(ctx, base);
  }
  if (ctx.r3.u32) {
    auto& state = edf::native::State();
    // Metadata publication only: no native upload/draw or context operation.
    // Keep address-reuse invalidation atomic without joining frame pacing.
    std::lock_guard lock(state.mutex);
    state.textures.erase(ctx.r3.u32);
    state.texture_creations.insert_or_assign(ctx.r3.u32, creation);
  }
}
