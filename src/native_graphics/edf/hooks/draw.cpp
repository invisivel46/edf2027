// The per-draw path: the immediate, indexed and activation hooks, the setters and packet builders a draw goes through, and the native passes' draw recording (the static world pass, the model pass, the full frame's effect items).
// Moved from guest_shader_bridge.cpp unchanged (stage 3 of its split); what this file shares with the bridge
// and the other hook files is in bridge/bridge_shared.h.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../bridge/bridge_shared.h"
#include "../../guest_shader_bridge.h"
#include "../../native_renderer_preset.h"
#include "../../../scripted_input_logic.h"
#include "../../../pause_menu.h"
#include "../../d3d11_backend.h"
#include "../../d3d12_backend.h"
#include "../../native_scene_sources.h"
#include "../../native_scene_adapter.h"
#include "../../native_scene_cpu_window.h"
#include "../../native_recorded_reads.h"
#include "../../native_scene_geometry_install.h"
#include "../../native_material_cpu_program.h"
#include "../../native_scene_handoff.h"
#include "../../native_scene_execution.h"
#include "../../native_scene_world_restore.h"
#include "../../native_static_group_eligibility.h"
#include "../../native_static_world_resolve.h"
#include "../../native_static_world_pass.h"
#include "../../native_static_world_cache.h"
#include "../../guest_draw_state.h"
#include "../../guest_sdk_readable_range.h"
#include "../../guest_mesh_watch_audit.h"
#include "../../native_model_buffers.h"
#include "../../native_shader_state.h"
#include "../../native_full_frame_static_world.h"
#include "../../native_full_frame_models.h"
#include "../../native_full_frame_effects.h"
#include "../../native_render_state_snapshot.h"
#include "../../native_declarations.h"
#include "../../native_font_bindings.h"
#include "../../native_xui_bindings.h"
#include "../../native_movie_bindings.h"
#include "../../native_generated_indices.h"
#include "../../native_contract_ledger.h"
#include "../../native_capture_policy.h"
#include "../../native_post_finish_plan.h"
#include "../../native_constant_ownership.h"
#include "../../immediate_mesh_key.h"
#include "../../d3d11_texture.h"
#include "../../d3d11_quads.h"
#include "../../d3d11_gpu_timer.h"
#include "../../movie_effect.h"
#include "../../xui_effect.h"
#include "../../native_immediate_classify.h"
#include "../../font_effect.h"
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

REXCVAR_DECLARE(std::string, edf_hud_safe_area);
namespace edf::native {
// The 2D canvas layout for a target. edf_hud_safe_area is read per draw: it
// is a constant mapping, not an allocation, so it applies live.
NativeClipAffine NativeHudLayout(uint32_t width,uint32_t height) {
  if(!NativeRenderDimensions()[0]) return {};
  return NativeHudClipAffine(width,height,ParseNativeHudSafeArea(REXCVAR_GET(edf_hud_safe_area)));
}
// A scissor already in legacy canvas form (ScaleNativeCanvasScissor), mapped
// through a layout affine; identity leaves it untouched.
NativeViewportState MapNativeCanvasViewportScissor(NativeViewportState state,const NativeClipAffine& affine,
                                                   uint32_t width,uint32_t height) {
  if(affine.identity()) return state;
  const auto mapped=MapNativeCanvasPixelRect({int32_t(state.scissor.left),int32_t(state.scissor.top),
    int32_t(state.scissor.right),int32_t(state.scissor.bottom)},affine,width,height);
  state.scissor={LONG(mapped[0]),LONG(mapped[1]),LONG(mapped[2]),LONG(mapped[3])};
  return state;
}
namespace {
template<class Reader>
void BindGuestRenderState(const NativeRenderState& state,ID3D11DeviceContext& context,
                          const Reader& reader,uint32_t device,uint64_t* generation=nullptr) {
  // Every path that binds render state comes through here, so this is where a
  // skip elsewhere has to be invalidated from. Same for BindActiveTarget.
  // Passed in rather than fetched, because this is a template defined before
  // the bridge state is.
  if(generation) ++*generation;
  if(!state.requires_blend_factor) { state.Bind(context); return; }
  if(!REXCVAR_GET(edf_native_owned_render_state) && !REXCVAR_GET(edf_native_render_state_audit)) {
    state.Bind(context,ReadBlendFactor(reader,device)); return;
  }
  NativeRenderStateSnapshots::BlendWords live{};
  const bool audit=REXCVAR_GET(edf_native_render_state_audit);
  if(audit) live=ReadGuestWords<4>(reader,reader.Add(device,10336));
  state.Bind(context,ResolveBlendFactorForDraw(device,audit?&live:nullptr));
}
namespace {
// Fill in only what is resolved: a rejection before shader or declaration
// lookup still names a real contract, just a coarser one.
edf::native::NativeContract MakeNativeContract(Bridge& state,edf::native::NativeContractPath path,
    uint32_t vertex,uint32_t pixel,uint32_t declaration,uint32_t topology,
    uint32_t stride=0,uint32_t index_width=0) {
  edf::native::NativeContract contract;
  contract.path=path; contract.topology=topology;
  contract.stride=stride; contract.index_width=index_width;
  if(const auto found=state.shaders.find(vertex);found!=state.shaders.end() && found->second.bindings)
    contract.vertex_source=found->second.bindings->shader().source_fingerprint;
  if(const auto found=state.shaders.find(pixel);found!=state.shaders.end() && found->second.bindings)
    contract.pixel_source=found->second.bindings->shader().source_fingerprint;
  if(declaration) try {
    const auto owned=state.declarations.Get(declaration);
    contract.declaration=edf::native::HashNativeDeclaration(owned->bytes());
    contract.elements=owned->count();
    // Keep the bytes, not only the identity: the disc cannot supply them.
    state.contracts.RetainDeclaration(contract.declaration,owned->bytes());
  } catch(const std::exception&) { /* Unpublished declaration stays unresolved. */ }
  return contract;
}
void ReportNativeContractRejection(Bridge& state,const edf::native::NativeContract& contract,
    std::string_view reason) {
  state.contracts.set_limit(size_t(std::clamp(REXCVAR_GET(edf_native_contract_limit),1,1<<20)));
  if(!state.contracts.RecordRejected(contract,reason)) return;
  REXLOG_WARN("Native contract rejected: path={}, VS_source={:#x}, PS_source={:#x}, declaration={:#x}, elements={}, topology={}, stride={}, index_width={}, reason={} (this draw is missing from the frame)",
    edf::native::NativeContractPathName(contract.path),contract.vertex_source,contract.pixel_source,
    contract.declaration,contract.elements,contract.topology,contract.stride,contract.index_width,reason);
}
}
template<class Reader>
void UploadParameters(const Reader& reader, const std::shared_ptr<const NativeMaterialParameters::Groups>& owned,
                      RegisteredShader& shader, uint32_t stage_offset,
                      ShaderBindings& bindings, Bridge& state, ShaderBindings* alternate=nullptr) {
  // sub_821BBAD8 constructs each stage's 36-byte group. Local named records:
  // [name, data, register_count, register_index]. Globals:
  // [value_vector, name, register_count, register_index]. The activation path
  // sub_821B8E48 dereferences value_vector+0 to obtain the live global data.
  for (bool global : {false, true}) {
    const size_t group=(stage_offset?2:0)+(global?1:0);
    const auto& plan=shader.ResolveUploads(owned,group,alternate!=nullptr);
    state.optimized_out+=plan.optimized_out;
    if(plan.parameters.empty()) continue;
    // Keep record windows and payloads live. Only shader layout decisions are
    // cached: material/global pointers may change on every activation.
    const auto record_base=owned->record_base[group];
    const auto record_bytes=owned->record_bytes[group];
    const std::array<ShaderBindings*,2> destinations{&bindings,alternate};
    const auto upload=[&](const auto& source) {
    for(const auto& entry:plan.parameters) {
      const auto& parameter=(*owned)[group][entry.index];
      const auto value=parameter.ReadValue(source,global);
      const auto& targets=entry.targets;
      const auto& sizes=entry.sizes;
      const auto maximum=entry.maximum;
      const bool canvas_xy=entry.canvas_xy;
      // Global vector capacity is live even though required native sizes are
      // immutable. Check it before reading any payload, as on the old path.
      if(global && (value.available>4096 || maximum>size_t(value.available)*16))
        throw std::runtime_error("native global constant exceeds owned vector: "+parameter.name);
      const auto* data=source.Bytes(value.data,maximum);
      if(REXCVAR_GET(edf_native_instance_motion_trace)>0 && !stage_offset && parameter.name=="g_mWorldArray") {
        // Palette storage may be shared scratch rewritten between draws. Track
        // both between-frame and within-frame changes before choosing a key.
        struct PaletteSample {
          uint64_t frame=0,hash=0,samples=0,changes=0,within_frame=0;
          uint32_t registers=0;
        };
        static std::map<std::pair<uint32_t,uint32_t>,PaletteSample> samples;
        static uint64_t start_frame=0;
        static bool finished=false;
        if(!finished) {
          if(!start_frame) start_frame=state.scene_frames;
          const auto frames=REXCVAR_GET(edf_native_instance_motion_trace);
          if(state.scene_frames-start_frame>=uint64_t(frames)) {
            for(const auto& [key,sample]:samples)
              REXLOG_INFO("Native palette motion: record={:#x} data={:#x} registers={} samples={} changes={} same_frame_changes={}",
                key.first,key.second,sample.registers,sample.samples,sample.changes,sample.within_frame);
            REXLOG_INFO("Native palette motion: complete frames={} sources={}",frames,samples.size());
            finished=true; samples.clear();
          } else {
            const auto key=std::make_pair(parameter.record,value.data);
            auto found=samples.find(key);
            if(found==samples.end() && samples.size()<64) {
              found=samples.emplace(key,PaletteSample{}).first;
              found->second.registers=parameter.registers;
              REXLOG_INFO("Native palette identity: record={:#x} data={:#x} registers={} first={} global={} bytes={}",
                parameter.record,value.data,parameter.registers,parameter.first,global,maximum);
            }
            if(found!=samples.end()) {
              auto& sample=found->second;
              uint64_t hash=14695981039346656037ull;
              for(size_t i=0;i<maximum;++i) { hash^=data[i]; hash*=1099511628211ull; }
              if(!sample.samples || sample.frame!=state.scene_frames) {
                if(sample.samples && sample.hash!=hash) ++sample.changes;
                ++sample.samples; sample.frame=state.scene_frames;
              } else if(sample.hash!=hash) ++sample.within_frame;
              sample.hash=hash;
            }
          }
        }
      }
      std::array<uint8_t,16> canvas_data{};
      if(canvas_xy && NativeRenderDimensions()[0]>0 && maximum==16) {
        // Kept raw for the Utility draw's per-target canvas layout.
        const bool offset=parameter.name=="_g_DX2DOffset";
        std::memcpy(offset?shader.canvas_offset.data():shader.canvas_scale.data(),data,16);
        shader.canvas_uploaded|=offset?2:1;
        canvas_data=ScaleNativeCanvasXY({data,16},float(NativeRenderDimensions()[0])/1280.0f,
          float(NativeRenderDimensions()[1])/720.0f);
        data=canvas_data.data();
      }
      for(size_t index=0;index<destinations.size();++index) if(targets[index]) {
        if(destinations[index]->SetGuestFloatRegisters(*targets[index],{data,sizes[index]})) ++state.parameter_uploads;
        else ++state.optimized_out;
      }
    }
    };
    // Windowing the scattered payload reads as well was measured and did not
    // pay: the pixel stage improved 14% but the vertex stage lost 5% to the
    // extra pass, and activation.native did not move outside the 5% run-to-run
    // drift of the untouched phases. Not worth a second pass and a fallback in
    // this routine.
    if(record_base && record_bytes)
      upload(edf::native::GuestReadWindow(reader,record_base,record_bytes));
    else upload(reader);
  }
}
// `samplers` reads the device's sampler block; it is windowed by the caller
// because these words lie outside the material, and reading them through the
// material's window fell back to a validated read per texture per activation.
template<class Reader,class SamplerReader>
void UploadTextures(const Reader& reader, const SamplerReader& samplers,
                    const std::shared_ptr<const NativeMaterialParameters::Groups>& owned,
                    RegisteredShader& shader, uint32_t device, ShaderBindings& bindings, Bridge& state) {
  bindings.BeginResourceUpdate();
  for (bool global : {false, true}) {
    const auto& resolved=shader.ResolveTextures(owned,global?1:0);
    size_t index=0;
    for(const auto& parameter:owned->textures[global?1:0]) {
      const auto& target=resolved[index++];
      const auto value=parameter.ReadValue(reader,global);
      const auto& name=parameter.name;
      const auto handle=value.handle;
      const auto found = state.textures.find(handle);
      const std::shared_ptr<NativeBackendTexture> missing_texture;
      const auto& texture = found == state.textures.end() || !found->second.content_valid
        ? missing_texture : found->second.backend;
      auto* view = texture.get();
      if (!bindings.TrySetTexture(target, texture)) {
        // Guest material records describe Xbox compiler usage. A native entry
        // can optimize a combined sampler out; it then has neither binding.
        // Do not let an unused record erase the material's remaining textures.
        if (bindings.TrySetSampler(target,nullptr))
          throw std::runtime_error("native combined sampler has no texture binding: " + name);
        continue;
      }
      // Read after the original activation has applied named filtering and
      // texture mip limits. Inline engine address changes are already present.
      edf::native::HookTiming sampler_timing(edf::native::HookPhase::ActivationSamplerWords);
      const auto key=NativeFilteringKey(ReadSamplerWords(samplers,device,value.slot),
                                        REXCVAR_GET(edf_native_anisotropic_filtering));
      auto cached = state.samplers.find(key);
      sampler_timing.Finish();
      if (cached == state.samplers.end()) {
        // Decoded by the shared guest decoder, not by a D3D11-shaped one, so a
        // second backend cannot filter this material differently.
        const auto desc = DecodeNativeGuestSampler(key);
        cached = state.samplers.emplace(key,&EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
        REXLOG_INFO("Native sampler: cached={}, address={}/{}/{}, filter={}/{}/{}, lod={}..{}, bias={}, anisotropy={}",
                    state.samplers.size(),uint32_t(desc.u),uint32_t(desc.v),uint32_t(desc.w),
                    uint32_t(desc.min),uint32_t(desc.mag),uint32_t(desc.mip),
                    desc.min_lod,desc.max_lod,desc.mip_lod_bias,desc.max_anisotropy);
      }
      if(!bindings.TrySetSampler(target,cached->second))
        throw std::runtime_error("unknown sampler: "+name);
      ++state.sampler_bindings;
      if (handle && !view) {
        ++state.texture_missing;
        if (state.texture_missing <= 10)
          REXLOG_INFO("Native texture bridge: pending resource name={}, handle={:#x}", name, handle);
        if (state.texture_missing <= 10) {
          const auto creation = state.texture_creations.find(handle);
          if (creation != state.texture_creations.end()) {
            const auto& c = creation->second;
            REXLOG_INFO("Native texture bridge: pending allocation {}x{}x{}, levels={}, usage={:#x}, format={:#x}, pool={}, type={}, caller={:#x}",
                        c.width, c.height, c.depth, c.levels, c.usage, c.format, c.pool, c.type, c.caller);
          }
        }
      } else ++state.texture_bindings;
    }
  }
  bindings.EndResourceUpdate();
}
// Whether this shader samples the target it is drawing into.
//
// What a shader can actually sample is the resolved texture, or the surface
// itself when the target is one of the converting ones that declares it
// sampled. Both are checked; neither is a D3D11 pointer, because this used to
// compare those and a backend without them made every answer yes.
bool SamplesTarget(const ShaderBindings& shader,const NativeRenderTarget& target) {
  if(target.sampled.backend && shader.UsesTexture(*target.sampled.backend)) return true;
  if(target.backend_surface)
    if(auto* surface=target.backend_surface->texture())
      if(shader.UsesTexture(*surface)) return true;
  return false;
}
struct NativeQueuedSceneGroup {
  NativeSceneExecution execution;
  std::optional<GuestViewportWords> pass_viewport;
  NativeSceneMaterialHandoff material_handoff;
  NativeSceneGeometryHandoff geometry_handoff;
  NativeSceneGeometryInstallState geometry_install;
  bool material_compatibility_only=true;
  bool published_activation_used=false,all_native_draws=true;
  uint32_t activation_instance=0;
  std::shared_ptr<const NativeSceneGroupMaterial> published_material;
  std::vector<NativeSceneMaterialInputs::Constant> pass_constants;
  std::optional<NativeMaterialRenderPass> material_pass;
  std::array<NativeMaterialSamplerPass,16> sampler_pass{};
  bool material_audited=false;
  std::vector<std::shared_ptr<const NativeSceneInstance>> objects;
  std::shared_ptr<const NativeSceneMaterial> material;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  bool reverse_depth=false;
  uint32_t vertex=0,pixel=0;
  std::optional<VertexParameterRange> world_parameter;
  bool world_column_major=false,constants_clean=false,population_safe=true;
  std::optional<std::array<uint8_t,64>> pending_world;
  NativeSceneView view;
  ActiveTargets targets;
  uint32_t instance=0;
};
thread_local NativeQueuedSceneGroup* native_queued_scene_group=nullptr;
void EnterNativeSceneBoundary(NativeSceneBoundary boundary) {
  if(native_queued_scene_group)
    native_queued_scene_group->execution.Enter(boundary,REXCVAR_GET(edf_native_scene_reject_compatibility));
}
// source is sources.Find(instance), already looked up by the caller.
template<class Reader>
void ReadNativeSceneInstanceParameters(const Reader& reader,const NativeSceneSources::Source* source,
    uint32_t instance,std::vector<InstanceParameter>& parameters) {
  if(!REXCVAR_GET(edf_native_scene_instance_owned) || !source || !source->world_first) {
    ReadInstanceParameters(reader,instance,parameters); return;
  }
  parameters.assign(1,InstanceParameter{source->world_data,*source->world_first,4});
  if(REXCVAR_GET(edf_native_scene_transform_audit)) {
    const auto actual=ReadInstanceParameters(reader,instance);
    if(actual!=parameters) {
      REXLOG_ERROR("Native instance metadata mismatch: instance={:#x} owner={:#x}",instance,source->owner);
      throw std::runtime_error("published world-only instance metadata changed without a source event");
    }
  }
  static uint64_t count=0;
  if(++count<=4 || count%100000==0)
    REXLOG_INFO("Native owned instance metadata: reads={} audited={}",count,REXCVAR_GET(edf_native_scene_transform_audit));
}
template<class Reader>
void ReadNativeSceneInstanceParameters(const Reader& reader,const NativeSceneSources& sources,
    uint32_t instance,std::vector<InstanceParameter>& parameters) {
  ReadNativeSceneInstanceParameters(reader,sources.Find(instance),instance,parameters);
}
// source is sources.Find(instance). Owned metadata without the transform
// audit is exactly the source generation's (ReadNativeSceneInstanceParameters
// would only copy it), so it is decided from the source without building the
// parameter list; anything else reads the parameters as before.
template<class Reader>
bool NativeStaticWorldOnly(const Reader& reader,const NativeSceneSources::Source& source,
                          uint32_t instance,uint32_t first,uint32_t device) {
  if(!source.world_data) return false;
  if(REXCVAR_GET(edf_native_scene_instance_owned) && source.world_first && !REXCVAR_GET(edf_native_scene_transform_audit))
    return NativeStaticWorldOnlySource(source,first,device);
  static thread_local std::vector<InstanceParameter> parameters;
  ReadNativeSceneInstanceParameters(reader,&source,instance,parameters);
  return parameters.size()==1 && parameters[0].count==4 && parameters[0].first==first &&
    parameters[0].data==source.world_data &&
    !(uint64_t(parameters[0].data)<uint64_t(device)+5888 && uint64_t(parameters[0].data)+64>uint64_t(device)+1792);
}
template<class Reader>
bool NativeStaticWorldOnly(const Reader& reader,const NativeSceneSources& sources,
                          uint32_t instance,uint32_t first,uint32_t device) {
  const auto* source=sources.Find(instance);
  return source && NativeStaticWorldOnly(reader,*source,instance,first,device);
}
std::shared_ptr<const NativeSceneInstance> SelectNativeSceneInstanceLocked(Bridge& state,uint64_t id) {
  const auto current=state.scene_adapter.SelectOne(id);
  const auto published=native_scene_publication?native_scene_publication->Find(id):nullptr;
  if(published && published->object==current->object) {
    ++state.scene_published_selections;
    return published;
  }
  ++state.scene_current_selections;
  return current;
}
bool TryAppendNativeQueuedSceneInstance(uint8_t* base,uint32_t device,uint32_t instance,NativeQueuedSceneGroup& group);
bool TryAppendPublishedNativeSceneInstance(uint8_t* base,uint32_t device,uint32_t address,uint32_t instance,uint32_t count,NativeQueuedSceneGroup& group);
void SynchronizeNativeQueuedSceneInstance(uint8_t* base,uint32_t device,NativeQueuedSceneGroup& group);
void ConfigureNativeQueuedWorldLocked(Bridge& state,NativeQueuedSceneGroup& group) {
  group.world_parameter.reset(); group.constants_clean=false;
  size_t matrices=0;
  for(const auto& constant:group.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) {
      if(constant.stage!=NativeBackendStage::Vertex) return;
      ++matrices; group.world_column_major=matrix.column_major;
    }
  if(matrices!=1) return;
  if(group.published_material) {
    const auto& definition=group.published_material->program->inputs;
    const auto first=definition.WorldRegisterFirst();
    if(!first) return;
    auto& shader=state.shaders.at(definition.vertex);
    if(!shader.reversed_bindings) return;
    auto normal=shader.bindings->ResolveFloatRegisters("g_mWorld");
    auto reversed=shader.reversed_bindings->ResolveFloatRegisters("g_mWorld");
    if(normal.bytes()!=64 || reversed.bytes()!=64) return;
    group.world_parameter=VertexParameterRange{"g_mWorld",*first,4,std::move(normal),std::move(reversed)};
    return;
  }
  if(!state.active_vertex_parameters) return;
  for(const auto& parameter:*state.active_vertex_parameters)
    if(parameter.name=="g_mWorld" && parameter.count==4 && parameter.normal.bytes()==64 && parameter.reversed.bytes()==64) {
      for(const auto& other:*state.active_vertex_parameters)
        if(&other!=&parameter && other.first<parameter.first+4 && other.first+other.count>parameter.first) return;
      group.world_parameter=parameter; return;
    }
}
void FlushNativeQueuedSceneLocked(Bridge& state,NativeQueuedSceneGroup& group) {
  if(group.objects.empty()) return;
  auto& recorder=SceneRecorderLocked(state);
  recorder.SetRenderTargets({group.targets.colors.data(),group.targets.count},group.targets.depth);
  const auto statistics=group.execution.RecordBatch([&] {
    // Keep pending objects until the complete batch has been recorded. A
    // partial GPU recording cannot be rolled back or safely repeated.
    state.scene_recorded_snapshots.push_back(NativeSceneSnapshot{0,group.objects});
    return state.scene_renderer.Render(*state.scene_backend,state.scene_recorded_snapshots.back(),group.view,1);
  });
  state.scene_native_objects+=statistics.visible; state.scene_native_draws+=statistics.draws;
  group.objects.clear();
  // The batch bound its own targets and pipelines, like a resolve: neither the
  // recorder cache nor the indexed skip may compare against what came before.
  ++state.bind_generation;
  state.recorded={};
}
// render_states[key], created on first use, as the UI draw paths look it up:
// a std::map search of array keys per draw, where consecutive draws almost
// always ask for the key they asked for last.
NativeRenderState& RenderStateLocked(Bridge& state,const RenderStateWords& key) {
  auto& memo=state.render_state_memo;
  // Reuse off (native_reuse.h): the map is searched (the memo still records).
  if(NativeReuseAllowed()) {
    if(memo[0].value && memo[0].key==key) return *memo[0].value;
    if(memo[1].value && memo[1].key==key) { std::swap(memo[0],memo[1]); return *memo[0].value; }
  }
  auto found=state.render_states.find(key);
  if(found==state.render_states.end())
    found=state.render_states.emplace(key,CreateNativeRenderState(state.device.Get(),key)).first;
  memo[1]=memo[0]; memo[0]={key,&found->second};
  return found->second;
}
// The immediate hook's pair classification, memoized (native_immediate_classify.h).
const NativeImmediatePairMemo<Bridge::ImmediatePairShaders>::Entry&
ClassifyImmediatePairLocked(Bridge& state,const GuestShaderPair& pair) {
  return state.immediate_pairs.Get(pair.vertex,pair.pixel,state.shader_registry_generation,
    state.embedded_shader_generation,[&](NativeImmediatePairClass& kind,Bridge::ImmediatePairShaders& shaders) {
      const auto vertex_embedded=state.embedded_shaders.find(pair.vertex);
      const auto pixel_embedded=state.embedded_shaders.find(pair.pixel);
      const auto vertex=state.shaders.find(pair.vertex),pixel=state.shaders.find(pair.pixel);
      NativeEmbeddedIdentity vertex_identity,pixel_identity;
      NativeSourceIdentity vertex_source,pixel_source;
      const bool embedded=vertex_embedded!=state.embedded_shaders.end() &&
                          pixel_embedded!=state.embedded_shaders.end();
      if(embedded) {
        vertex_identity={vertex_embedded->second.source,vertex_embedded->second.pixel};
        pixel_identity={pixel_embedded->second.source,pixel_embedded->second.pixel};
      }
      const bool registered=vertex!=state.shaders.end() && pixel!=state.shaders.end();
      if(registered) {
        const auto& vs=vertex->second.bindings->shader();
        const auto& ps=pixel->second.bindings->shader();
        vertex_source={vs.source_fingerprint,vs.entry.name};
        pixel_source={ps.source_fingerprint,ps.entry.name};
      }
      kind=ClassifyNativeImmediatePair(embedded?&vertex_identity:nullptr,embedded?&pixel_identity:nullptr,
        registered?&vertex_source:nullptr,registered?&pixel_source:nullptr);
      shaders={registered?&vertex->second:nullptr,registered?&pixel->second:nullptr};
    });
}
// The live value of each global g_mWorld of published's constants, as 821B94E8
// would upload it now. The preload keeps the published copy stable
// (NativeSceneObjectWorldConstant); the guest path's pass constants, which its
// published activation binds to the live shader bindings, take it from here.
// Throws when the group's load no longer matches the publication's program.
void ReadNativeSceneLiveWorldLocked(Bridge& state,const GuestReader& reader,const NativeSceneGroupMaterial& published,
    std::vector<NativeSceneMaterialInputs::Constant>& constants) {
  const auto found=state.scene_material_loads.find(published.group);
  if(found==state.scene_material_loads.end() || !found->second.published || !found->second.schema ||
     found->second.published->program!=published.program || found->second.constants.size()!=constants.size())
    throw std::runtime_error("native scene material load does not match its publication");
  const auto& load=found->second;
  for(size_t i=0;i<constants.size();++i) {
    const auto& slot=load.constants[i];
    if(!slot.object_world) continue;
    if(!constants[i].global || constants[i].name!="g_mWorld") throw std::runtime_error("native scene material world slot moved");
    const auto* data=ReadNativeSceneMaterialConstant(reader,*load.schema,slot);
    constants[i].registers.assign(data,data+slot.bytes);
  }
}
bool ObservePublishedActivation(uint32_t instance) {
  auto* group=native_queued_scene_group;
  if(!group || group->activation_instance!=instance || !group->published_material || !group->material_pass)
    return false;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto& published=*group->published_material;
  const auto latest=state.scene_adapter.GroupMaterial(published.group,published.revision);
  const auto* membership=NativeSceneSourcesForPass(state).FindGroup(published.group);
  if(!latest || latest->program!=published.program || !membership || membership->revision!=published.revision)
    return false;
  const auto& program=*published.program;
  const auto vertex=program.inputs.vertex,pixel=program.inputs.pixel;
  if(!state.shaders.contains(vertex) || !state.shaders.contains(pixel) || program.backend!=state.scene_backend)
    return false;
  auto& shader=state.shaders.at(vertex);
  auto& vs=*shader.bindings;
  auto& ps=*state.shaders.at(pixel).bindings;
  if(!shader.reversed_bindings || vs.shader().bytecode!=program.vertex.bytecode ||
     shader.reversed_bindings->shader().bytecode!=program.reversed_vertex.bytecode ||
     ps.shader().bytecode!=program.pixel.bytecode) return false;
  auto& reversed=*shader.reversed_bindings;
  const std::pair<uint32_t,uint32_t> link{vertex,pixel};
  if(!state.validated_links.contains(link)) {
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    ValidateNativeShaderLink(reversed.shader(),ps.shader());
    state.validated_links.insert(link);
  }
  auto ranges=shader.ResolvePublishedVertexRanges(published.program);
  const auto resolved=program.ResolveSamplers(group->sampler_pass);
  std::vector<NativeBackendSampler*> samplers;
  for(const auto& texture:program.inputs.textures) {
    if(texture.slot>=resolved.size()) throw std::runtime_error("invalid published activation sampler slot");
    const auto key=NativeFilteringKey(resolved[texture.slot].words,REXCVAR_GET(edf_native_anisotropic_filtering));
    auto cached=state.samplers.find(key);
    if(cached==state.samplers.end())
      cached=state.samplers.emplace(key,&program.backend->CreateSampler(DecodeNativeGuestSampler(key))).first;
    samplers.push_back(cached->second);
  }
  state.active_vertex=0;
  state.active_vertex_parameters.reset();
  program.ApplyBindings(vs,ps,group->pass_constants,samplers);
  if(!shader.reversed_mirrors) program.ApplyBindings(reversed,ps,group->pass_constants,samplers);
  if(state.context) { vs.Bind(*state.context.Get()); ps.Bind(*state.context.Get()); }
  state.linked_vertex=vertex; state.linked_pixel=pixel;
  state.active_vertex_parameters=std::move(ranges);
  state.active_vertex=vertex;
  group->published_activation_used=true;
  ++state.activations;
  static uint64_t count=0;
  if(++count<=4 || count%100000==0)
    REXLOG_INFO("Native published activation: count={} (no live material constants or texture reads)",count);
  return true;
}
void ObserveActivation(const GuestReader& backing, uint32_t instance, uint32_t device) {
  // Per-stage vector headers, texture vectors and pass pointer share this
  // material object. Validate once, retaining no window beyond activation.
  const GuestReadWindow reader(backing,instance,112);
  // The device's sixteen sampler records, validated once for the activation.
  const GuestReadWindow sampler_reader(backing,backing.Add(device,1024),16*24);
  const auto pass = reader.Word(reader.Add(instance, 108));
  const auto vertex = reader.Word(reader.Word(pass));
  const auto pixel = reader.Word(reader.Add(reader.Word(reader.Add(pass, 4)), 4));
  auto& state = State();
  // Sub-phases: activation is the most expensive per-call hook in gameplay and
  // nobody has measured which part of it that is. Lock wait is separated from
  // work because both scale differently with thread contention.
  edf::native::HookTiming lock_timing(edf::native::HookPhase::ActivationLock);
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  lock_timing.Finish();
  edf::native::HookTiming resolve_timing(edf::native::HookPhase::ActivationResolve);
  ++state.activations;
  // Repeat activations of one material with unchanged shaders are the cheapest
  // thing to skip if they dominate, so count them before optimising anything.
  if(instance==state.last_activation_instance && vertex==state.last_activation_vertex &&
     pixel==state.last_activation_pixel) ++state.repeat_activations;
  state.last_activation_instance=instance;
  state.last_activation_vertex=vertex; state.last_activation_pixel=pixel;
  if(state.activations%250000==0)
    REXLOG_INFO("Native activation repeats: activations={}, repeats_of_previous={} ({:.1f}% identical instance and shader pair as the immediately preceding activation)",
      state.activations,state.repeat_activations,100.0*double(state.repeat_activations)/double(state.activations));
  // One lookup per shader, kept: nothing below inserts into or erases from the
  // shader registry, so both entries stay where they were found.
  const auto vertex_entry=state.shaders.find(vertex),pixel_entry=state.shaders.find(pixel);
  const bool found = vertex_entry!=state.shaders.end() && pixel_entry!=state.shaders.end();
  state.active_vertex = 0;
  state.active_vertex_parameters.reset();
  if (!found) ++state.misses;
  if (found) {
    try {
      auto& vertex_shader=vertex_entry->second;
      auto& pixel_shader=pixel_entry->second;
      const auto material=state.material_parameters.Get(instance);
      auto& vs = *vertex_shader.bindings;
      auto& reversed = *vertex_shader.reversed_bindings;
      auto& ps = *pixel_shader.bindings;
      if (state.linked_vertex != vertex || state.linked_pixel != pixel) {
        const std::pair<uint32_t,uint32_t> link{vertex,pixel};
        if(!state.validated_links.contains(link)) {
          ValidateNativeShaderLink(vs.shader(),ps.shader());
          ValidateNativeShaderLink(reversed.shader(),ps.shader());
          state.validated_links.insert(link); // After both passed, never before.
        }
        state.linked_vertex = vertex; state.linked_pixel = pixel;
      }
      const auto vertex_ranges=vertex_shader.ResolveVertexRanges(material);
      // A stage whose native shader consumes a Common.fx global that this
      // material never lists for that stage keeps the value our own HLSL
      // compilation baked in. Distinguish that from an upload the bridge drops:
      // the guest's own records decide a stage's parameter list.
      // Audit the first activation of each distinct shader pair only. The
      // per-stage parameter list belongs to the material's shaders, not to the
      // draw, so one sample per pair is complete, and gameplay activates
      // hundreds of thousands of times a second.
      if(const auto audit=REXCVAR_GET(edf_native_shared_constant_audit);
         audit>0 && state.shared_constant_pairs.size()<uint64_t(audit) &&
         state.shared_constant_pairs.insert({vertex,pixel}).second) {
        ++state.shared_constant_activations;
        const auto owned=state.material_parameters.Get(instance);
        const auto supplied=[&](size_t stage,const std::string& name) {
          for(size_t group=stage*2;group<stage*2+2;++group)
            for(const auto& parameter:(*owned)[group]) if(parameter.name==name) return true;
          return false;
        };
        // Where both stages are supplied, compare the storage each one names.
        // Identical source bytes with different shader values would be an
        // upload defect; different source addresses are the guest's own layout.
        const auto global_source=[&](size_t stage,const std::string& name)
            ->std::optional<std::array<uint32_t,3>> {
          for(const auto& parameter:(*owned)[stage*2+1]) {
            if(parameter.name!=name) continue;
            const auto node=GuestBlockWord(reader.Bytes(parameter.record,4));
            const auto value=parameter.ReadValue(reader,true);
            return std::array<uint32_t,3>{parameter.record,node,value.data};
          }
          return std::nullopt;
        };
        for(const auto* name:{"g_LightVector","g_LightDiffuse","g_HemiSphereVector",
            "g_HemiSphereColor1","g_HemiSphereColor2","g_FogParam","g_FogColor"}) {
          // Declaring a constant is not reading it: the reflection lists every
          // Common.fx global in the buffer, so a binding's existence says
          // nothing about whether this stage's pixels depend on the value.
          const bool vertex_used=vs.ConsumesConstant(name);
          const bool pixel_used=ps.ConsumesConstant(name);
          const bool vertex_supplied=supplied(0,name),pixel_supplied=supplied(1,name);
          if(vertex_supplied || pixel_supplied) {
            const auto vertex_source=global_source(0,name),pixel_source=global_source(1,name);
            if(vertex_used && pixel_used && vertex_supplied && pixel_supplied)
              ++state.shared_constant_both_supplied;
            if(vertex_source || pixel_source) {
              const std::array<uint32_t,3> storage_identity{vertex,pixel,uint32_t(name[2])*7+uint32_t(name[3])};
              if(state.shared_constant_storage_reported.size()<64 &&
                 state.shared_constant_storage_reported.insert(storage_identity).second) {
                const auto first=[&](uint32_t data)->float {
                  return data ? std::bit_cast<float>(GuestBlockWord(reader.Bytes(data,4))) : 0.f;
                };
                const auto vertex_data=vertex_source?(*vertex_source)[2]:0u;
                const auto pixel_data=pixel_source?(*pixel_source)[2]:0u;
                REXLOG_INFO("Native shared constant storage: name={}, VS={:#x} {} uses={} data={:#x} first={}, PS={:#x} {} uses={} data={:#x} first={}, same_data={}",
                  name,vertex,vs.shader().entry.name,vertex_used,vertex_data,first(vertex_data),
                  pixel,ps.shader().entry.name,pixel_used,pixel_data,first(pixel_data),
                  vertex_data && pixel_data && vertex_data==pixel_data);
              }
              if(vertex_source && pixel_source && (*vertex_source)[2]!=(*pixel_source)[2])
                ++state.shared_constant_split_storage;
            }
          }
          if((vertex_used && !vertex_supplied)||(pixel_used && !pixel_supplied)) {
            ++state.shared_constant_unsupplied;
            const std::array<uint32_t,3> identity{vertex,pixel,uint32_t(std::string_view(name).size()*131+name[2])};
            if(state.shared_constant_reported.size()<64 &&
               state.shared_constant_reported.insert(identity).second)
              REXLOG_WARN("Native shared constant unsupplied: name={}, VS={:#x} {} uses={} supplied={}, PS={:#x} {} uses={} supplied={}, source={:#x} (an unsupplied stage keeps the compiled source default)",
                name,vertex,vs.shader().entry.name,vertex_used,vertex_supplied,
                pixel,ps.shader().entry.name,pixel_used,pixel_supplied,
                vs.shader().source_fingerprint);
          }
        }
        const auto seen=state.shared_constant_activations;
        if(seen<=4 || (seen&(seen-1))==0)
          REXLOG_INFO("Native shared constant audit: pairs={}, unsupplied_uses={}, distinct_unsupplied={}, both_supplied={}, split_storage={} (one sample per distinct shader pair; split_storage counts pairs whose two stages name different guest value storage)",
            seen,state.shared_constant_unsupplied,state.shared_constant_reported.size(),
            state.shared_constant_both_supplied,state.shared_constant_split_storage);
      }
      resolve_timing.Finish();
      { edf::native::HookTiming vertex_params(edf::native::HookPhase::ActivationVertexParams);
        UploadParameters(reader, material, vertex_shader, 0, vs, state,
                         vertex_shader.reversed_mirrors?nullptr:&reversed); }
      { edf::native::HookTiming pixel_params(edf::native::HookPhase::ActivationPixelParams);
        UploadParameters(reader, material, pixel_shader, 36, ps, state); }
      edf::native::HookTiming texture_timing(edf::native::HookPhase::ActivationTextures);
      try { UploadTextures(reader, sampler_reader, material, pixel_shader, device, ps, state); }
      catch (const std::exception& error) {
        ++state.texture_binding_errors;
        if (state.texture_binding_errors <= 10)
          REXLOG_ERROR("Native texture binding: {} (instance={:#x})", error.what(), instance);
        ps.ClearTextures();
        ps.ClearSamplers();
      }
      texture_timing.Finish();
      if(state.context) { edf::native::HookTiming bind_timing(edf::native::HookPhase::ActivationBind);
        vs.Bind(*state.context.Get());
        ps.Bind(*state.context.Get()); }
      state.active_vertex_parameters=vertex_ranges;
      state.active_vertex = vertex;
    } catch (const std::exception& error) {
      ++state.parameter_errors;
      if (state.parameter_errors <= 10)
        REXLOG_ERROR("Native parameter bridge: {} (instance={:#x})", error.what(), instance);
    }
  }
  if (state.activations <= 5 || state.activations % 10000 == 0)
    REXLOG_INFO("Native shader bridge: activation={}, VS={:#x}, PS={:#x}, resolved={}, misses={}, uploads={}, optimized_out={}, parameter_errors={}, texture_bindings={}, texture_missing={}, texture_binding_errors={}, sampler_bindings={}, sampler_states={}",
                state.activations, vertex, pixel, found, state.misses, state.parameter_uploads,
                state.optimized_out, state.parameter_errors, state.texture_bindings,
                state.texture_missing, state.texture_binding_errors,state.sampler_bindings,state.samplers.size());
}
}
template <typename Reader>
GuestXuiDeviceWords ReadAuditedXuiDeviceWords(const Reader& reader,uint32_t device) {
  if(REXCVAR_GET(edf_native_owned_render_state))
    return ReadXuiDeviceWords(reader,device,ReadAuditedRenderStateWords(reader,device));
  auto snapshot=ReadXuiDeviceWords(reader,device);
  AuditNativeRenderState(device,snapshot.render);
  return snapshot;
}
}
REX_EXTERN(sub_821B94E8);
REX_EXTERN(sub_821C3BB8);
REX_EXTERN(sub_821D96D8);
REX_EXTERN(__imp__sub_821D96D8);
REX_EXTERN(sub_82137410);
REX_EXTERN(sub_82149A90);
REX_EXTERN(sub_821375C0);
REX_EXTERN(sub_821B94E8);
REX_EXTERN(sub_821D9600);
REX_EXTERN(sub_821FE358);
REX_EXTERN(__imp__edf_native_indexed_cpu_tail);
namespace {
void InstallNativeStaticGeometry(PPCContext&,uint8_t*,uint32_t,const edf::native::NativeSceneGeometrySource&,edf::native::NativeSceneGeometryInstallState&);
void RestoreNativeStaticMaterial(PPCContext&,uint8_t*,uint32_t,uint32_t);
}
REX_HOOK_RAW(sub_821D96D8) {
  edf::native::HookTiming timing(edf::native::HookPhase::RenderMaterialGroup);
  edf::native::NativeQueuedSceneGroup group;
  const auto group_address=ctx.r3.u32;
  struct Restore {
    edf::native::NativeQueuedSceneGroup& group;
    uint32_t address;
    edf::native::NativeQueuedSceneGroup* previous=edf::native::native_queued_scene_group;
    ~Restore() {
      edf::native::native_queued_scene_group=previous;
      // Report aborted groups too. Logging must never replace their exception.
      try {
        static std::array<std::atomic<uint64_t>,size_t(1)<<uint32_t(edf::native::NativeSceneBoundary::Count)> shapes{};
        const auto& run=group.execution;
        const auto count=++shapes.at(run.mask());
        if(!run.complete() || count<=4 || !(count&(count-1)))
          REXLOG_INFO("Native static group execution: group={:#x} completed={} recorded_batches={} compatibility_calls={} boundary_mask={:#x} occurrences={}",
            address,run.complete(),run.recordings(),run.Total(),run.mask(),count);
      } catch(...) {}
    }
  } restore{group,group_address};
  edf::native::native_queued_scene_group=&group;
  if(!EDF_NATIVE_FLAG(scene_queued)) {
    // No queued group without the queued scene: the indexed draw hook treats a
    // set group as a queued instance, and every guest draw then threw and
    // recorded its setup twice (about 7 ms/frame in Mission 1 gameplay).
    edf::native::native_queued_scene_group=nullptr;
    if(edf::native::native_material_pass_cursor) edf::native::native_material_pass_cursor->reset();
    __imp__sub_821D96D8(ctx,base); group.execution.Complete(); return;
  }
  size_t native_queue_size=0;
  uint32_t group_device=0;
  if(EDF_NATIVE_FLAG(host) && EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(seam_draws)) {
    const edf::native::GuestReader reader(base);
    const auto address=ctx.r3.u32;
    auto instances=edf::native::native_scene_queues?edf::native::native_scene_queues->Take(address):std::vector<uint32_t>{};
    native_queue_size=instances.size();
    if(native_queue_size && reader.Word(reader.Add(address,4))!=reader.Word(reader.Add(address,8)))
      throw std::runtime_error("untracked guest producer modified a native scene queue");
    auto work=ctx;
    if(work.r1.u32<128) throw std::runtime_error("invalid native scene group stack");
    work.r1.u64=work.r1.u32-128;
    reader.StoreWord(work.r1.u32,ctx.r1.u32);
    uint32_t device=0,index_count=0;
    const auto install_geometry=[&](const edf::native::NativeSceneGeometrySource& input) {
      InstallNativeStaticGeometry(work,base,device,input,group.geometry_install);
    };
    const auto finish_geometry=[&] {
      if(!group.geometry_handoff.pending()) return;
      const auto restore_material=[&] {
        if(group.material_handoff.pending()) {
          // Activation writes material defaults into the register bank. The
          // final instance world must be restored afterwards, never before.
          group.material_handoff.Finish([&] {
            RestoreNativeStaticMaterial(work,base,group.activation_instance,device);
          },[&] { edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group); });
          static uint64_t count=0;
          if(++count<=4 || count%100000==0)
            REXLOG_INFO("Native material handoff: groups={} (CPU activation follows native submission; final instance restored)",count);
        }
      };
      edf::native::FinishNativeSceneGroupHandoff(group.geometry_handoff,
        [&] { edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group); },
        [&] {
          auto& state=edf::native::State();
          std::lock_guard submission(state.submissions);
          std::lock_guard lock(state.mutex);
          edf::native::FlushNativeQueuedSceneLocked(state,group);
        },install_geometry,restore_material,[&] {
        work.r3.u64=device; work.r4.u64=4; work.r5.u64=0; work.r6.u64=0; work.r7.u64=index_count;
        work.lr=0x821D97E8; __imp__edf_native_indexed_cpu_tail(work,base);
      },[&] { group.constants_clean=false; });
      if(group.geometry) {
        static uint64_t count=0;
        if(++count<=4 || count%100000==0)
          REXLOG_INFO("Native geometry handoff: groups={} (native submission precedes CPU buffer/declaration setup and draw tail)",count);
      }
    };
    const auto prepare=[&] {
      std::optional<edf::native::NativeSceneGeometrySource> published_setup;
      {
        auto& state=edf::native::State();
        std::lock_guard lock(state.mutex);
        group.material=state.scene_adapter.PreviousGroupMaterial(group_address);
        if(EDF_NATIVE_FLAG(scene_geometry_owned) && edf::native::native_scene_publication) {
          const auto* source=edf::native::NativeSceneSourcesForPass(state).FindGroup(group_address);
          if(source) {
            const auto latest=state.scene_adapter.GroupGeometry(group_address,source->revision);
            for(const auto& published:edf::native::native_scene_publication->group_geometry)
              if(published==latest && published->group==group_address && published->setup &&
                 published->geometry->backend()==state.scene_backend.get()) {
                published_setup=published->setup; break;
              }
          }
        }
      }
      device=reader.Word(reader.Add(reader.Word(0x8257BFB4),8));
      group_device=device;
      if(published_setup) {
        const auto& input=*published_setup;
        const auto eligibility=edf::native::AssessNativeStaticGroup(reader,device,work.r1.u32,input);
        group.material_compatibility_only=eligibility!=edf::native::NativeStaticGroupEligibility::Supported;
        if(group.material_compatibility_only) {
          edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::UnsupportedConfiguration);
          static std::array<std::atomic<uint64_t>,7> reasons{};
          const auto count=++reasons.at(size_t(eligibility));
          if(count<=4 || !(count&(count-1)))
            REXLOG_INFO("Native static group compatibility eligibility: reason={} count={}",unsigned(eligibility),count);
        }
        if(native_queue_size && REXCVAR_GET(edf_native_scene_geometry_deferred) &&
           REXCVAR_GET(edf_native_scene_activation_owned) && EDF_NATIVE_FLAG(scene_material_owned) &&
           !REXCVAR_GET(edf_native_scene_material_audit) && !group.material_compatibility_only) group.geometry_handoff.Defer(input);
        else install_geometry(input);
        index_count=input.count; work.r3.u64=input.material;
        static uint64_t count=0;
        if(++count<=4 || count%100000==0)
          REXLOG_INFO("Native published geometry setup: groups={} (owned descriptor inputs; CPU binding mirrors retained)",count);
      } else {
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::GeometrySetup);
      auto descriptor=reader.Word(reader.Add(address,12));
      work.r3.u64=device; work.r4.u64=0; work.r5.u64=reader.Add(descriptor,4);
      work.r6.u64=0; work.r7.u64=reader.Word(reader.Add(descriptor,60)); work.r8.u64=4096;
      work.lr=0x821D9730; sub_82137410(work,base);
      descriptor=reader.Word(reader.Add(address,12));
      const auto container=reader.Word(reader.Add(descriptor,72));
      const auto node=reader.Word(reader.Add(descriptor,76));
      if(!container || node==reader.Word(reader.Add(container,4)))
        throw std::runtime_error("native scene group has no index resource");
      work.r3.u64=device; work.r4.u64=reader.Word(reader.Add(node,28));
      work.lr=0x821D9764; sub_82149A90(work,base);
      descriptor=reader.Word(reader.Add(address,12));
      work.r3.u64=device; work.r4.u64=reader.Add(descriptor,84);
      work.lr=0x821D9774; sub_821375C0(work,base);
      descriptor=reader.Word(reader.Add(address,12));
      index_count=uint32_t((int32_t(reader.Word(reader.Add(descriptor,140)))/3)*3);
      work.r3.u64=reader.Word(reader.Add(reader.Word(descriptor),16));
      }
      group.activation_instance=work.r3.u32;
      if(REXCVAR_GET(edf_native_scene_material_audit) || EDF_NATIVE_FLAG(scene_material_owned)) {
        const auto publication=edf::native::native_scene_publication;
        if(publication) for(const auto& published:publication->group_materials)
          if(published->group==group_address) { group.published_material=published; break; }
        if(group.published_material) {
          try {
          if(!edf::native::native_scene_pass_camera) throw std::runtime_error("native material pass has no camera");
          group.pass_constants=group.published_material->constants;
          {
            // The published g_mWorld is not refreshed; the activation binds the live one.
            auto& state=edf::native::State();
            std::lock_guard lock(state.mutex);
            edf::native::ReadNativeSceneLiveWorldLocked(state,reader,*group.published_material,group.pass_constants);
          }
          for(auto& constant:group.pass_constants) {
            if(edf::native::native_scene_pass_camera->Apply(constant)) continue;
            if(!constant.global || (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
            if(!edf::native::native_scene_pass_animation) throw std::runtime_error("missing native animation pass");
            edf::native::native_scene_pass_animation->Apply(constant);
          }
          static std::set<std::string> changed_inputs;
          for(const auto& constant:group.pass_constants) {
            if(constant.name=="g_mWorld" || edf::native::NativeScenePassOwnedConstant(constant.global,constant.name)) continue;
            const auto& published=group.published_material->constants;
            const auto old=std::find_if(published.begin(),published.end(),[&](const auto& input) {
              return input.pixel==constant.pixel && input.global==constant.global && input.name==constant.name;
            });
            if(old!=published.end() && old->registers==constant.registers) continue;
            const auto key=std::string(constant.pixel?"PS ":"VS ")+(constant.global?"global ":"local ")+constant.name;
            if(changed_inputs.size()<64 && changed_inputs.insert(key).second)
              REXLOG_INFO("Native scene pass input changed since publication: {}",key);
          }
          const auto* cursor=edf::native::native_material_pass_cursor;
          if(cursor && *cursor && (*cursor)->device==device) {
            group.material_pass=(*cursor)->material.render;
            group.sampler_pass=(*cursor)->material.samplers;
            if(REXCVAR_GET(edf_native_scene_view_owned)) {
              group.pass_viewport=(*cursor)->viewport;
              group.targets=(*cursor)->targets;
            }
            static uint64_t reads=0;
            if(++reads<=4 || reads%100000==0)
              REXLOG_INFO("Native inherited pass inputs: reads={} (no live render-state or sampler reads)",reads);
          } else if(cursor) {
            group.material_pass=edf::native::ReadNativeMaterialRenderPass(reader,device);
            for(uint32_t slot=0;slot<16;++slot)
              group.sampler_pass[slot]=edf::native::ReadNativeMaterialSamplerPass(reader,device,slot);
          } else {
          group.material_pass=edf::native::ReadNativeMaterialRenderPass(reader,device);
          std::array<bool,16> read{};
          for(const auto& operation:group.published_material->program->sampler_operations) {
            if(operation.slot>=read.size()) throw std::runtime_error("invalid published sampler slot");
            if(!read[operation.slot]) {
              group.sampler_pass[operation.slot]=edf::native::ReadNativeMaterialSamplerPass(reader,device,operation.slot);
              read[operation.slot]=true;
            }
          }
          }
          } catch(const std::exception& error) {
            static std::set<std::string> errors;
            if(errors.size()<32 && errors.insert(error.what()).second)
              REXLOG_ERROR("Native scene pass input construction failed: {}",error.what());
            group.published_material.reset();
            group.material_pass.reset();
          }
        }
      }
      if(edf::native::native_material_pass_cursor) edf::native::native_material_pass_cursor->reset();
      if(REXCVAR_GET(edf_native_scene_material_deferred) && group.geometry_handoff.pending() &&
         group.published_material && group.material_pass &&
         group.published_material->program->CanDeferCpuActivation() &&
         edf::native::ObservePublishedActivation(group.activation_instance)) {
        group.material_handoff.Defer();
        return;
      }
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::MaterialActivation);
      work.lr=0x821D979C; sub_821B94E8(work,base);
      if(!group.published_activation_used) finish_geometry();
    };
    const auto draw=[&](uint32_t instance) {
      if(edf::native::TryAppendNativeQueuedSceneInstance(base,device,instance,group)) return;
      edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group);
      if(edf::native::TryAppendPublishedNativeSceneInstance(base,device,address,instance,index_count,group)) {
        if(group.geometry_handoff.pending()) group.geometry_handoff.DrawAccepted();
        else {
          work.r3.u64=device; work.r4.u64=4; work.r5.u64=0; work.r6.u64=0; work.r7.u64=index_count;
          work.lr=0x821D97E8;
          __imp__edf_native_indexed_cpu_tail(work,base);
        }
        return;
      }
      finish_geometry();
      group.all_native_draws=false;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::InstanceSetup);
      work.r3.u64=instance; work.r4.u64=device;
      work.lr=0x821D97D0; sub_821D9600(work,base);
      work.r3.u64=device; work.r4.u64=4; work.r5.u64=0; work.r6.u64=0; work.r7.u64=index_count;
      work.lr=0x821D97E8;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::IndexedDraw);
      sub_821FE358(work,base);
      // A non-world override would contaminate the shared capture for unseen
      // parts. Certify every full-path instance as well as direct instances.
      if(group.population_safe) {
        auto& state=edf::native::State();
        std::lock_guard lock(state.mutex);
        try {
          group.population_safe=group.world_parameter && edf::native::NativeStaticWorldOnly(
            reader,state.scene_sources,instance,group.world_parameter->first,device);
        } catch(const std::exception&) { group.population_safe=false; }
      }
    };
    if(native_queue_size) {
      prepare();
      for(const auto instance:instances) draw(instance);
      reader.StoreWord(reader.Add(address,4),0);
    } else edf::native::VisitNativeQueuedScene(reader,address,prepare,draw);
    edf::native::SynchronizeNativeQueuedSceneInstance(base,device,group);
    finish_geometry();
    if(auto* cursor=edf::native::native_material_pass_cursor;
       cursor && group.published_activation_used && group.all_native_draws && group.material_pass) {
      const edf::native::NativeSceneMaterialPassState incoming{*group.material_pass,group.sampler_pass};
      auto next=incoming.After(*group.published_material->program);
      if(REXCVAR_GET(edf_native_material_state_audit) || REXCVAR_GET(edf_native_material_sampler_audit)) {
        edf::native::NativeSceneMaterialPassState actual;
        actual.render=edf::native::ReadNativeMaterialRenderPass(reader,device);
        for(uint32_t slot=0;slot<16;++slot)
          actual.samplers[slot]=edf::native::ReadNativeMaterialSamplerPass(reader,device,slot);
        actual=actual.Inputs();
        if(next!=actual) {
          REXLOG_ERROR("Native inherited pass mismatch: group={:#x} render_equal={} samplers_equal={}",
            group_address,next.render==actual.render,next.samplers==actual.samplers);
          throw std::runtime_error("native inherited material pass differs from CPU mirrors");
        }
      }
      *cursor=edf::native::NativeScenePassCursorState{device,std::move(next),group.pass_viewport,group.targets};
      static uint64_t count=0;
      if(++count<=4 || count%100000==0)
        REXLOG_INFO("Native inherited material pass: groups={} (owned render state and all 16 sampler slots)",count);
    }
  } else {
    if(edf::native::native_material_pass_cursor) edf::native::native_material_pass_cursor->reset();
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::OriginalGroup);
    __imp__sub_821D96D8(ctx,base);
  }
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  edf::native::FlushNativeQueuedSceneLocked(state,group);
  if(native_queue_size && group.population_safe && group.world_parameter && group.geometry && group.material &&
     !edf::native::BufferWrites().Pending()) {
    try {
      const edf::native::GuestReader reader(base);
      const auto populated=state.scene_adapter.PopulateGroup(state.scene_sources,group_address,group.geometry,
        {group.material,edf::native::kNativeSceneIdentity,group.view},[&](const auto& part) {
          try { return edf::native::NativeStaticWorldOnly(reader,state.scene_sources,part.instance,
                                                        group.world_parameter->first,group_device); }
          catch(const std::exception&) { return false; }
        });
      state.scene_asset_examined+=populated.examined;
      state.scene_asset_created+=populated.created;
      state.scene_asset_rejected+=populated.rejected;
    } catch(const std::exception& error) {
      if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
        REXLOG_INFO("Native scene group population deferred: {}",error.what());
    }
  }
  if(group.geometry && group.material) state.scene_adapter.RememberGroupMaterial(group_address,group.material);
  group.execution.Complete();
  if(native_queue_size) {
    const auto previous=state.scene_queue_instances;
    state.scene_queue_instances+=native_queue_size; ++state.scene_queue_groups;
    if(previous/1000000!=state.scene_queue_instances/1000000) {
      REXLOG_INFO("Native scene queues: instances={} groups={} fallback_batches={}",
        state.scene_queue_instances,state.scene_queue_groups,state.scene_queue_fallbacks);
      REXLOG_INFO("Native scene retained materials: captures={} reused={}",state.scene_group_material_captures,state.scene_group_material_reused);
      REXLOG_INFO("Native scene group assets: examined={} created_before_draw={} rejected={}",
        state.scene_asset_examined,state.scene_asset_created,state.scene_asset_rejected);
      REXLOG_INFO("Native scene publications: tick={} published_selections={} current_selections={}",
        state.scene_publication_tick,state.scene_published_selections,state.scene_current_selections);
    }
  }
}
REX_EXTERN(__imp__sub_821B8E48);
REX_EXTERN(sub_821498C8);
REX_EXTERN(sub_82149608);
REX_EXTERN(sub_82149248);
REX_EXTERN(sub_82149358);
REX_EXTERN(sub_8213BA98);
namespace {
template<class Reader> void BindNativeShaderResource(PPCContext&,uint8_t*,bool,const Reader&);
template<class Reader> void BindNativeTextureResource(PPCContext&,uint8_t*,const Reader&);
bool NativeTextureBindingOwned();
void PublishNativeShaderBinding(uint32_t,uint32_t,bool);
// The guest device block the activation reads and writes: sampler records at
// +1024, constant registers from +1792, render-state mirrors to +12188, the
// texture and shader bindings at +12272..+12423; the extent the static world
// handoff's device overlay covers (NativeStaticWorldDeviceOverlay::kBytes).
constexpr uint32_t kNativeActivationDeviceBytes=13520;
// states=false runs the activation without its state operations (the static
// world handoff writes those combined): shader binds with defaults, constant
// uploads and texture binds with their retirements and sampler words.
//
// Its guest accesses go through two windows proven once per activation - the
// device block (read and written: dirty halves, constant registers, sampler
// and state mirrors, bindings) and the material's 112-byte header - rather
// than a separately validated access each: a HUD Utility activation made a
// few hundred of those (each state override alone re-read the render pass,
// twenty-odd words, and published its fields through a fresh reader), and the
// HUD phase loop activates ~130 times a frame. The windows are live, so every
// value is the one the per-access reads saw; payloads, records and the stack
// keep their own validated accesses, and so does everything when the device
// window cannot be proven. The operation lists go into one program per thread
// (a nested activation, should a guest call ever make one, uses its own).
void ActivateNativeMaterial(PPCContext& ctx,uint8_t* base,uint32_t instance,uint32_t device,bool states=true) {
  const edf::native::GuestReader backing(base);
  const edf::native::GuestWritableWindow reader(backing,device,kNativeActivationDeviceBytes);
  static thread_local edf::native::NativeMaterialCpuProgram reused;
  static thread_local bool reused_busy=false;
  std::optional<edf::native::NativeMaterialCpuProgram> nested;
  auto& program=reused_busy?nested.emplace():reused;
  struct Busy {
    bool& flag; bool owner;
    explicit Busy(bool& flag):flag(flag),owner(!flag) { flag=true; }
    ~Busy() { if(owner) flag=false; }
  } busy(reused_busy);
  edf::native::ReadNativeMaterialCpuProgram(edf::native::GuestReadWindow(reader,instance,112),instance,device,program);
  auto work=ctx;
  if(work.r1.u32<144) throw std::runtime_error("native activation stack");
  work.r1.u64-=144;
  reader.StoreWord(work.r1.u32,ctx.r1.u32);
  edf::native::ExecuteNativeMaterialCpuProgram(program,[&](uint32_t shader,bool pixel) {
    work.r3.u64=device; work.r4.u64=shader; work.lr=pixel?0x821B8E84:0x821B8E70;
    BindNativeShaderResource(work,base,pixel,reader);
    PublishNativeShaderBinding(device,shader,pixel);
  },[&](const auto& operation) {
    const auto first=operation.first/4,last=(operation.first+operation.count-1)/4;
    const uint64_t mask=(~uint64_t(0)>>(first))&(~uint64_t(0)<<(63-last));
    work.r3.u64=device; work.r4.u64=operation.first; work.r5.u64=operation.data;
    work.r6.u64=operation.count; work.r7.u64=mask;
    work.lr=operation.pixel?0x821B8FBC:0x821B8ED8;
    const auto destination=reader.Add(device,(operation.first+(operation.pixel?368:112))*16);
    const size_t bytes=size_t(operation.count)*16;
    const bool disjoint=uint64_t(operation.data)+bytes<=destination || uint64_t(destination)+bytes<=operation.data;
    if(!(destination&15) && disjoint) {
      // The audited upload setters only copy these raw float4 bytes and OR
      // the supplied stage mask. Preserve their stack scratch stores as well.
      reader.StoreWord(work.r1.u32-32,device);
      reader.StoreWord(reader.Add(work.r1.u32,48),uint32_t(mask>>32));
      reader.StoreWord(reader.Add(work.r1.u32,52),uint32_t(mask));
      // The destination and the incoming dirty mask are only the audit
      // oracle's: the upload validates the destination and reads the mask
      // itself, so an unaudited activation does not read them twice.
      const bool audit=REXCVAR_GET(edf_native_scene_material_audit);
      uint8_t* target=nullptr;
      uint32_t dirty_address=0;
      uint64_t dirty=0;
      if(audit) {
        target=const_cast<uint8_t*>(reader.WritableBytes(destination,bytes,4));
        dirty_address=reader.Add(device,operation.pixel?8:0);
        dirty=(uint64_t(reader.Word(dirty_address))<<32)|reader.Word(reader.Add(dirty_address,4));
      }
      if(!edf::native::UploadNativeMaterialConstant(reader,device,operation,mask))
        throw std::runtime_error("native constant eligibility changed during activation");
      if(audit) {
        static std::mutex audit_mutex;
        static std::set<std::tuple<bool,uint32_t,uint32_t,uint32_t>> shapes;
        bool sample=false;
        {
          std::lock_guard lock(audit_mutex);
          if(shapes.size()<4096) sample=shapes.emplace(operation.pixel,operation.first,operation.count,operation.data&15).second;
        }
        if(sample) {
          const std::vector<uint8_t> expected(target,target+bytes);
          // Re-run the original with the same incoming flags. The disjoint
          // source is unchanged, so this is an independent byte-copy oracle.
          reader.StoreWord(dirty_address,uint32_t(dirty>>32));
          reader.StoreWord(reader.Add(dirty_address,4),uint32_t(dirty));
          edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::AuditOracle);
          if(operation.pixel) sub_82149358(work,base); else sub_82149248(work,base);
          const auto actual=(uint64_t(reader.Word(dirty_address))<<32)|reader.Word(reader.Add(dirty_address,4));
          if(std::memcmp(target,expected.data(),bytes) || actual!=(dirty|mask))
            throw std::runtime_error("native activation CPU constant upload differs from original");
          REXLOG_INFO("Native activation constant audit: pixel={} first={} count={} source_alignment={} bytes_and_dirty_match=true",
            operation.pixel,operation.first,operation.count,operation.data&15);
        }
      }
    } else {
      // Preserve the original vector load/store ordering for aliased uploads.
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::AliasedConstant);
      if(operation.pixel) sub_82149358(work,base); else sub_82149248(work,base);
    }
  },[&](const auto& operation) {
    const uint64_t mask=uint64_t(1)<<(43-operation.slot);
    work.r3.u64=device; work.r4.u64=operation.slot; work.r5.u64=operation.handle;
    work.r6.u64=mask; work.lr=0x821B9090;
    // 8213BA98's hook, through the device window when its native branch owns
    // the bind; the hook itself (and so the guest setter) otherwise.
    if(NativeTextureBindingOwned()) BindNativeTextureResource(work,base,reader);
    else sub_8213BA98(work,base);
    const auto inherited=edf::native::ReadNativeMaterialSamplerPass(reader,device,operation.slot);
    const auto resolved=edf::native::ApplyNativeMaterialSampler(inherited,operation.sampler);
    reader.StoreWord(reader.Add(device,1036+operation.slot*24),resolved.words[1]);
    reader.StoreWord(reader.Add(device,1040+operation.slot*24),resolved.words[2]);
    const auto dirty=(uint64_t(reader.Word(reader.Add(device,16)))<<32)|reader.Word(reader.Add(device,20));
    reader.StoreWord(reader.Add(device,16),uint32_t((dirty|mask)>>32));
    reader.StoreWord(reader.Add(device,20),uint32_t(dirty|mask));
  },[&](const auto& operation) {
    if(!states) return;
    work.r3.u64=device; work.r4.u64=operation.value; work.ctr.u64=operation.setter; work.lr=0x821B920C;
    const auto dirty=[&](uint32_t offset) {
      return (uint64_t(reader.Word(reader.Add(device,offset)))<<32)|reader.Word(reader.Add(device,offset+4));
    };
    const auto writes=edf::native::NativeMaterialStateCpuWrites(
      edf::native::ReadNativeMaterialRenderPass(reader,device),operation.offset,operation.value,dirty(16),dirty(24));
    if(!writes) {
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::StateCallback);
      rex::runtime::ResolveIndirectFunction(operation.setter)(work,base); return;
    }
    bool sample=false;
    if(REXCVAR_GET(edf_native_material_state_audit)) {
      static std::mutex audit_mutex;
      static std::set<std::pair<uint32_t,uint32_t>> cases;
      std::lock_guard lock(audit_mutex);
      if(cases.size()<4096) sample=cases.emplace(operation.offset,operation.value).second;
    }
    std::vector<uint32_t> previous;
    if(sample) for(const auto& [offset,value]:*writes) previous.push_back(reader.Word(reader.Add(device,offset)));
    for(const auto& [offset,value]:*writes) reader.StoreWord(reader.Add(device,offset),value);
    if(sample) {
      const auto expected=edf::native::ReadNativeMaterialRenderPass(reader,device);
      for(size_t i=0;i<writes->size();++i) reader.StoreWord(reader.Add(device,(*writes)[i].first),previous[i]);
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::AuditOracle);
      rex::runtime::ResolveIndirectFunction(operation.setter)(work,base);
      for(const auto& [offset,value]:*writes) if(reader.Word(reader.Add(device,offset))!=value)
        throw std::runtime_error("native material CPU state word differs from original at "+std::to_string(offset));
      if(edf::native::ReadNativeMaterialRenderPass(reader,device)!=expected)
        throw std::runtime_error("native material CPU state differs from original");
      REXLOG_INFO("Native activation state oracle: offset={:#x} value={:#x} words_and_dirty_match=true",operation.offset,operation.value);
    }
    // Replacing the setter also replaces its ownership-publication hook.
    // Draws consume these snapshots even when the CPU words already match.
    if(operation.offset==0x44) {
      auto& state=edf::native::State();
      std::lock_guard lock(state.mutex);
      state.render_state_snapshots.PublishBlend(device,
        edf::native::ReadGuestWords<4>(reader,reader.Add(device,10336)),operation.setter);
    } else if(operation.offset!=0x64) {
      edf::native::PublishNativeRenderStateWith(device,operation.setter,[&]() -> const auto& { return reader; });
    }
  });
  static std::atomic<uint64_t> activations=0;
  const auto count=++activations;
  if(count<=4 || count%100000==0)
    REXLOG_INFO("Native material activation: count={} (native traversal/shaders/constants/samplers/state; aliased uploads, texture retirement and scissor callbacks retained)",count);
}
void RestoreNativeStaticMaterial(PPCContext& ctx,uint8_t* base,uint32_t instance,uint32_t device) {
  // Called only while the group's activation is pending. The activation hook
  // would skip ObserveActivation in this state; retain its audit path when asked.
  static std::atomic<uint64_t> native_count=0,fallback_count=0;
  if(EDF_NATIVE_FLAG(material_activation) && !REXCVAR_GET(edf_native_material_state_audit) &&
     !REXCVAR_GET(edf_native_material_sampler_audit)) {
    ActivateNativeMaterial(ctx,base,instance,device);
    ++native_count;
  } else {
    ++fallback_count;
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::MaterialActivation);
    ctx.r3.u64=instance; ctx.lr=0x821D979C; sub_821B94E8(ctx,base);
  }
  const auto count=native_count.load()+fallback_count.load();
  if(count<=4 || count%100000==0)
    REXLOG_INFO("Native static material handoff: native={} compatibility={}",native_count.load(),fallback_count.load());
}
}
REX_HOOK_RAW(sub_821B8E48) {
  const auto instance = ctx.r3.u32, device = ctx.r4.u32;
  // Audit state only when audited: the library's std::map allocates its head
  // node when constructed, and this hook runs for every activation.
  const bool sampler_audit=REXCVAR_GET(edf_native_material_sampler_audit);
  std::optional<std::map<uint32_t,edf::native::NativeMaterialSamplerPass>> audited_samplers;
  if(sampler_audit) audited_samplers.emplace();
  std::optional<edf::native::NativeMaterialRenderPass> expected_state;
  if(REXCVAR_GET(edf_native_material_state_audit)) {
    const edf::native::GuestReader reader(base);
    expected_state=edf::native::ReadNativeMaterialRenderPass(reader,device);
    const auto states=reader.Word(reader.Add(instance,96)),count=reader.Word(reader.Add(instance,104));
    if(count>4096) throw std::runtime_error("invalid native material state audit count");
    for(uint32_t i=0;i<count;++i) {
      const auto operation=edf::native::ReadGuestWords<2>(reader,reader.Add(states,i*8));
      try {
        const auto setter=edf::native::NativeMaterialStateSetter(operation[0]);
        if(reader.Word(reader.Add(device,56+operation[0]))!=setter)
          throw std::runtime_error("native material state setter identity changed");
        edf::native::ApplyNativeMaterialState(*expected_state,operation[0],operation[1]);
      } catch(const std::exception& error) {
        REXLOG_ERROR("Native material state audit: material={:#x} offset={:#x} value={:#x}: {}",
          instance,operation[0],operation[1],error.what());
        throw;
      }
    }
  }
  if(sampler_audit) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    const edf::native::GuestReader reader(base);
    const auto schema=state.material_parameters.Get(instance);
    const auto operations=edf::native::ReadNativeMaterialSamplerOperations(reader,*schema);
    for(const auto& operation:operations) {
      auto found=audited_samplers->find(operation.slot);
      if(found==audited_samplers->end()) found=audited_samplers->emplace(operation.slot,
        edf::native::ReadNativeMaterialSamplerPass(reader,device,operation.slot)).first;
      found->second=edf::native::ApplyNativeMaterialSampler(found->second,operation);
    }
  }
  {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationGuest);
    if(EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
       (!edf::native::native_queued_scene_group || !edf::native::native_queued_scene_group->material_compatibility_only))
      ActivateNativeMaterial(ctx,base,instance,device);
    else {
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::MaterialActivation);
      __imp__sub_821B8E48(ctx, base);
    }
  }
  if(expected_state) {
    const auto actual=edf::native::ReadNativeMaterialRenderPass(edf::native::GuestReader(base),device);
    if(actual!=*expected_state) {
      REXLOG_ERROR("Native material state mismatch: material={:#x} expected={:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x} actual={:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}",
        instance,expected_state->words[0],expected_state->words[1],expected_state->words[2],expected_state->words[3],expected_state->words[4],expected_state->words[5],
        actual.words[0],actual.words[1],actual.words[2],actual.words[3],actual.words[4],actual.words[5]);
      throw std::runtime_error("native material render state differs from guest activation");
    }
    static std::atomic<uint64_t> checked=0;
    const auto count=++checked;
    if(count<=4 || count%100000==0) REXLOG_INFO("Native material state audit: checked={} mismatches=0",count);
  }
  if(sampler_audit) {
    const edf::native::GuestReader reader(base);
    static std::atomic<uint64_t> checked=0;
    for(const auto& [slot,expected]:*audited_samplers) {
      const auto actual=edf::native::ReadSamplerWords(reader,device,slot);
      if(edf::native::SamplerStateKey(actual)!=edf::native::SamplerStateKey(expected.words) ||
         (actual[2]&3)!=(expected.words[2]&3)) {
        REXLOG_ERROR("Native material sampler mismatch: material={:#x} slot={} expected={:#x}/{:#x}/{:#x}/{:#x} actual={:#x}/{:#x}/{:#x}/{:#x}",
          instance,slot,expected.words[0],expected.words[1],expected.words[2],expected.words[3],actual[0],actual[1],actual[2],actual[3]);
        throw std::runtime_error("native material sampler program differs from guest activation");
      }
      const auto count=++checked;
      if(count<=4 || count%100000==0)
        REXLOG_INFO("Native material sampler audit: checked={} mismatches=0",count);
    }
  }
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    edf::native::HookTiming timing(edf::native::HookPhase::ActivationNative);
    if(const auto* group=edf::native::native_queued_scene_group;
       group && group->material_handoff.activation_pending() && group->activation_instance==instance) return;
    try {
      if(!REXCVAR_GET(edf_native_scene_activation_owned) || !EDF_NATIVE_FLAG(scene_material_owned) ||
         REXCVAR_GET(edf_native_scene_material_audit) || !edf::native::ObservePublishedActivation(instance))
        edf::native::ObserveActivation(edf::native::GuestReader(base), instance, device);
    }
    catch (const std::exception& error) { REXLOG_ERROR("Native shader bridge: {}", error.what()); }
  }
}
namespace edf::native {
namespace {
// The instanced mesh loop at 821D97C4 uploads these overrides after material
// activation, immediately before each indexed draw. Preserve that ordering.
void ApplyNativeInstanceParametersLocked(Bridge& state,const GuestReader& reader,uint32_t list,uint32_t device) {
    if(!state.active_vertex || !state.active_vertex_parameters || state.shader_bindings.Vertex(device)!=state.active_vertex) return;
    auto& shader=state.shaders.at(state.active_vertex);
    // A single instanced shader variant can only serve a run whose instances
    // all patch the same register range. Fingerprint the shape (not the data)
    // so the indexed hook can tell whether a collapsible run is also uniform.
    edf::native::HookTiming read_timing(edf::native::HookPhase::InstanceRead);
    static thread_local std::vector<edf::native::InstanceParameter> parameters;
    edf::native::ReadInstanceParameters(reader,list,parameters);
    read_timing.Finish();
    edf::native::HookTiming patch_timing(edf::native::HookPhase::InstancePatch);
    if(REXCVAR_GET(edf_native_batch_audit)) {
      uint64_t shape=1469598103934665603ull;
      for(const auto& entry:parameters) for(const auto word:{entry.first,entry.count}) {
        shape^=word; shape*=1099511628211ull;
      }
      state.instance_shape=shape;
    }
    for(const auto& [data,first,count]:parameters) {
      for(const auto& parameter:*state.active_vertex_parameters) {
        const auto low=(std::max)(first,parameter.first),high=(std::min)(first+count,parameter.first+parameter.count);
        if(low>=high) continue;
        const size_t bytes=size_t(high-low)*16;
        const auto* source=reader.Bytes(reader.Add(data,(low-first)*16),bytes);
        if(const auto frames=REXCVAR_GET(edf_native_instance_motion_trace); frames>0) {
          // Diagnostic only. Addresses identify observations, not certified
          // object lifetimes; do not use this key for interpolation yet.
          struct Entry {
            uint32_t list=0,data=0,first=0,count=0;
            std::string name;
            uint64_t last_frame=0,hash=0,samples=0,changes=0,same_frame_changes=0;
          };
          static uint64_t start_frame=0;
          static bool finished=false;
          static std::map<std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>,Entry> entries;
          if(!finished) {
            if(!start_frame) start_frame=state.scene_frames;
            if(state.scene_frames-start_frame>=uint64_t(frames)) {
              for(const auto& [key,entry]:entries)
                REXLOG_INFO("Native instance motion: list={:#x} data={:#x} first={} count={} name={} samples={} changes={} same_frame_changes={}",
                  entry.list,entry.data,entry.first,entry.count,entry.name,entry.samples,entry.changes,entry.same_frame_changes);
              REXLOG_INFO("Native instance motion: complete frames={} sources={}",frames,entries.size());
              finished=true; entries.clear();
            } else {
              const auto key=std::make_tuple(list,data,low,high-low);
              auto found=entries.find(key);
              if(found==entries.end() && entries.size()<256) {
                found=entries.emplace(key,Entry{list,data,low,high-low,parameter.name}).first;
                const auto header=edf::native::ReadGuestWords<3>(reader,list);
                REXLOG_INFO("Native instance identity: list={:#x} data={:#x} first={} count={} name={} header={:#x},{:#x},{:#x}",
                  list,data,low,high-low,parameter.name,header[0],header[1],header[2]);
              }
              if(found!=entries.end()) {
                auto& entry=found->second;
                uint64_t hash=14695981039346656037ull;
                for(size_t i=0;i<bytes;++i) { hash^=source[i]; hash*=1099511628211ull; }
                if(!entry.samples || entry.last_frame!=state.scene_frames) {
                  if(entry.samples && entry.hash!=hash) ++entry.changes;
                  ++entry.samples; entry.last_frame=state.scene_frames;
                } else if(entry.hash!=hash) ++entry.same_frame_changes;
                entry.hash=hash;
              }
            }
          }
        }
        shader.bindings->PatchGuestFloatRegisters(parameter.normal,low-parameter.first,{source,bytes});
        // A mirroring reversed variant picks these up at the draw that uses it.
        if(!shader.reversed_mirrors)
          shader.reversed_bindings->PatchGuestFloatRegisters(parameter.reversed,low-parameter.first,{source,bytes});
        ++state.instance_parameter_updates;
        if(state.instance_parameter_updates<=12)
          REXLOG_INFO("Native instance constant: name={}, register={}, count={}",parameter.name,low,high-low);
      }
    }
}
void SynchronizeNativeQueuedSceneInstanceLocked(Bridge& state,const GuestReader& reader,uint32_t device,NativeQueuedSceneGroup& group) {
  if(!group.pending_world) return;
  const auto& parameter=*group.world_parameter;
  auto& shader=state.shaders.at(group.vertex);
  const auto& bytes=*group.pending_world;
  // There have been no guest callbacks since the last consumed draw. All
  // skipped draws only overwrite this complete matrix, so only the final value
  // is observable by the next guest callback. Keep the source bytes owned.
  RestoreNativeSceneWorld(reader,device,parameter.first,bytes,[&](const auto& restored) {
    shader.bindings->PatchGuestFloatRegisters(parameter.normal,0,restored);
    if(!shader.reversed_mirrors) shader.reversed_bindings->PatchGuestFloatRegisters(parameter.reversed,0,restored);
  });
  group.pending_world.reset();
}
void SynchronizeNativeQueuedSceneInstance(uint8_t* base,uint32_t device,NativeQueuedSceneGroup& group) {
  if(group.material_handoff.activation_pending()) return;
  if(!group.pending_world) return;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  SynchronizeNativeQueuedSceneInstanceLocked(state,GuestReader(base),device,group);
}
struct NativeStaticPassView {
  GuestViewportWords viewport; ActiveTargets targets;
  bool operator==(const NativeStaticPassView&) const=default;
};
// Publication inputs of one static group. deferred_geometry is the identity a
// pending geometry handoff will bind instead of the draw's live bindings.
struct NativeStaticGroupInputs {
  uint32_t address=0,count=0;
  const std::shared_ptr<const NativeSceneGroupMaterial>& material;
  const std::optional<NativeSceneGeometrySource>& deferred_geometry;
};
// Pass constants, render-state and sampler pass, and the owned pass view. With
// no view the draw's live view (NativeStaticDrawBindings::view) is used.
struct NativeStaticPassInputs {
  uint32_t device=0;
  const std::vector<NativeSceneMaterialInputs::Constant>& constants;
  const NativeMaterialRenderPass& render;
  const std::array<NativeMaterialSamplerPass,16>& samplers;
  std::optional<NativeStaticPassView> view;
};
// What a guest draw reaching this instance has bound. Only the guest-draw path
// supplies these; a native pass has no bound state. Each runs at the point the
// resolve reaches it, so reads, audits and throws keep their order.
struct NativeStaticDrawBindings {
  std::function<bool(const NativeSceneGeometrySource&)> geometry;
  std::function<bool(uint32_t vertex,uint32_t pixel)> program;
  std::function<NativeStaticPassView()> view;
  std::function<void(const NativeStaticPassView&)> audit_view;
};
struct NativeStaticInstanceResolution {
  NativeStaticWorldDecline decline=NativeStaticWorldDecline::None;
  std::shared_ptr<const NativeSceneInstance> object;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::shared_ptr<const NativeSceneMaterial> material;
  uint32_t vertex=0,pixel=0;
  NativeSceneView view;
  NativeStaticPassView pass{};
  bool reverse_depth=false;
  std::optional<VertexParameterRange> world_parameter;
  bool world_column_major=false;
  std::array<uint8_t,64> world{};
  explicit operator bool() const { return decline==NativeStaticWorldDecline::None; }
};
// A group's material under one pass. program.Resolve reads the group's program,
// its retained geometry's layout, the pass view and the pass constants, render
// state and samplers - never the instance - and the capture zeroes g_mWorld out
// of the material image. So every instance of a group shares one interned
// material and differs only in the world applied to its copy of the capture,
// which is what lets the renderer instance them (it batches by material identity).
// Identified by the program, not the published group material: the preload
// republishes that object whenever a constant moves, and the resolve reads it
// only as program and constants.
struct NativeStaticGroupMaterial {
  const NativeSceneMaterialProgram* program=nullptr;
  const NativeIndexedMesh::RetainedDraw* geometry=nullptr;
  NativeStaticPassView pass{};
  NativeSceneResolvedMaterial resolved;
  std::optional<VertexParameterRange> world_parameter;
  bool world_column_major=false;
};
// The per-group half of a published static resolve: what every instance of the
// group shares under one pass. material points into the caller's slot.
struct NativeStaticGroupResolution {
  NativeStaticWorldDecline decline=NativeStaticWorldDecline::None;
  const NativeSceneSources* sources=nullptr;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  const NativeStaticGroupMaterial* material=nullptr;
  NativeStaticPassView pass{};
  NativeSceneView view;
  bool reverse_depth=false;
  explicit operator bool() const { return decline==NativeStaticWorldDecline::None; }
};
// The per-instance half: its publication object and its g_mWorld registers.
struct NativeStaticInstanceWorld {
  NativeStaticWorldDecline decline=NativeStaticWorldDecline::None;
  std::shared_ptr<const NativeSceneInstance> object;
  std::array<uint8_t,64> world{};
  explicit operator bool() const { return decline==NativeStaticWorldDecline::None; }
};
// Resolve one published static group from publication and pass inputs only,
// once for all of instances (each must still be a member of the group).
// Never reads the live bound shaders, streams, index bindings or view; those
// checks belong to the caller, through draw. Exceptions propagate. slot
// carries the group's material between resolves against one
// NativeStaticPassInputs (or constants the caller proved resolve to the same
// material: NativeStaticWorldGroupCache::Current); a different program,
// geometry or view resolves afresh (under the QueuedResolve timing when timed).
NativeStaticGroupResolution ResolveNativePublishedStaticGroupLocked(Bridge& state,const GuestReader& reader,
    const NativeScenePublication& publication,const NativeStaticGroupInputs& group,std::span<const uint32_t> instances,
    const NativeStaticPassInputs& pass,const NativeStaticDrawBindings* draw,std::optional<NativeStaticGroupMaterial>& slot,bool timed) {
  using D=NativeStaticWorldDecline;
  NativeStaticGroupResolution result;
  const auto decline=[&](D reason) { result.decline=reason; return result; };
  if(BufferWrites().Pending()) return decline(D::PendingWrites);
  const auto& sources=NativeSceneSourcesForPass(state);
  result.sources=&sources;
  const auto* membership=sources.FindGroup(group.address);
  const auto cached=state.scene_geometry_loads.find(group.address);
  if(!membership || !std::ranges::all_of(instances,[&](uint32_t instance) { return membership->parts.contains(instance); }) ||
     cached==state.scene_geometry_loads.end() ||
     cached->second.revision!=membership->revision || group.material->revision!=membership->revision) return decline(D::Revision);
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  for(const auto& published:publication.group_geometry)
    if(published->group==group.address && published->revision==membership->revision) { geometry=published->geometry; break; }
  const auto latest=state.scene_adapter.GroupGeometry(group.address,membership->revision);
  if(!geometry || !latest || latest->geometry!=geometry || geometry->backend()!=state.scene_backend.get()) return decline(D::GeometryPublication);
  const auto& source_geometry=cached->second.source;
  if(group.count!=source_geometry.count) return decline(D::GeometryCount);
  if(group.deferred_geometry) {
    if(*group.deferred_geometry!=source_geometry) return decline(D::DeferredGeometry);
  } else if(draw && draw->geometry && !draw->geometry(source_geometry)) return decline(D::GeometryBinding);
  const auto* vb=state.model_buffers.Find(source_geometry.vertex,NativeModelBuffers::Kind::Vertex);
  const auto* ib=state.model_buffers.Find(source_geometry.index,NativeModelBuffers::Kind::Index);
  if(!vb || !ib || !vb->physical || !ib->physical || vb->generation!=cached->second.vertex_generation ||
     ib->generation!=cached->second.index_generation) return decline(D::BufferGeneration);
  const auto index_contents=ib->index_contents?ib->index_contents:(ib->index_storage?ib->index_storage->SourceSnapshot():nullptr);
  using Identity=NativeBufferWrites::SnapshotIdentityView;
  NativeBufferWrites::SnapshotPolicy policy{};
  policy.audit_revisions=REXCVAR_GET(edf_native_retirement_audit);
  if(!policy.audit_revisions && state.mesh_watch_audit.expired()) {
    policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
    policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
  }
  const auto versions=BufferWrites().TryValidateObservedSet(std::array<Identity,2>{{
    {source_geometry.vertex,*vb->physical,vb->bytes,&vb->vertex_contents},
    {source_geometry.index,*ib->physical,ib->bytes,&index_contents}}},policy);
  if(versions) {
    for(size_t i=0;i<2;++i) if((*versions)[i].lifetime!=cached->second.versions[i].lifetime ||
      (*versions)[i].revision!=cached->second.versions[i].revision) return decline(D::VersionChanged);
  } else {
    // A due sampled comparison runs here, bounded to this group's VB and IB,
    // rather than turning the group back to the guest path until the next
    // preload tick. It consumes the due observation, so the preload then finds
    // the group unchanged; anything but an exact match leaves the re-load to it.
    using C=NativeStaticComparison;
    using Source=NativeBufferWrites::SnapshotSource;
    const auto compared=CompareNativeStaticGeometry(BufferWrites(),std::array<Source,2>{{
      {source_geometry.vertex,*vb->physical,{reader.Bytes(vb->address,vb->bytes),vb->bytes},vb->vertex_contents},
      {source_geometry.index,*ib->physical,{reader.Bytes(ib->address,ib->bytes),ib->bytes},index_contents}}},
      cached->second.versions,policy);
    if(compared==C::UnreportedChange)
      REXLOG_WARN("Native published geometry draw detected an unreported geometry write: group={:#x}; the preload re-loads it",group.address);
    if(compared==C::Unavailable) return decline(D::VersionsUnavailable);
    if(compared!=C::Confirmed) return decline(D::ComparisonChanged);
    ++state.scene_geometry_draw_verified;
  }
  const auto& program=*group.material->program;
  if(draw && draw->program && !draw->program(program.inputs.vertex,program.inputs.pixel)) return decline(D::ProgramBinding);
  if(pass.view) {
    result.pass=*pass.view;
    if(draw && draw->audit_view) draw->audit_view(result.pass);
    static uint64_t count=0;
    if(++count<=4 || count%100000==0)
      REXLOG_INFO("Native owned pass view: reads={} (viewport/scissor/targets from native pass)",count);
  } else {
    if(!draw || !draw->view) throw std::runtime_error("native static instance has no pass view");
    result.pass=draw->view();
  }
  const auto& targets=result.pass.targets;
  const auto viewport=DecodeDrawViewport(result.pass.viewport);
  if(!targets.count) return decline(D::Targets);
  if(!slot || slot->program!=&program || slot->geometry!=geometry.get() ||
     slot->pass.viewport!=result.pass.viewport || slot->pass.targets!=targets) {
    // Only the world pass shares a slot across resolves; its cache misses land here.
    HookTiming resolve_timing(HookPhase::QueuedResolve,timed);
    NativeBackendPipelineDesc desc;
    desc.vertex_id=(uint64_t(program.inputs.vertex)<<1)|uint64_t(viewport.reverse_depth);
    desc.pixel_id=program.inputs.pixel;
    desc.input_layout=geometry->input_layout().elements(); desc.input_layout_id=geometry->input_layout().fingerprint();
    desc.render_targets=targets.count; desc.rtv_format=targets.rtv_format;
    desc.dsv_format=targets.dsv_format; desc.sample_count=targets.samples;
    auto resolved=program.Resolve(desc,viewport.reverse_depth,pass.constants,pass.render,pass.samplers,
      REXCVAR_GET(edf_native_anisotropic_filtering));
    // Equal materials share one object: the renderer instances only identical
    // material pointers, and the publication then keeps an unchanged instance.
    resolved.capture.material=state.scene_adapter.InternMaterial(std::move(resolved.capture.material));
    NativeQueuedSceneGroup candidate;
    candidate.material=resolved.capture.material;
    candidate.published_material=group.material;
    ConfigureNativeQueuedWorldLocked(state,candidate);
    slot=NativeStaticGroupMaterial{&program,geometry.get(),result.pass,std::move(resolved),
      std::move(candidate.world_parameter),candidate.world_column_major};
  }
  if(!slot->world_parameter) return decline(D::WorldParameter);
  result.material=&*slot;
  // Every instance's view: the shared capture's camera in the pass viewport.
  result.view=NativeStaticInstanceView(slot->resolved.capture.camera,viewport,slot->resolved.render.words[5]!=0);
  result.reverse_depth=viewport.reverse_depth;
  result.geometry=std::move(geometry);
  return result;
}
// One instance of a resolved group: its source, its world and its object. The
// group's world binding is its single vertex g_mWorld, so the decode is
// ApplyNativeScenePublishedWorld's. reuse, when given, lets an instance
// resolved to the same material and world as before keep its object.
NativeStaticInstanceWorld ResolveNativePublishedStaticInstanceWorldLocked(Bridge& state,const GuestReader& reader,
    const NativeScenePublication& publication,const NativeStaticGroupResolution& group,uint32_t instance,uint32_t device,
    NativeSceneInstanceReuse* reuse) {
  using D=NativeStaticWorldDecline;
  NativeStaticInstanceWorld result;
  const auto decline=[&](D reason) { result.decline=reason; return result; };
  const auto& sources=*group.sources;
  const auto& material=*group.material;
  const auto* source=sources.Find(instance);
  if(!source || !NativeStaticWorldOnly(reader,*source,instance,material.world_parameter->first,device)) return decline(D::WorldParameter);
  const auto world=sources.WorldRegisters(*source,source->world_data);
  if(!world) return decline(D::WorldRegisters);
  if(EDF_NATIVE_FLAG(scene_sources_owned) && REXCVAR_GET(edf_native_scene_transform_audit)) {
    ++state.scene_world_checks;
    if(std::memcmp(world->data(),reader.Bytes(source->world_data,64),64)) {
      ++state.scene_world_mismatches;
      REXLOG_ERROR("Native published first world mismatch: owner={:#x} instance={:#x}",source->owner,instance);
      return decline(D::WorldMismatch);
    }
  }
  const auto& capture=material.resolved.capture;
  const auto matrix=DecodeNativeQueuedWorld(*world,material.world_column_major);
  if(EDF_NATIVE_FLAG(scene_selection_owned)) {
    result.object=publication.Resolve(*source,group.geometry,capture.material,matrix,reuse);
    if(!result.object) return decline(D::Lifetime);
  } else {
    const auto id=state.scene_adapter.Observe(*source,group.geometry,NativeSceneMaterialCapture{capture.material,matrix,capture.camera});
    result.object=SelectNativeSceneInstanceLocked(state,id);
  }
  result.world=*world;
  return result;
}
// Both halves for one instance, as the guest draw reaching it resolves it.
NativeStaticInstanceResolution ResolveNativePublishedStaticInstanceLocked(Bridge& state,const GuestReader& reader,
    const NativeScenePublication& publication,const NativeStaticGroupInputs& group,uint32_t instance,
    const NativeStaticPassInputs& pass,const NativeStaticDrawBindings* draw=nullptr) {
  NativeStaticInstanceResolution result;
  std::optional<NativeStaticGroupMaterial> slot;
  const auto resolved=ResolveNativePublishedStaticGroupLocked(state,reader,publication,group,{&instance,1},pass,draw,slot,false);
  if(!resolved) { result.decline=resolved.decline; return result; }
  auto world=ResolveNativePublishedStaticInstanceWorldLocked(state,reader,publication,resolved,instance,pass.device,nullptr);
  if(!world) { result.decline=world.decline; return result; }
  const auto& material=*resolved.material;
  const auto& program=*group.material->program;
  result.object=std::move(world.object); result.geometry=resolved.geometry; result.material=material.resolved.capture.material;
  result.vertex=program.inputs.vertex; result.pixel=program.inputs.pixel;
  result.view=resolved.view; result.pass=resolved.pass; result.reverse_depth=resolved.reverse_depth;
  result.world_parameter=material.world_parameter; result.world_column_major=material.world_column_major;
  result.world=world.world;
  return result;
}
bool TryAppendPublishedNativeSceneInstance(uint8_t* base,uint32_t device,uint32_t address,uint32_t instance,uint32_t count,NativeQueuedSceneGroup& group) {
  if(!EDF_NATIVE_FLAG(scene_geometry_owned) || !EDF_NATIVE_FLAG(scene_material_owned) ||
     REXCVAR_GET(edf_native_scene_material_audit) || group.geometry || !group.objects.empty() ||
     !group.published_material || !group.material_pass || !native_scene_publication) return false;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  const auto defer=[](const char* reason) {
    static std::set<std::string> reported;
    if(reported.insert(reason).second) REXLOG_INFO("Native published geometry draw declined: {}",reason);
    return false;
  };
  if(BufferWrites().Pending()) return defer("pending resource writes");
  try {
    const GuestReader reader(base);
    // The live guest bindings. The resolve calls each only where it reaches
    // that check, as the inline reads did.
    NativeStaticDrawBindings draw;
    draw.geometry=[&](const NativeSceneGeometrySource& source) {
      return NativeBoundGeometryMatches(source,[&]() -> const GuestStream& { return state.streams.at({device,0}); },
        [&] { return state.index_bindings.at(device); },[&] { return state.declaration_bindings.at(device); });
    };
    draw.program=[&](uint32_t vertex,uint32_t pixel) {
      return state.active_vertex==vertex && state.linked_pixel==pixel && state.active_vertex_parameters &&
        (group.material_handoff.activation_pending() || state.shader_bindings.Vertex(device)==vertex);
    };
    draw.view=[&] { const auto viewport=ReadNativeDrawViewportWords(reader,device); return NativeStaticPassView{viewport,ActiveTargetsLocked(state)}; };
    draw.audit_view=[&](const NativeStaticPassView& view) {
      if(REXCVAR_GET(edf_native_render_state_audit) &&
         (view.viewport!=ReadNativeDrawViewportWords(reader,device) || view.targets!=ActiveTargetsLocked(state))) {
        REXLOG_ERROR("Native pass view mismatch: group={:#x}",address);
        throw std::runtime_error("native inherited viewport or targets changed without a pass boundary");
      }
    };
    const bool owned_view=REXCVAR_GET(edf_native_scene_view_owned) &&
      group.material_handoff.activation_pending() && group.pass_viewport.has_value();
    NativeStaticPassInputs pass{device,group.pass_constants,*group.material_pass,group.sampler_pass};
    if(owned_view) pass.view=NativeStaticPassView{*group.pass_viewport,group.targets};
    auto resolved=ResolveNativePublishedStaticInstanceLocked(state,reader,*native_scene_publication,
      {address,count,group.published_material,group.geometry_handoff.pending()},instance,pass,&draw);
    if(!resolved) {
      if(const auto* reason=NativeStaticWorldDeclineReason(resolved.decline)) return defer(reason);
      return false;
    }
    group.objects.push_back(std::move(resolved.object));
    group.geometry=std::move(resolved.geometry); group.material=std::move(resolved.material);
    group.view=resolved.view; group.targets=resolved.pass.targets; group.reverse_depth=resolved.reverse_depth;
    group.pass_viewport=resolved.pass.viewport;
    group.vertex=resolved.vertex; group.pixel=resolved.pixel;
    group.world_parameter=std::move(resolved.world_parameter); group.world_column_major=resolved.world_column_major;
    // The accepted instance only replaces g_mWorld. Preserve its final CPU
    // value before the next guest callback, as for subsequent queued instances.
    group.pending_world=resolved.world;
    group.instance=instance;
    group.constants_clean=group.geometry_handoff.pending().has_value();
    if(++state.scene_geometry_draw_bypasses<=4 || state.scene_geometry_draw_bypasses%10000==0)
      REXLOG_INFO("Native scene published geometry draws: bypassed={} draw_verified={} (no instance callback, indexed mesh acquisition or live binding setup)",
        state.scene_geometry_draw_bypasses,state.scene_geometry_draw_verified);
    return true;
  } catch(const std::exception& error) {
    if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
      REXLOG_INFO("Native published geometry draw deferred: {}",error.what());
    return false;
  }
}
bool TryAppendNativeQueuedSceneInstance(uint8_t* base,uint32_t device,uint32_t instance,NativeQueuedSceneGroup& group) {
  if(!group.geometry || !group.material) return false;
  auto& state=State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  // A pending resource write must pass the full draw's revision/geometry check.
  if(BufferWrites().Pending() || state.active_vertex!=group.vertex || state.linked_pixel!=group.pixel ||
     !state.active_vertex_parameters || (!group.material_handoff.activation_pending() && state.shader_bindings.Vertex(device)!=group.vertex)) return false;
  const GuestReader reader(base);
  std::optional<NativeSceneMaterialCapture> capture;
  std::optional<std::array<uint8_t,64>> world_registers;
  std::shared_ptr<const NativeSceneInstance> published_selection;
  uint64_t id=0;
  try {
    const auto& sources=NativeSceneSourcesForPass(state);
    const auto* source=sources.Find(instance);
    if(!source) return false;
    static thread_local std::vector<InstanceParameter> parameters;
    ReadNativeSceneInstanceParameters(reader,source,instance,parameters);
    const bool world_only=group.world_parameter && parameters.size()==1 && parameters[0].count==4 &&
      parameters[0].first==group.world_parameter->first &&
      !(uint64_t(parameters[0].data)<uint64_t(device)+5888 && uint64_t(parameters[0].data)+64>uint64_t(device)+1792);
    if(world_only) {
      if(parameters[0].data!=source->world_data) group.population_safe=false;
      if(!group.constants_clean) {
        if(!NativeQueuedConstantsClean(reader,device)) return false;
        group.constants_clean=true;
      }
      world_registers.emplace();
      if(const auto published=sources.WorldRegisters(*source,parameters[0].data)) {
        *world_registers=*published; ++state.scene_world_reused;
        if(REXCVAR_GET(edf_native_scene_transform_audit)) {
          ++state.scene_world_checks;
          const auto* actual=reader.Bytes(parameters[0].data,64);
          if(std::memcmp(actual,published->data(),64)) {
            group.population_safe=false;
            ++state.scene_world_mismatches;
            std::memcpy(world_registers->data(),actual,64);
            if(state.scene_world_mismatches<=8)
              REXLOG_ERROR("Native scene transform mismatch: owner={:#x} lod={} part={} data={:#x}",source->owner,source->lod,source->part,parameters[0].data);
          }
        }
      } else {
        std::memcpy(world_registers->data(),reader.Bytes(parameters[0].data,64),64); ++state.scene_world_reads;
      }
      capture=NativeSceneMaterialCapture{group.material,DecodeNativeQueuedWorld(*world_registers,group.world_column_major),group.view};
    } else {
      if(group.geometry_handoff.pending()) return false;
      group.population_safe=false;
      SynchronizeNativeQueuedSceneInstanceLocked(state,reader,device,group);
      if(!NativeQueuedConstantsConsumed(reader,device,parameters)) return false;
      ApplyNativeInstanceParametersLocked(state,reader,instance,device);
      auto& vertex=VertexBindingsForDraw(state.shaders.at(group.vertex),group.reverse_depth);
      capture=CaptureNativeSceneMaterial(state.scene_backend,*group.material->pipeline(),vertex,
        *state.shaders.at(group.pixel).bindings,group.material->blend_factor(),group.material);
    }
    if(EDF_NATIVE_FLAG(scene_selection_owned) && world_only && native_scene_publication) {
      published_selection=native_scene_publication->Resolve(*source,group.geometry,*capture);
      if(!published_selection) return false;
      static uint64_t selections=0;
      if(++selections<=4 || selections%100000==0)
        REXLOG_INFO("Native immutable instance selection: count={} (no current scene database mutation)",selections);
    } else id=state.scene_adapter.Observe(*source,group.geometry,*capture);
  } catch(const std::exception& error) {
    ++state.scene_native_direct_retries;
    if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
      REXLOG_INFO("Native scene direct retry: {}",error.what());
    // CPU constant writes and native patches are idempotent for the nonaliasing
    // sources accepted above. The regular path reapplies every override.
    return false;
  }
  const auto& camera=capture->camera;
  if(group.view.view!=camera.view || group.view.projection!=camera.projection || group.view.view_projection!=camera.view_projection)
    FlushNativeQueuedSceneLocked(state,group);
  group.view.view=camera.view; group.view.projection=camera.projection; group.view.view_projection=camera.view_projection;
  group.objects.push_back(published_selection?std::move(published_selection):SelectNativeSceneInstanceLocked(state,id));
  group.material=group.objects.back()->object.material;
  if(world_registers) { group.pending_world=std::move(world_registers); ++state.scene_native_direct_worlds; }
  if(++state.scene_native_direct_instances%100000==0) {
    REXLOG_INFO("Native scene direct: instances={} retries={} worlds={} objects={} rendered={} draws={}",
      state.scene_native_direct_instances,state.scene_native_direct_retries,state.scene_native_direct_worlds,state.scene_adapter.objects(),
      state.scene_native_objects,state.scene_native_draws);
    REXLOG_INFO("Native scene transforms: updates={} retained={} guest_reads={} checks={} mismatches={}",
      state.scene_world_publications,state.scene_world_reused,state.scene_world_reads,state.scene_world_checks,state.scene_world_mismatches);
  }
  return true;
}
}
}
REX_EXTERN(__imp__sub_821D9600);
REX_HOOK_RAW(sub_821D9600) {
  const auto list=ctx.r3.u32, device=ctx.r4.u32;
  if(edf::native::native_queued_scene_group) edf::native::native_queued_scene_group->instance=list;
  {
    edf::native::HookTiming timing(edf::native::HookPhase::InstanceGuest);
    __imp__sub_821D9600(ctx,base);
  }
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  edf::native::HookTiming timing(edf::native::HookPhase::InstanceNative);
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  if(REXCVAR_GET(edf_native_scene_adapter_audit)) {
    if(state.scene_sources.Find(list)) ++state.scene_source_draws;
    else ++state.scene_source_misses;
    const auto total=state.scene_source_draws+state.scene_source_misses;
    if(total%100000==0)
      REXLOG_INFO("Native scene ownership: owners={} parts={} mapped_draws={} unmapped_draws={}",
        state.scene_sources.owners(),state.scene_sources.parts(),state.scene_source_draws,state.scene_source_misses);
  }
  try {
    edf::native::ApplyNativeInstanceParametersLocked(state,edf::native::GuestReader(base),list,device);
  } catch(const std::exception& error) {
    ++state.instance_parameter_errors;
    state.active_vertex=0; // Never submit a draw with partially applied overrides.
    state.active_vertex_parameters.reset();
    if(state.instance_parameter_errors<=10) REXLOG_ERROR("Native instance constant: {}",error.what());
  }
}
// Preserve retail setter/dirty-mask/return semantics, then publish completed CPU
// state. Diagnostic-only until indirect, reset and concurrent writers are proven.
#define EDF_RENDER_STATE_SETTER(name,pc) \
  REX_EXTERN(__imp__##name); \
  REX_HOOK_RAW(name) { \
    const auto device=ctx.r3.u32; \
    __imp__##name(ctx,base); \
    edf::native::PublishNativeRenderState(base,device,pc); \
  }
EDF_RENDER_STATE_SETTER(sub_82134EE8,0x82134ee8)
EDF_RENDER_STATE_SETTER(sub_82134EB8,0x82134eb8)
EDF_RENDER_STATE_SETTER(sub_82134F18,0x82134f18)
EDF_RENDER_STATE_SETTER(sub_82134F58,0x82134f58)
EDF_RENDER_STATE_SETTER(sub_82134FE8,0x82134fe8)
EDF_RENDER_STATE_SETTER(sub_82135078,0x82135078)
EDF_RENDER_STATE_SETTER(sub_82135108,0x82135108)
EDF_RENDER_STATE_SETTER(sub_82135198,0x82135198)
EDF_RENDER_STATE_SETTER(sub_82135208,0x82135208)
EDF_RENDER_STATE_SETTER(sub_82135278,0x82135278)
EDF_RENDER_STATE_SETTER(sub_821352E8,0x821352e8)
EDF_RENDER_STATE_SETTER(sub_821353E8,0x821353e8)
EDF_RENDER_STATE_SETTER(sub_82135530,0x82135530)
EDF_RENDER_STATE_SETTER(sub_82135578,0x82135578)
EDF_RENDER_STATE_SETTER(sub_821355A8,0x821355a8)
EDF_RENDER_STATE_SETTER(sub_821355E8,0x821355e8)
EDF_RENDER_STATE_SETTER(sub_82135630,0x82135630)
EDF_RENDER_STATE_SETTER(sub_82135670,0x82135670)
EDF_RENDER_STATE_SETTER(sub_821356A0,0x821356a0)
EDF_RENDER_STATE_SETTER(sub_821356E0,0x821356e0)
EDF_RENDER_STATE_SETTER(sub_82135720,0x82135720)
EDF_RENDER_STATE_SETTER(sub_82135750,0x82135750)
EDF_RENDER_STATE_SETTER(sub_82135780,0x82135780)
EDF_RENDER_STATE_SETTER(sub_821357C0,0x821357c0)
EDF_RENDER_STATE_SETTER(sub_82135800,0x82135800)
EDF_RENDER_STATE_SETTER(sub_82135948,0x82135948)
EDF_RENDER_STATE_SETTER(sub_82135A10,0x82135a10)
EDF_RENDER_STATE_SETTER(sub_82135AB8,0x82135ab8)
EDF_RENDER_STATE_SETTER(sub_82135B08,0x82135b08)
EDF_RENDER_STATE_SETTER(sub_82135B40,0x82135b40)
EDF_RENDER_STATE_SETTER(sub_82135B78,0x82135b78)
EDF_RENDER_STATE_SETTER(sub_82135BB0,0x82135bb0)
EDF_RENDER_STATE_SETTER(sub_82136478,0x82136478)
EDF_RENDER_STATE_SETTER(sub_821364C8,0x821364c8)
EDF_RENDER_STATE_SETTER(sub_821364F8,0x821364f8)
EDF_RENDER_STATE_SETTER(sub_82137978,0x82137978)
EDF_RENDER_STATE_SETTER(sub_82137988,0x82137988)
#undef EDF_RENDER_STATE_SETTER
REX_EXTERN(sub_82137410);
REX_EXTERN(sub_821375C0);
// Clear's native flags are low four color-target bits, bit 4 depth, bit 5
// stencil (verified in 821334E8), not desktop D3D9's 1/2/4 flag values.
REX_EXTERN(__imp__sub_821340D0);
REX_HOOK_RAW(sub_821340D0) {
  if (EDF_NATIVE_FLAG(shader_bridge) && (ctx.r6.u32&15)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    try {
      const edf::native::GuestReader reader(base);
      auto word=[&](uint32_t offset) { return reader.Word(reader.Add(ctx.r3.u32,offset)); };
      // 82133D20 reads the four actual color surfaces at device+12168..12180.
      // Match resource identity, not the last high-level Begin scope. Never
      // interpret r7 as an address: 821340D0 unpacks that packed ARGB value.
      for (uint32_t slot=0;slot<4;++slot) {
        if (!(ctx.r6.u32&(1u<<slot))) continue;
        const auto surface=word(12168+slot*4);
        for (auto& [owner,registered]:state.render_targets) {
          if (!surface || registered.surface_handle!=surface) continue;
          auto& target=registered.native;
          // Partial/tiled clears need their own rectangle mapping. Retain no
          // claim of current initialized contents when that write is skipped.
          if (ctx.r5.u32 || word(11584) || word(12376) || word(12380) ||
              word(12384)!=target.sampled.width || word(12388)!=target.sampled.height ||
              (*reader.Bytes(reader.Add(ctx.r3.u32,10808),1)&0x30)) {
            target.content_valid=false;
            if (++state.color_clear_skips<=5)
              REXLOG_INFO("Native color clear skipped: surface={:#x}, rectangles={:#x}, viewport={}x{}, target={}x{}",
                surface,ctx.r5.u32,word(12384),word(12388),target.sampled.width,target.sampled.height);
            continue;
          }
          edf::native::ClearNativeColorTarget(edf::native::SceneRecorderLocked(state),target,ctx.r7.u32);
          if (++state.color_clears<=5)
            REXLOG_INFO("Native color clear: surface={:#x}, slot={}, ARGB={:#x}, initialized=true",surface,slot,ctx.r7.u32);
        }
      }
    } catch (const std::exception& error) {
      REXLOG_ERROR("Native color clear: {}",error.what());
    }
  }
  if (EDF_NATIVE_FLAG(shader_bridge) && (ctx.r6.u32&0x30)) {
    auto& state=edf::native::State();
    std::lock_guard lock(state.mutex);
    try {
      const edf::native::GuestReader reader(base);
      auto word=[&](uint32_t offset) { return reader.Word(reader.Add(ctx.r3.u32,offset)); };
      const auto found=state.depth_targets.find(word(12184));
      if (found!=state.depth_targets.end()) {
        auto& target=found->second;
        // Guest clears intersect viewport/scissor and may carry explicit
        // rectangles. D3D11 ClearDepthStencilView clears the whole resource.
        // Never expand an unimplemented partial clear into a whole-surface one.
        if (ctx.r5.u32 || word(11584) || word(12376) || word(12380) ||
            word(12384)!=target.width || word(12388)!=target.height) {
          ++state.depth_clear_skips;
          if (ctx.r6.u32&0x10) target.depth_valid=false;
          if (ctx.r6.u32&0x20) target.stencil_valid=false;
        } else {
          edf::native::ClearNativeDepthTarget(edf::native::SceneRecorderLocked(state),target,
            (ctx.r6.u32&0x10)!=0,(ctx.r6.u32&0x20)!=0,float(ctx.f1.f64),uint8_t(ctx.r9.u32));
          ++state.depth_clears;
          if (state.depth_clears<=5 || state.depth_clears%1000==0)
            REXLOG_INFO("Native depth clear: cleared={}, skipped={}, flags={:#x}, depth={}, stencil={}",
              state.depth_clears,state.depth_clear_skips,ctx.r6.u32,ctx.f1.f64,ctx.r9.u32);
        }
      }
    } catch (const std::exception& error) {
      if (++state.depth_errors<=10) REXLOG_ERROR("Native depth clear: {}",error.what());
    }
  }
  __imp__sub_821340D0(ctx,base);
}
REX_EXTERN(__imp__sub_82137F98);
REX_HOOK_RAW(sub_82137F98) {
  edf::native::HookTiming timing(edf::native::HookPhase::ColorTarget);
  __imp__sub_82137F98(ctx,base);
}
REX_EXTERN(__imp__sub_82137CB8);
REX_HOOK_RAW(sub_82137CB8) {
  edf::native::HookTiming timing(edf::native::HookPhase::DepthTarget);
  const auto device=ctx.r3.u32;
  __imp__sub_82137CB8(ctx,base);
  edf::native::PublishNativeRenderState(base,device,0x82137cb8);
}
namespace {
// CPU SetStreamSource: retain the original byte offset and full stride. The
// device's fetch descriptor encodes these and cannot serve as a native API.
void AuditNativeRetirement(const edf::native::GuestReader& reader,uint32_t device,bool index,
    edf::native::NativeRetirementPath path,uint32_t previous,uint32_t value,uint32_t cursor) noexcept {
  if(!REXCVAR_GET(edf_native_retirement_audit)) return;
  static std::atomic<uint64_t> counts[2][4]{};
  const auto branch=static_cast<unsigned>(path);
  const auto count=counts[index?1:0][branch].fetch_add(1,std::memory_order_relaxed)+1;
  if(count>8 && (count&(count-1))) return;
  try {
    const auto descriptor=reader.Word(reader.Add(device,13140));
    REXLOG_INFO("Native retirement: index={} branch={} count={} device={:08X} previous={:08X} value={:08X} cursor={:08X} descriptor={:08X}",
      index,branch,count,device,previous,value,cursor,descriptor);
  } catch(const std::exception& error) {
    REXLOG_WARN("Native retirement audit read failed: {}",error.what());
  }
}
}
REX_EXTERN(__imp__sub_82137410);
REX_EXTERN(sub_82141440);
namespace {
void InstallNativeStaticGeometry(PPCContext& ctx,uint8_t* base,uint32_t device,
    const edf::native::NativeSceneGeometrySource& input,edf::native::NativeSceneGeometryInstallState& progress) {
  const edf::native::GuestReader reader(base);
  edf::native::InstallNativeSceneGeometry(reader,device,input,
    [&](bool index) {
      auto work=ctx;
      const uint32_t frame=index?128:144;
      if(work.r1.u32<frame) throw std::runtime_error("invalid static geometry retirement stack");
      work.r1.u64=work.r1.u32-frame; work.r3.u64=device;
      work.lr=index?0x8213761C:0x821374D0;
      // Queue growth is an explicit remaining allocator boundary, not a setter.
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
      sub_82141440(work,base); return work.r3.u32;
    },[&](bool index) {
      const uint32_t offset=index?48:64;
      if(ctx.r1.u32<offset) throw std::runtime_error("invalid static geometry retirement tag");
      return reader.Word(ctx.r1.u32-offset);
    },[&](bool index,auto path,auto previous,auto value,auto cursor) {
      AuditNativeRetirement(reader,device,index,path,previous,value,cursor);
    },[&](edf::native::NativeGeometryBinding binding) {
      auto& state=edf::native::State();
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      switch(binding) {
        case edf::native::NativeGeometryBinding::Stream:
          state.streams.insert_or_assign({device,0},edf::native::GuestStream{input.vertex,0,input.stride}); break;
        case edf::native::NativeGeometryBinding::Declaration:
          state.declaration_bindings.insert_or_assign(device,input.declaration); break;
        case edf::native::NativeGeometryBinding::Index:
          state.index_bindings.insert_or_assign(device,input.index); break;
      }
    },&progress);
}
}
REX_HOOK_RAW(sub_82137410) {
  const auto device=ctx.r3.u32, stream=ctx.r4.u32;
  const edf::native::GuestStream binding{ctx.r5.u32,ctx.r6.u32,ctx.r7.u32};
  if(!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_82137410(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  edf::native::SetNativeStreamResource(reader,device,stream,binding.resource,binding.offset,binding.stride,ctx.r8.u64,
    [&] {
      auto work=ctx;
      if(work.r1.u32<144) throw std::runtime_error("invalid stream setter guest stack");
      work.r1.u64=work.r1.u32-144u; work.r3.u64=device; work.lr=0x821374D0;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
      sub_82141440(work,base);
      return work.r3.u32;
    },
    [&] {
      if(ctx.r1.u32<64) throw std::runtime_error("invalid stream setter tag address");
      return reader.Word(ctx.r1.u32-64u);
    },[&](auto path,uint32_t previous,uint32_t value,uint32_t cursor) {
      AuditNativeRetirement(reader,device,false,path,previous,value,cursor);
    });
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.streams.insert_or_assign({device,stream},binding);
  }
}
// CPU SetIndices: own the binding at its producer, rather than re-reading the
// guest device slot for each native draw. Keep the original old-resource fence
// and deferred-retirement bookkeeping; a null binding is an explicit unbind.
REX_EXTERN(__imp__sub_821375C0);
REX_EXTERN(sub_82141440);
REX_HOOK_RAW(sub_821375C0) {
  const auto device=ctx.r3.u32,resource=ctx.r4.u32;
  if(!EDF_NATIVE_FLAG(shader_bridge)) { __imp__sub_821375C0(ctx,base); return; }
  const edf::native::GuestReader reader(base);
  edf::native::SetNativeIndexResource(reader,device,resource,
    [&] {
      auto work=ctx;
      // Match the original caller frame while invoking the retained queue
      // allocator. In particular, do not overwrite the legacy tag at SP-48.
      if(work.r1.u32<128) throw std::runtime_error("invalid index setter guest stack");
      work.r1.u64=work.r1.u32-128u; work.r3.u64=device; work.lr=0x8213761C;
      edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
      sub_82141440(work,base);
      return work.r3.u32;
    },
    [&] {
      if(ctx.r1.u32<48) throw std::runtime_error("invalid index setter tag address");
      return reader.Word(ctx.r1.u32-48u);
    },[&](auto path,uint32_t previous,uint32_t value,uint32_t cursor) {
      AuditNativeRetirement(reader,device,true,path,previous,value,cursor);
    });
  if(EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.index_bindings.insert_or_assign(device,resource);
  }
}
namespace {
// reader is what the device, shader and stack words go through: the material
// activation passes its device window, the setter hooks a fresh reader.
template<class Reader>
void BindNativeShaderResource(PPCContext& ctx,uint8_t* base,bool pixel,const Reader& reader) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  edf::native::SetNativeShaderResource(reader,device,shader,pixel,[&] {
    auto work=ctx;
    if(work.r1.u32<128) throw std::runtime_error("invalid native shader setter stack");
    work.r1.u64=work.r1.u32-128; work.r3.u64=device;
    work.lr=pixel?0x82149664:0x82149924;
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
    sub_82141440(work,base);
    return work.r3.u32;
  },[&] {
    if(ctx.r1.u32<48) throw std::runtime_error("invalid native shader retirement tag");
    return reader.Word(ctx.r1.u32-48);
  });
}
void BindNativeShaderResource(PPCContext& ctx,uint8_t* base,bool pixel) {
  BindNativeShaderResource(ctx,base,pixel,edf::native::GuestReader(base));
}
// 8213BA98's native branch: SetTexture's CPU words and retirement. Owned
// under the same flags as the shader setters; otherwise the guest setter runs.
bool NativeTextureBindingOwned() {
  return EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
    !(edf::native::native_queued_scene_group && edf::native::native_queued_scene_group->material_compatibility_only);
}
template<class Reader>
void BindNativeTextureResource(PPCContext& ctx,uint8_t* base,const Reader& reader) {
  const auto device=ctx.r3.u32;
  edf::native::SetNativeTextureResource(reader,device,ctx.r4.u32,ctx.r5.u32,ctx.r6.u64,[&] {
    auto work=ctx;
    if(work.r1.u32<160) throw std::runtime_error("invalid native texture setter stack");
    work.r1.u64=work.r1.u32-160; work.r3.u64=device; work.lr=0x8213BBE0;
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::RetirementAllocation);
    sub_82141440(work,base); return work.r3.u32;
  },[&] {
    if(ctx.r1.u32<80) throw std::runtime_error("invalid native texture retirement tag");
    return reader.Word(ctx.r1.u32-80);
  });
}
void PublishNativeShaderBinding(uint32_t device,uint32_t shader,bool pixel) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.shader_bindings.Set(device,shader,pixel);
}
void PublishNativeMaterialParameters(uint8_t* base,uint32_t instance) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  // CPU-owned metadata publication is serialized by the registry mutex.
  std::lock_guard lock(state.mutex);
  state.material_parameters.Publish(edf::native::GuestReader(base),instance);
}
void RetireNativeMaterialParameters(uint8_t* base,uint32_t instance,bool array) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  // Retained metadata snapshots keep their lifetime independently of this map.
  std::lock_guard lock(state.mutex);
  if(array) {
    if(instance<4) throw std::runtime_error("invalid material array header");
    const edf::native::GuestReader reader(base);
    state.material_parameters.RetireArray(instance,reader.Word(instance-4));
  } else state.material_parameters.Retire(instance);
}
void PublishNativeDeclarationContents(uint8_t* base,uint32_t handle) {
  if(!EDF_NATIVE_FLAG(shader_bridge) || !handle) return;
  auto& state=edf::native::State();
  // Immutable declaration snapshots issue no immediate-context commands.
  std::lock_guard lock(state.mutex);
  const edf::native::GuestReader reader(base);
  const auto count=reader.Word(reader.Add(handle,24));
  if(count>64) throw std::runtime_error("invalid published declaration count");
  const size_t bytes=size_t(count)*12;
  state.declarations.Publish(handle,bytes?std::span<const uint8_t>{reader.Bytes(reader.Add(handle,52),bytes),bytes}:
    std::span<const uint8_t>{});
}
void PublishNativeDeclarationBinding(uint32_t device,uint32_t declaration) {
  if(!EDF_NATIVE_FLAG(shader_bridge)) return;
  auto& state=edf::native::State();
  std::lock_guard submission(state.submissions);
  std::lock_guard lock(state.mutex);
  state.declaration_bindings.insert_or_assign(device,declaration);
}
}
REX_EXTERN(__imp__sub_82149AB0);
REX_EXTERN(__imp__sub_821BC230);
REX_HOOK_RAW(sub_821BC230) {
  const auto instance=ctx.r3.u32;
  RetireNativeMaterialParameters(base,instance,false);
  __imp__sub_821BC230(ctx,base);
  if(ctx.r3.u32&255) PublishNativeMaterialParameters(base,instance);
}
REX_EXTERN(__imp__sub_821BD110);
REX_HOOK_RAW(sub_821BD110) {
  const auto instance=ctx.r3.u32;
  RetireNativeMaterialParameters(base,instance,false);
  __imp__sub_821BD110(ctx,base);
  PublishNativeMaterialParameters(base,instance);
}
REX_EXTERN(__imp__sub_820ABE88);
REX_HOOK_RAW(sub_820ABE88) {
  RetireNativeMaterialParameters(base,ctx.r3.u32,false);
  __imp__sub_820ABE88(ctx,base);
}
REX_EXTERN(__imp__sub_820ABF20);
REX_HOOK_RAW(sub_820ABF20) {
  RetireNativeMaterialParameters(base,ctx.r3.u32,(ctx.r4.u32&2)!=0);
  __imp__sub_820ABF20(ctx,base);
}
REX_HOOK_RAW(sub_82149AB0) {
  __imp__sub_82149AB0(ctx,base);
  PublishNativeDeclarationContents(base,ctx.r3.u32);
}
REX_EXTERN(__imp__sub_82147BA0);
REX_HOOK_RAW(sub_82147BA0) {
  const auto handle=ctx.r4.u32;
  __imp__sub_82147BA0(ctx,base);
  PublishNativeDeclarationContents(base,handle);
}
REX_EXTERN(__imp__sub_821498C8);
REX_EXTERN(__imp__sub_8213BA98);
REX_HOOK_RAW(sub_8213BA98) {
  if(!NativeTextureBindingOwned()) {
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::TextureBinding);
    __imp__sub_8213BA98(ctx,base); return;
  }
  BindNativeTextureResource(ctx,base,edf::native::GuestReader(base));
}
REX_HOOK_RAW(sub_821498C8) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
     (!edf::native::native_queued_scene_group || !edf::native::native_queued_scene_group->material_compatibility_only)) BindNativeShaderResource(ctx,base,false);
  else {
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::ShaderBinding);
    __imp__sub_821498C8(ctx,base);
  }
  PublishNativeShaderBinding(device,shader,false);
}
REX_EXTERN(__imp__sub_82149608);
REX_HOOK_RAW(sub_82149608) {
  const auto device=ctx.r3.u32,shader=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(shader_bridge) && EDF_NATIVE_FLAG(material_activation) &&
     (!edf::native::native_queued_scene_group || !edf::native::native_queued_scene_group->material_compatibility_only)) BindNativeShaderResource(ctx,base,true);
  else {
    edf::native::EnterNativeSceneBoundary(edf::native::NativeSceneBoundary::ShaderBinding);
    __imp__sub_82149608(ctx,base);
  }
  PublishNativeShaderBinding(device,shader,true);
}
// Both retail writers of device+11536: the explicit declaration setter and
// the FVF setter. Keep CPU dirty flags and FVF conversion; draws consume the
// native binding. B0A0 returns the converted declaration in volatile r4, not
// its input FVF value (verified by its 47BA0 call and final store).
REX_EXTERN(__imp__sub_82149A90);
REX_HOOK_RAW(sub_82149A90) {
  const auto device=ctx.r3.u32,declaration=ctx.r4.u32;
  if(EDF_NATIVE_FLAG(shader_bridge)) {
    const edf::native::GuestReader reader(base);
    reader.StoreWord(reader.Add(device,11536),declaration);
    ctx.r12.u64=uint64_t(1)<<51;
    ctx.r11.u64=(uint64_t(reader.Word(reader.Add(device,16)))<<32)|reader.Word(reader.Add(device,20));
    ctx.r11.u64|=ctx.r12.u64;
    reader.StoreWord(reader.Add(device,16),uint32_t(ctx.r11.u64>>32));
    reader.StoreWord(reader.Add(device,20),uint32_t(ctx.r11.u64));
  } else __imp__sub_82149A90(ctx,base);
  PublishNativeDeclarationBinding(device,declaration);
}
REX_EXTERN(__imp__sub_8214B0A0);
REX_HOOK_RAW(sub_8214B0A0) {
  const auto device=ctx.r3.u32;
  __imp__sub_8214B0A0(ctx,base);
  PublishNativeDeclarationBinding(device,ctx.r4.u32);
}
// DrawIndexedVertices(device, primitive, baseVertex, firstIndex, indexCount).
// The model producer 821B2C28 uses triangle list (4). Inspect actual bound
// buffers before the original helper emits packets. Submit only inside the
// verified full-frame scene scope, never into an unrelated post-process target.
REX_EXTERN(__imp__sub_821FE358);
REX_EXTERN(__imp__edf_native_indexed_cpu_tail);
REX_HOOK_RAW(sub_821FE358) {
  bool native_submitted=false;
  bool native_geometry_rejected=false;
  // The stride and index width belong to the draw, not the declaration, and the
  // coverage catalog needs both to replay a layout at the size it was actually
  // drawn with. Resolved inside the draw, reported after it.
  uint32_t drawn_stride=0,drawn_index_width=0;
  edf::native::HookTiming native_timing(edf::native::HookPhase::IndexedNative);
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state=edf::native::State();
    edf::native::HookTiming submission_wait(edf::native::HookPhase::IndexedSubmissionWait);
    std::lock_guard submission(state.submissions);
    submission_wait.Finish();
    edf::native::HookTiming context_wait(edf::native::HookPhase::IndexedContextWait);
    std::lock_guard lock(state.mutex);
    context_wait.Finish();
    ++state.indexed_draws;
    if(edf::native::BufferWrites().Pending()) {
      const auto writes=edf::native::BufferWrites().Drain();
      edf::native::AuditGeneratedWrites(state,writes);
      size_t affected=0;
      state.model_buffers.ApplyWrites(writes,[&](uint32_t owner) { state.meshes.Invalidate(owner); ++affected; });
      if(writes.all) ++state.buffer_write_all_batches;
      const bool report_all=writes.all && (state.buffer_write_all_batches & (state.buffer_write_all_batches-1))==0;
      if(++state.buffer_write_batches<=8 || report_all || writes.pages)
        REXLOG_INFO("Native buffer write batch: ranges={}, all={}, affected={}, all_batches={}, pages={} (deferred notifications)",
          writes.count,writes.all,affected,state.buffer_write_all_batches,writes.pages?writes.pages->count():0);
    }
    const bool scene_draw=state.active_scene && !state.active_target && state.scenes.contains(state.active_scene);
    if (!scene_draw) {
      if (++state.indexed_outside_scene<=10)
        REXLOG_INFO("Native indexed outside scene: draw={}, scene={:#x}, target={:#x}, caller={:#x}, primitive={}, count={}",
          state.indexed_draws,state.active_scene,state.active_target,uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32);
    }
    if (state.indexed_draws<=20 || scene_draw) {
      try {
        edf::native::HookTiming setup_timing(edf::native::HookPhase::IndexedSetup);
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        // Material activation isn't necessarily the last shader change. Consult
        // native setter-owned identity before reusing its prepared parameters.
        const auto guest_shaders=state.shader_bindings.Pair(ctx.r3.u32);
        const auto guest_vertex=guest_shaders.vertex;
        const auto guest_pixel=guest_shaders.pixel;
        if (guest_vertex!=state.active_vertex || guest_pixel!=state.linked_pixel)
          throw std::runtime_error("indexed shader changed outside material activation: guestVS="+
            std::to_string(guest_vertex)+" nativeVS="+std::to_string(state.active_vertex)+
            " guestPS="+std::to_string(guest_pixel)+" nativePS="+std::to_string(state.linked_pixel));
        const auto stream=state.streams.at({ctx.r3.u32,0});
        const auto decl=state.declaration_bindings.at(ctx.r3.u32);
        const auto native_declaration=state.declarations.Get(decl);
        const auto elements=uint32_t(native_declaration->bytes().size()/12);
        const auto ib=state.index_bindings.at(ctx.r3.u32);
        const auto* native_vb=state.model_buffers.Find(stream.resource,edf::native::NativeModelBuffers::Kind::Vertex);
        const auto* native_ib=state.model_buffers.Find(ib,edf::native::NativeModelBuffers::Kind::Index);
        const unsigned ownership=(native_vb?1u:0u)|(native_ib?2u:0u);
        const auto ownership_count=++state.indexed_ownership[ownership];
        const auto ownership_attempt=++state.indexed_ownership_attempts;
        if(native_vb && native_ib && native_vb->physical && native_ib->physical) ++state.indexed_physical_pairs;
        if(ownership!=3 && (ownership_count<=8 || (ownership_count&(ownership_count-1))==0))
          REXLOG_INFO("Native indexed guest-header fallback: registered_vb={}, registered_ib={}, VB={:#x}, IB={:#x}, caller={:#x}, VS={:#x}, category_count={} (before header read; missing creator/lifetime ownership)",
            native_vb!=nullptr,native_ib!=nullptr,stream.resource,ib,uint32_t(ctx.lr),state.active_vertex,ownership_count);
        if(ownership_attempt<=8 || (ownership_attempt&(ownership_attempt-1))==0)
          REXLOG_INFO("Native indexed ownership coverage: attempts={}, both={}, vb_only={}, ib_only={}, neither={}, physical_pairs={} (resolved indexed attempts, not submitted draws; registration is not writer completeness)",
            ownership_attempt,state.indexed_ownership[3],state.indexed_ownership[1],state.indexed_ownership[2],
            state.indexed_ownership[0],state.indexed_physical_pairs);
        uint32_t vertex_address,vertex_bytes,index_address,index_bytes,index_width;
        if(native_vb) { vertex_address=native_vb->address; vertex_bytes=native_vb->bytes; }
        else {
          const auto header=edf::native::ReadGuestWords<2>(reader,reader.Add(stream.resource,24));
          vertex_address=header[0]&~3u; vertex_bytes=header[1]&0x03fffffcu;
        }
        if(native_ib) { index_address=native_ib->address; index_bytes=native_ib->bytes; index_width=native_ib->stride; }
        else {
          const auto header=edf::native::ReadGuestWords<8>(reader,ib);
          index_address=header[6]; index_bytes=header[7]; index_width=(header[0]&0x80000000u)?4:2;
        }
        drawn_stride=stream.stride; drawn_index_width=index_width;
        if (state.indexed_draws<=20) REXLOG_INFO("Native indexed input: draw={}, primitive={}, base={}, first={}, count={}, stride={}, vertex_bytes={}, index_bytes={}, index_width={}, elements={}, target={:#x}, native_vb={}, native_ib={}",
          state.indexed_draws,ctx.r4.u32,ctx.r5.s32,ctx.r6.u32,ctx.r7.u32,stream.stride,
          vertex_bytes,index_bytes,index_width,elements,state.active_target,native_vb!=nullptr,native_ib!=nullptr);
        if (ctx.r4.u32!=4 || !elements || elements>64 || stream.offset>=vertex_bytes ||
            vertex_bytes>128*1024*1024 || index_bytes>128*1024*1024)
          throw std::runtime_error("unsupported indexed geometry bounds/topology");
        auto& shader=state.shaders.at(state.active_vertex);
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        auto& bindings=edf::native::VertexBindingsForDraw(shader,viewport.reverse_depth);
        setup_timing.Finish();
        edf::native::HookTiming mesh_timing(edf::native::HookPhase::IndexedMesh);
        const auto declaration_bytes=native_declaration->bytes();
        std::span<const uint8_t> vertices_bytes,indices_bytes;
        const auto read_live_ranges=[&] {
          edf::native::HookTiming ranges_timing(edf::native::HookPhase::MeshRanges);
          vertices_bytes={reader.Bytes(reader.Add(vertex_address,stream.offset),vertex_bytes-stream.offset),vertex_bytes-stream.offset};
          indices_bytes={reader.Bytes(index_address,index_bytes),index_bytes};
        };
        auto mesh_watch_audit=state.mesh_watch_audit.lock();
        edf::native::HookTiming acquire_timing(edf::native::HookPhase::MeshAcquire);
        edf::native::HookTiming observe_timing(edf::native::HookPhase::MeshObserve);
        std::optional<edf::native::NativeBufferWrites::ObservedVersion> vertex_version,index_version;
        using GeometrySnapshots=std::array<edf::native::NativeBufferWrites::ObservedSnapshot,2>;
        std::optional<GeometrySnapshots> observed_geometry;
        auto vertex_contents=native_vb?native_vb->vertex_contents:nullptr;
        const bool prepare_queued=REXCVAR_GET(edf_native_prepared_geometry) &&
          EDF_NATIVE_FLAG(seam_draws) && uint32_t(ctx.lr)==0x821D97E8;
        edf::native::NativeIndexedMesh* preacquired_mesh=nullptr;
        const edf::native::NativeIndexedMesh::PreparedDraw* prepared_draw=nullptr;
        bool prepared_geometry=false;
        static uint64_t prepared_geometry_hits=0;
        if(native_vb && native_ib && native_vb->physical && native_ib->physical) {
          using Source=edf::native::NativeBufferWrites::SnapshotSource;
          edf::native::NativeBufferWrites::SnapshotFailure failure;
          const bool revision_audit=REXCVAR_GET(edf_native_retirement_audit);
          // Revision/writer-watch audits are the comparison's own oracles, so
          // they always read the source. Otherwise the comparison is sampled:
          // see NativeBufferWrites::SnapshotPolicy for the fail-closed rules.
          edf::native::NativeBufferWrites::SnapshotPolicy policy{};
          policy.audit_revisions=revision_audit;
          if(!revision_audit && !mesh_watch_audit) {
            policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
            policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
          }
          const auto index_contents=native_ib->index_contents?native_ib->index_contents:
            (native_ib->index_storage?native_ib->index_storage->SourceSnapshot():nullptr);
          using Identity=edf::native::NativeBufferWrites::SnapshotIdentity;
          if(prepare_queued) {
            using View=edf::native::NativeBufferWrites::SnapshotIdentityView;
            const auto versions=edf::native::BufferWrites().TryValidateObservedSet(std::array<View,2>{{
              {stream.resource,*native_vb->physical,vertex_bytes,&vertex_contents},
              {ib,*native_ib->physical,index_bytes,&index_contents}}},policy);
            if(versions) {
              vertices_bytes=std::span<const uint8_t>(*vertex_contents).subspan(stream.offset);
              indices_bytes=*index_contents;
              preacquired_mesh=state.meshes.TryAcquireOwned(edf::native::EnsureSceneBackendLocked(state),bindings.shader(),
                {stream.resource,ib,decl,state.active_vertex,uint32_t(viewport.reverse_depth)},
                *native_declaration,stream.stride,vertices_bytes,indices_bytes,index_width);
              if(preacquired_mesh) prepared_draw=preacquired_mesh->FindPreparedDraw(ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
              // Reuse attachment only if the registry still owns this exact
              // mesh's storage. Other shader/layout variants use normal commit.
              prepared_geometry=prepared_draw && native_vb->vertex_storage==preacquired_mesh->VertexStorage() &&
                native_ib->index_storage==preacquired_mesh->IndexStorage();
              if(prepared_geometry) ++prepared_geometry_hits;
              else observed_geometry=GeometrySnapshots{{
                {(*versions)[0],vertex_contents,false,true,false},
                {(*versions)[1],index_contents,false,true,false}}};
            }
          }
          if(!prepared_geometry) {
            if(!prepare_queued) observed_geometry=edf::native::BufferWrites().TryReuseObservedSet(std::array<Identity,2>{{
              {stream.resource,*native_vb->physical,vertex_bytes,vertex_contents},
              {ib,*native_ib->physical,index_bytes,index_contents}}},policy);
            std::optional<edf::native::GuestMeshWatchAudit::Observation> vertex_watch,index_watch;
            if(!observed_geometry) {
              // Only a real source comparison needs mapped guest ranges. The
              // registry rechecks writers/revisions after this validation; no
              // SDK heap lock is acquired while holding its writer mutex.
              read_live_ranges();
              const std::span<const uint8_t> full_vertices=stream.offset==0?vertices_bytes:
                std::span<const uint8_t>{reader.Bytes(vertex_address,vertex_bytes),vertex_bytes};
              if(mesh_watch_audit) {
                vertex_watch=mesh_watch_audit->Begin(reader.Add(vertex_address,stream.offset),vertices_bytes.size());
                index_watch=mesh_watch_audit->Begin(index_address,indices_bytes.size());
              }
              observed_geometry=edf::native::BufferWrites().CopyObservedSet(std::array<Source,2>{{
                {stream.resource,*native_vb->physical,full_vertices,vertex_contents},
                {ib,*native_ib->physical,indices_bytes,index_contents}}},&failure,policy);
            }
            if(mesh_watch_audit && observed_geometry) {
              if(vertex_watch) mesh_watch_audit->Finish(*vertex_watch,
                std::span<const uint8_t>(*(*observed_geometry)[0].contents).subspan(stream.offset));
              if(index_watch) mesh_watch_audit->Finish(*index_watch,*(*observed_geometry)[1].contents);
            }
            static std::array<uint64_t,2> revision_checked{},revision_without_baseline{},revision_missed{};
            if(revision_audit && observed_geometry) for(size_t slot=0;slot<2;++slot) {
              const auto& observation=(*observed_geometry)[slot];
              if(observation.revision_audited) ++revision_checked[slot];
              else ++revision_without_baseline[slot];
              if(observation.unreported_change) ++revision_missed[slot];
            }
            if(observed_geometry) for(size_t slot=0;slot<2;++slot) if((*observed_geometry)[slot].unreported_change)
              REXLOG_WARN("Native geometry revision audit: unreported change, owner={:#x}, index={}, lifetime={}, revision={} (live comparison repaired snapshot; sampled verification is now permanently disabled for this run)",
                slot?ib:stream.resource,slot==1,(*observed_geometry)[slot].version.lifetime,(*observed_geometry)[slot].version.revision);
            static uint64_t guarded=0,unavailable=0;
            static std::array<uint64_t,5> rejections{};
            if(observed_geometry) {
              ++guarded;
              vertex_contents=(*observed_geometry)[0].contents;
              vertices_bytes=std::span<const uint8_t>(*vertex_contents).subspan(stream.offset);
              indices_bytes=*(*observed_geometry)[1].contents;
              vertex_version=(*observed_geometry)[0].version; index_version=(*observed_geometry)[1].version;
            } else {
              ++unavailable; ++rejections.at(size_t(failure.reason));
              if(unavailable<=8 || (unavailable & (unavailable-1))==0)
                REXLOG_INFO("Native geometry snapshot rejection: unavailable={}, unknown={}, active={}, missing={}, extent={}, owner={:#x}, active_writers={} (classification at acquisition; no writer completeness claim)",
                  unavailable,rejections[1],rejections[2],rejections[3],rejections[4],failure.owner,failure.active_writers);
              if(unavailable<=8 || (unavailable & (unavailable-1))==0)
                REXLOG_INFO("Native geometry active write ranges: overlapping={}, unknown={}, same_thread={}, allocation_release={} (same-thread scopes cannot be waited out by this draw)",
                  failure.overlapping_writers,failure.unknown_writers,failure.same_thread_writers,failure.releasing_writers);
              if(unavailable<=8 || (unavailable & (unavailable-1))==0)
                REXLOG_INFO("Native geometry unknown writer providers: unspecified={}, bulk={}, file_read={}, word_fill={}, inline_indices={}, allocation_release={} (active scope counts at sampled rejection)",
                  failure.unknown_by_kind[0],failure.unknown_by_kind[1],failure.unknown_by_kind[2],failure.unknown_by_kind[3],failure.unknown_by_kind[4],failure.unknown_by_kind[5]);
              // Failed guarded acquisition never authorizes an unguarded read.
              // Neither active payload writers nor lost tracking/ownership are
              // repaired by comparing live bytes. Do not wait under these locks;
              // a producer may need the registry lock before it can complete.
              // The outer handler records an unsubmitted draw, not a safe retry.
              native_geometry_rejected=true;
              throw std::runtime_error("native geometry guarded acquisition failed; live fallback rejected");
            }
            const auto attempts=guarded+unavailable;
            if(attempts<=8 || (attempts & (attempts-1))==0) {
              REXLOG_INFO("Native geometry snapshot acquisition: guarded={}, unavailable={} (failed guarded acquisitions reject the draw)",guarded,unavailable);
              const auto trust=edf::native::BufferWrites().Trust();
              REXLOG_INFO("Native geometry comparison schedule: compared={}, trusted={}, unreported_changes={}, revoked={}, interval={}, initial={} (trusted observations reused a revision-proven snapshot without reading guest bytes)",
                trust.verified,trust.trusted,trust.unreported_changes,trust.revoked,
                policy.verify_interval,policy.verify_initial);
            }
            if(revision_audit && (attempts<=8 || (attempts & (attempts-1))==0))
              REXLOG_INFO("Native geometry revision audit coverage: checked_vb={}, checked_ib={}, without_baseline_vb={}, without_baseline_ib={}, missed_vb={}, missed_ib={} (successful snapshots only; checked requires same candidate and unchanged revision; not writer completeness)",
                revision_checked[0],revision_checked[1],revision_without_baseline[0],revision_without_baseline[1],revision_missed[0],revision_missed[1]);
          }
        } else read_live_ranges();
        if(mesh_watch_audit && state.indexed_draws%10000==0) {
          const auto& c=mesh_watch_audit->counters();
          REXLOG_INFO("Native mesh watch audit: checked={}, unsupported={}, stable={}, invalidated={}, missed={}, resets={}, excluded={}, foreign={} (shadow only; checked counts begun observations, rejected snapshots not compared)",
            c.checked,c.unsupported,c.stable,c.invalidated,c.missed,c.resets,c.excluded,c.foreign);
        }
        auto before_snapshot=[&] {
          if(observed_geometry) return; // Never label an earlier copy with a later writer epoch.
          if(native_vb && native_vb->physical) vertex_version=edf::native::BufferWrites().Version(stream.resource);
          if(native_ib && native_ib->physical) index_version=edf::native::BufferWrites().Version(ib);
        };
        observe_timing.Finish();
        edf::native::HookTiming lookup_timing(edf::native::HookPhase::MeshLookup);
        state.meshes.SetTimingsEnabled(REXCVAR_GET(edf_native_hook_timings));
        auto& mesh_backend=edf::native::EnsureSceneBackendLocked(state);
        const edf::native::NativeMeshCache::Key mesh_key{
          stream.resource,ib,decl,state.active_vertex,uint32_t(viewport.reverse_depth)};
        auto* owned_mesh=preacquired_mesh?preacquired_mesh:REXCVAR_GET(edf_native_owned_mesh_hit)?state.meshes.TryAcquireOwned(
          mesh_backend,bindings.shader(),mesh_key,*native_declaration,stream.stride,
          vertices_bytes,indices_bytes,index_width):nullptr;
        auto& mesh=owned_mesh?*owned_mesh:state.meshes.Acquire(mesh_backend,bindings.shader(),mesh_key,
          declaration_bytes,stream.stride,vertices_bytes,indices_bytes,index_width,native_declaration,{},
          native_ib?native_ib->index_storage:nullptr,native_vb?native_vb->vertex_storage:nullptr,
          {&before_snapshot,[](void* context) {
            (*static_cast<decltype(before_snapshot)*>(context))();
          }},vertex_contents,stream.offset,observed_geometry?(*observed_geometry)[1].contents:nullptr);
        lookup_timing.Finish();
        edf::native::HookTiming commit_timing(edf::native::HookPhase::MeshCommit);
        // The registry lock protects lifetimes; the queue handshake additionally
        // rejects completed notified writes during construction. Unnotified raw
        // writes still require the existing live source comparisons.
        if(state.indexed_draws<=5) {
          const auto& source=*mesh.IndexStorage()->SourceSnapshot();
          if(source.size()>=12) {
            const auto word=[&](size_t at) {
              uint32_t value=0;
              for(size_t lane=0;lane<4;++lane) value=(value<<8)|source[at+lane];
              return value;
            };
            REXLOG_INFO("Native indexed resources: VB={:#x} data={:#x} offset={}, IB={:#x} data={:#x}, index_width={}, first_words={:#x}/{:#x}/{:#x} (native draw snapshot)",
              stream.resource,vertex_address,stream.offset,ib,index_address,index_width,word(0),word(4),word(8));
          }
        }
        if(prepared_geometry) {
          // Guarded identities and registry storage matched the prepared mesh;
          // there is no new snapshot or attachment to publish.
        } else if(observed_geometry) {
          // Both source observations came from one guarded acquisition. Keep
          // attachment indivisible too, including a full VB with stream offset.
          if(native_vb->vertex_storage!=mesh.VertexStorage() ||
             native_vb->vertex_contents!=vertex_contents || native_ib->index_storage!=mesh.IndexStorage() ||
             native_ib->index_contents!=(*observed_geometry)[1].contents)
            state.model_buffers.CommitObservedGeometry(
              {stream.resource,native_vb->generation,*vertex_version},
              {ib,native_ib->generation,*index_version},mesh.VertexStorage(),vertex_contents,mesh.IndexStorage(),
              (*observed_geometry)[1].contents);
        } else {
          if(native_vb && native_vb->vertex_storage!=mesh.VertexStorage()) {
            if(vertex_version) state.model_buffers.CommitObservedVertex(stream.resource,native_vb->generation,*vertex_version,mesh.VertexStorage());
            else if(!native_vb->physical) state.model_buffers.RetainVertexStorage(stream.resource,native_vb->generation,mesh.VertexStorage());
          }
          if(native_vb && !stream.offset && !mesh.VertexStorage()->SourceOffset() &&
             mesh.VertexStorage()->SourceBytes()==mesh.VertexStorage()->SourceSnapshot()->size() &&
             native_vb->vertex_contents!=mesh.VertexStorage()->SourceSnapshot()) {
            state.model_buffers.RetainVertexContents(stream.resource,native_vb->generation,
              mesh.VertexStorage()->SourceSnapshot(),vertex_version);
          }
          if(native_ib && native_ib->index_storage!=mesh.IndexStorage()) {
            if(index_version) state.model_buffers.CommitObservedIndex(ib,native_ib->generation,*index_version,mesh.IndexStorage());
            else if(!native_ib->physical) state.model_buffers.RetainIndexStorage(ib,native_ib->generation,mesh.IndexStorage());
          }
        }
        commit_timing.Finish();
        acquire_timing.Finish();
        edf::native::HookTiming draw_range_timing(edf::native::HookPhase::MeshDrawRange);
        if(prepare_queued) prepared_draw=&mesh.PrepareDraw(ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
        else mesh.ValidateDraw(ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
        draw_range_timing.Finish();
        mesh_timing.Finish();
        ++state.indexed_uploads;
        if (scene_draw) {
          edf::native::HookTiming binding_timing(edf::native::HookPhase::IndexedBindings);
          const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
          auto found=state.render_states.find(key);
          if (found==state.render_states.end()) found=state.render_states.emplace(key,
            edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
          // 77.4% of this game's indexed draws repeat the one before them in
          // mesh, material and state, differing only in the constants an
          // activation patches between them. The targets and render state do
          // not change across such a run, so binding them again per draw is
          // pure repetition - which is the batch, without needing the draws
          // themselves to merge.
          //
          // Skipped only when nothing else has bound since we did: the
          // generation catches another path replacing the state underneath us,
          // which a comparison against our own cache never would. A state that
          // needs a constant blend factor is never skipped, because the factor
          // can change while the state words do not.
          const bool same_binding=state.indexed_bind_valid &&
            state.bind_generation==state.indexed_bind_generation &&
            state.indexed_bind_key==key &&
            state.indexed_bind_target==state.active_target &&
            state.indexed_bind_scene==state.active_scene &&
            state.indexed_bind_output==state.active_output &&
            !found->second.requires_blend_factor;
          // The same guard extended to the material. When the previous draw
          // left this very shader pair bound with these textures and samplers,
          // the only thing this draw changes is its constants, which an
          // activation has just patched. Re-sending the shader, the texture
          // runs and the sampler runs is the same calls with the same
          // arguments, once per draw, for 77.4% of them.
          //
          // The reversed-depth variant is a different shader object under the
          // same handle, so it has to be part of the comparison: a draw that
          // flips depth direction would otherwise reuse the wrong one.
          const bool same_material=same_binding &&
            REXCVAR_GET(edf_native_reuse_material) &&
            state.indexed_bind_vertex==state.active_vertex &&
            state.indexed_bind_pixel==state.linked_pixel &&
            state.indexed_bind_reversed==viewport.reverse_depth;
          // Recorded draws set all of this per draw through the pipeline and
          // the recorder, so the context bindings below are not merely
          // redundant, they describe a draw that is not going to happen.
          const bool seam_draws=EDF_NATIVE_FLAG(seam_draws);
          if(seam_draws) { /* bound per draw below */ }
          else if(same_binding) ++state.indexed_binds_skipped;
          else {
            edf::native::BindActiveTarget(state);
            edf::native::BindGuestRenderState(found->second,*state.context.Get(),reader,ctx.r3.u32,
                                             &state.bind_generation);
            state.indexed_bind_valid=true;
            state.indexed_bind_key=key;
            state.indexed_bind_target=state.active_target;
            state.indexed_bind_scene=state.active_scene;
            state.indexed_bind_output=state.active_output;
            state.indexed_bind_generation=state.bind_generation;
            ++state.indexed_binds_bound;
          }
          if(!seam_draws) {
            viewport.Bind(*state.context.Get());
            if(same_material) {
              bindings.BindConstants(*state.context.Get());
              state.shaders.at(state.linked_pixel).bindings->BindConstants(*state.context.Get());
              ++state.indexed_materials_reused;
            } else {
              bindings.Bind(*state.context.Get());
              state.shaders.at(state.linked_pixel).bindings->Bind(*state.context.Get());
              state.indexed_bind_vertex=state.active_vertex;
              state.indexed_bind_pixel=state.linked_pixel;
              state.indexed_bind_reversed=viewport.reverse_depth;
            }
          }
          binding_timing.Finish();
          if(REXCVAR_GET(edf_native_capture_indexed_state) &&
             !REXCVAR_GET(edf_native_scene_capture).empty()) {
            // This scene is a candidate for the next indexed output. If output
            // fails, the index may be reused; keep the cap across those retries.
            const auto frame=state.indexed_output_frames+1;
            if(state.indexed_trace_frame!=frame) {
              state.indexed_trace_frame=frame;
              state.indexed_trace_draws=0;
            }
            if(state.indexed_trace_draws<256 && edf::native::ShouldCaptureNativeOutput(
                frame,state.output_captures,REXCVAR_GET(edf_native_output_capture_limit),
                REXCVAR_GET(edf_native_output_capture_interval),REXCVAR_GET(edf_native_output_capture_start_frame))) {
              ++state.indexed_trace_draws;
              REXLOG_INFO("Native indexed capture state: next_output_candidate={}, sample={}/256, scene={:#x}, draw={}, VS={:#x}, PS={:#x}, VB={:#x}, IB={:#x}, declaration={:#x}, first={}, count={}, base={}, blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, mask={}, scissor={}, depth_range={}..{}, reversed={}; state before draw, not proof of output",
                frame,state.indexed_trace_draws,state.active_scene,state.indexed_draws,
                state.active_vertex,state.linked_pixel,stream.resource,ib,decl,
                ctx.r6.u32,ctx.r7.u32,ctx.r5.s32,key[0],key[1],key[2],key[3],key[4],key[5],
                viewport.viewport.MinDepth,viewport.viewport.MaxDepth,viewport.reverse_depth);
              REXLOG_INFO("Native indexed capture viewport: draw={}, xy={},{} extent={}x{}, scene_extent={}x{}",
                state.indexed_draws,viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,
                viewport.viewport.Width,viewport.viewport.Height,
                state.scenes.at(state.active_scene).color.sampled.width,
                state.scenes.at(state.active_scene).color.sampled.height);
            }
          }
          const std::array<uint32_t,4> probe_key{state.active_vertex,state.linked_pixel,key[2],key[1]};
          // The clip probe replays the draw with a stream-output geometry
          // shader and requires this VS already bound on the context. A
          // recorded draw binds nothing there, so the replay would refuse -
          // correctly, but once per draw and in the log. Skip it instead.
          if (!seam_draws && !REXCVAR_GET(edf_native_scene_capture).empty() && state.clip_probes.size()<24 &&
              state.clip_probes.insert(probe_key).second) {
            try {
              const auto positions=mesh.CaptureClipPositions(*state.context.Get(),bindings.shader(),
                ctx.r6.u32,(std::min)(ctx.r7.u32,9u),ctx.r5.s32);
              REXLOG_INFO("Native clip probe: VS={:#x} {}, PS={:#x} {}, raster={:#x}, depth={:#x}, first={}, base={}, vertices={}",
                state.active_vertex,bindings.shader().entry.name,state.linked_pixel,
                state.shaders.at(state.linked_pixel).bindings->shader().entry.name,key[2],key[1],ctx.r6.u32,ctx.r5.s32,positions.size());
              for(const auto* name:{"g_mWorld","g_mView","g_mProjection","g_mViewProjection"}) {
                if(const auto matrix=bindings.ReadFloat4x4(name)) for(size_t row=0;row<4;++row)
                  REXLOG_INFO("Native clip matrix: name={}, row={}, values={},{},{},{}",name,row,
                    (*matrix)[row*4],(*matrix)[row*4+1],(*matrix)[row*4+2],(*matrix)[row*4+3]);
              }
              // Log the original POSITION0 float3 for the same indexed samples.
              // This is diagnostic-only; other declaration formats still render.
              for(uint32_t element=0;element<elements;++element) {
                const auto declaration_word=[&](size_t at) {
                  uint32_t value=0;
                  for(size_t lane=0;lane<4;++lane) value=(value<<8)|declaration_bytes[at+lane];
                  return value;
                };
                const auto at=size_t(element)*12;
                if(declaration_word(at+8)!=0 || declaration_word(at+4)!=0x2a23b9) continue;
                const auto position_offset=declaration_word(at);
                if(position_offset>=stream.stride || stream.stride-position_offset<12) continue;
                const auto inputs=mesh.CaptureSourceFloat3(ctx.r6.u32,uint32_t(positions.size()),ctx.r5.s32,position_offset);
                for(size_t i=0;i<inputs.size();++i) {
                  REXLOG_INFO("Native clip input: index={}, xyz={},{},{} (native draw snapshot)",i,
                    inputs[i][0],inputs[i][1],inputs[i][2]);
                }
                break;
              }
              for(size_t i=0;i<positions.size();++i) {
                const auto& p=positions[i];
                REXLOG_INFO("Native clip vertex: index={}, xyzw={},{},{},{}",i,p[0],p[1],p[2],p[3]);
              }
            } catch(const std::exception& error) { REXLOG_ERROR("Native clip probe: {}",error.what()); }
          }
          Microsoft::WRL::ComPtr<ID3D11Query> visibility;
          // A context query cannot count a draw that was recorded rather than
          // issued on that context: it would return zero for every draw and
          // read as "nothing was visible". The visibility diagnostic is a
          // capture-time tool, so it is simply not taken on the recorded path
          // rather than reporting a number that means nothing.
          if (!seam_draws && !REXCVAR_GET(edf_native_scene_capture).empty() &&
              state.scene_captures<3 && state.visibility.size()<2048) {
            const D3D11_QUERY_DESC query_desc{D3D11_QUERY_OCCLUSION,0};
            if (SUCCEEDED(state.device->CreateQuery(&query_desc,&visibility))) state.context->Begin(visibility.Get());
          }
          // Batching precondition. A run of draws that share mesh, declaration,
          // shader pair, render state and index range, and differ only in the
          // per-instance constants patched by 821D9600, is exactly what one
          // DrawIndexedInstanced replaces. Measure the run lengths before
          // building that: the mean run length is the draw-call reduction, and
          // a mean near 1 would mean there is nothing to collapse.
          // Every hundred thousand rather than every million: a run slow enough
          // to need this explanation never reaches a million draws, which made
          // the one counter that could explain it unreachable.
          if(state.recorded_draws && state.recorded_draws%100000==0)
            REXLOG_INFO("Native recorded binding reuse: draws={}, pipeline_skips={} ({:.1f}%), material_skips={} ({:.1f}%), constant_buffer_skips={} ({:.2f} per draw) (a skip is something the recorder already held, so the draw did not re-send it)",
              state.recorded_draws,state.recorded_pipeline_skips,
              100.0*double(state.recorded_pipeline_skips)/double(state.recorded_draws),
              state.recorded_material_skips,
              100.0*double(state.recorded_material_skips)/double(state.recorded_draws),
              state.recorded_constant_skips,
              double(state.recorded_constant_skips)/double(state.recorded_draws));
          if(REXCVAR_GET(edf_native_batch_audit) && state.indexed_draws%1000000==0)
            REXLOG_INFO("Native indexed binding reuse: bound={}, skipped={} ({:.1f}% of draws bound no target or render state, because the draw before them had already bound the same)",
              state.indexed_binds_bound,state.indexed_binds_skipped,
              (state.indexed_binds_bound+state.indexed_binds_skipped)
                ?100.0*double(state.indexed_binds_skipped)/double(state.indexed_binds_bound+state.indexed_binds_skipped):0.0);
          if(REXCVAR_GET(edf_native_batch_audit) && state.indexed_draws%1000000==0)
            REXLOG_INFO("Native indexed material reuse: draws={}, constants_only={} ({:.1f}% re-sent only their constants, keeping the shader pair, textures and samplers the previous draw bound)",
              state.indexed_draws,state.indexed_materials_reused,
              state.indexed_draws?100.0*double(state.indexed_materials_reused)/double(state.indexed_draws):0.0);
          if(REXCVAR_GET(edf_native_batch_audit)) {
            const std::array<uint32_t,12> batch_key{stream.resource,ib,decl,
              state.active_vertex,state.linked_pixel,ctx.r6.u32,ctx.r7.u32,uint32_t(ctx.r5.s32),
              key[0],key[1],key[2],key[3]};
            ++state.batch_draws;
            if(batch_key==state.last_batch_key && state.instance_shape!=state.last_instance_shape)
              ++state.batch_shape_breaks;
            if(batch_key==state.last_batch_key) ++state.batch_run;
            else {
              if(state.batch_run) {
                state.batch_longest=(std::max)(state.batch_longest,state.batch_run);
                state.batch_run_total+=state.batch_run;
                ++state.batch_runs;
                if(state.batch_run>=2) state.batch_collapsible+=state.batch_run-1;
              }
              state.batch_run=1; state.last_batch_key=batch_key;
            }
            state.last_instance_shape=state.instance_shape;
            if(state.batch_draws%1000000==0) {
              REXLOG_INFO("Native batch audit: draws={}, runs={}, mean_run={:.2f}, longest_run={}, collapsible_draws={} ({:.1f}% of draws could be folded into a preceding instanced draw)",
                state.batch_draws,state.batch_runs,
                state.batch_runs?double(state.batch_run_total)/double(state.batch_runs):0.0,
                state.batch_longest,state.batch_collapsible,
                100.0*double(state.batch_collapsible)/double(state.batch_draws));
              REXLOG_INFO("Native batch shape: collapsible={}, register_shape_breaks={} ({:.2f}% of collapsible draws patch a different register range than the draw before, so cannot share one instanced variant)",
                state.batch_collapsible,state.batch_shape_breaks,
                state.batch_collapsible?100.0*double(state.batch_shape_breaks)/double(state.batch_collapsible):0.0);
            }
          }
          try {
            if(seam_draws) {
              edf::native::HookTiming record_timing(edf::native::HookPhase::IndexedRecord);
              const auto setup=[&]() -> edf::native::NativeBackendRecorder& { return edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
                bindings,*state.shaders.at(state.linked_pixel).bindings,viewport,key,
                mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
                (uint64_t(state.active_vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
                state.linked_pixel,edf::native::NativeBackendTopology::TriangleList,
                REXCVAR_GET(edf_native_world_instancing) && uint32_t(ctx.lr)==0x821D97E8}); };
              auto* group=edf::native::native_queued_scene_group;
              const bool native_material_setup=group && uint32_t(ctx.lr)==0x821D97E8 &&
                EDF_NATIVE_FLAG(scene_material_owned) && !REXCVAR_GET(edf_native_scene_material_audit);
              // Native scene rendering binds its own pipeline, resources and
              // constants. Only audit/fallback draws need the live bindings.
              auto& recorder=native_material_setup?edf::native::SceneRecorderLocked(state):setup();
              record_timing.Finish();
              edf::native::HookTiming draw_timing(edf::native::HookPhase::IndexedDraw);
              bool native_scene_draw=false;
              if(group && uint32_t(ctx.lr)==0x821D97E8) {
                std::optional<edf::native::NativeSceneMaterialCapture> captured;
                uint64_t object=0;
                try {
                  const auto* source=state.scene_sources.Find(group->instance);
                  if(!source) throw std::runtime_error("queued instance has no audited scene lifetime");
                  const bool owned=EDF_NATIVE_FLAG(scene_material_owned);
                  const bool audit=REXCVAR_GET(edf_native_scene_material_audit) && !group->material_audited;
                  if(!owned || audit)
                    captured=edf::native::CaptureNativeSceneMaterial(state.scene_backend,*state.recorded.pipeline,
                      bindings,*state.shaders.at(state.linked_pixel).bindings,
                      state.recorded.blend_factor_needed?std::optional(state.recorded.blend_factor):std::nullopt,group->material);
                  const auto resolve=[&](const auto& constants) {
                      if(!group->published_material || !group->material_pass)
                        throw std::runtime_error("native material publication or pass missing");
                      const auto& program=*group->published_material->program;
                      if(program.inputs.vertex!=state.active_vertex || program.inputs.pixel!=state.linked_pixel)
                        throw std::runtime_error("published shader pair differs from visible group");
                      const auto targets=edf::native::ActiveTargetsLocked(state);
                      if(!targets.count) throw std::runtime_error("native material pass has no color target");
                      edf::native::NativeBackendPipelineDesc desc;
                      desc.vertex_id=(uint64_t(state.active_vertex)<<1)|uint64_t(viewport.reverse_depth);
                      desc.pixel_id=state.linked_pixel;
                      desc.input_layout=mesh.input_layout().elements(); desc.input_layout_id=mesh.input_layout().fingerprint();
                      desc.render_targets=targets.count; desc.rtv_format=targets.rtv_format;
                      desc.dsv_format=targets.dsv_format; desc.sample_count=targets.samples;
                      return program.Resolve(desc,viewport.reverse_depth,constants,*group->material_pass,group->sampler_pass,
                        REXCVAR_GET(edf_native_anisotropic_filtering)).capture;
                  };
                  std::optional<edf::native::NativeSceneMaterialCapture> resolved;
                  if(owned) {
                    resolved=resolve(group->pass_constants);
                    const auto world=state.scene_sources.WorldRegisters(*source,source->world_data);
                    if(!world || !state.active_vertex_parameters)
                      throw std::runtime_error("owned native instance has no published world");
                    const auto& parameters=*state.active_vertex_parameters;
                    const auto parameter=std::find_if(parameters.begin(),parameters.end(),[](const auto& value) {
                      return value.name=="g_mWorld" && value.count==4;
                    });
                    if(parameter==parameters.end() || !edf::native::NativeStaticWorldOnly(reader,state.scene_sources,
                       group->instance,parameter->first,ctx.r3.u32))
                      throw std::runtime_error("owned native instance has additional overrides");
                    edf::native::ApplyNativeScenePublishedWorld(*resolved,*world);
                  }
                  if(audit) {
                    group->material_audited=true;
                    static uint64_t checked=0,mismatched=0,missing=0,animation_generation_differences=0;
                    static std::set<std::string> reasons;
                    if(!group->published_material || !group->material_pass) ++missing;
                    else try {
                      if(!resolved) resolved=resolve(group->pass_constants);
                      bool equivalent=resolved->material->Equivalent(*captured->material);
                      bool generation_difference=false;
                      if(!equivalent) {
                        // Audit a later producer generation separately. Keep the rendered
                        // material on its immutable pass snapshot; never copy live constants.
                        const auto latest=state.scene_adapter.AcquireWorldAnimations();
                        const auto producer=latest->find(edf::native::native_scene_animation_owner);
                        if(latest!=edf::native::native_scene_pass_animations && producer!=latest->end()) {
                          auto constants=group->pass_constants;
                          for(auto& constant:constants) producer->second.Apply(constant);
                          const auto current=resolve(constants);
                          if(current.material->Equivalent(*captured->material)) {
                            equivalent=true;
                            generation_difference=true;
                          }
                        }
                      }
                      if(!equivalent) {
                        std::string difference="pipeline/resources";
                        for(const auto& expected:resolved->material->constants()) {
                          const auto& actuals=captured->material->constants();
                          const auto actual=std::find_if(actuals.begin(),actuals.end(),[&](const auto& value) {
                            return value.stage==expected.stage && value.slot==expected.slot;
                          });
                          if(actual==actuals.end() || actual->bytes.size()!=expected.bytes.size()) {
                            difference="constant buffer shape"; break;
                          }
                          const auto mismatch=std::mismatch(expected.bytes.begin(),expected.bytes.end(),actual->bytes.begin());
                          if(mismatch.first==expected.bytes.end()) continue;
                          const auto offset=size_t(mismatch.first-expected.bytes.begin());
                          const auto& shader=expected.stage==edf::native::NativeBackendStage::Vertex?
                            (viewport.reverse_depth?group->published_material->program->reversed_vertex:group->published_material->program->vertex):
                            group->published_material->program->pixel;
                          D3D11_SHADER_DESC shader_desc{};
                          shader.reflection->GetDesc(&shader_desc);
                          difference="unnamed constant";
                          for(UINT index=0;index<shader_desc.ConstantBuffers;++index) {
                            auto* buffer=shader.reflection->GetConstantBufferByIndex(index);
                            D3D11_SHADER_BUFFER_DESC buffer_desc{}; buffer->GetDesc(&buffer_desc);
                            D3D11_SHADER_INPUT_BIND_DESC binding{};
                            shader.reflection->GetResourceBindingDescByName(buffer_desc.Name,&binding);
                            if(binding.BindPoint!=expected.slot) continue;
                            for(UINT variable=0;variable<buffer_desc.Variables;++variable) {
                              D3D11_SHADER_VARIABLE_DESC value{}; buffer->GetVariableByIndex(variable)->GetDesc(&value);
                              if(offset>=value.StartOffset && offset<value.StartOffset+value.Size) difference=value.Name;
                            }
                          }
                          static uint64_t details=0;
                          if(details++<16) {
                            uint32_t wanted=0,seen=0;
                            std::memcpy(&wanted,expected.bytes.data()+(offset&~size_t(3)),4);
                            std::memcpy(&seen,actual->bytes.data()+(offset&~size_t(3)),4);
                            REXLOG_INFO("Native scene constant difference: name={} stage={} slot={} byte={} native={:#x} visible={:#x}",
                              difference,uint32_t(expected.stage),expected.slot,offset,wanted,seen);
                            const auto latest=state.scene_adapter.AcquireWorldAnimations();
                            const auto producer=latest->find(edf::native::native_scene_animation_owner);
                            const auto selected=edf::native::native_scene_pass_animation;
                            if(producer!=latest->end() && selected)
                              REXLOG_INFO("Native animation timing: owner={:#x} selected_water={:#x} latest_water={:#x} selected_counter={} latest_counter={} same_generation={}",
                                edf::native::native_scene_animation_owner,selected->water_time,producer->second.water_time,
                                selected->signal_counter,producer->second.signal_counter,latest==edf::native::native_scene_pass_animations);
                            if(latest->size()<=4) for(const auto& [owner,value]:*latest)
                              REXLOG_INFO("Native animation producer: owner={:#x} water={:#x} counter={}",owner,value.water_time,value.signal_counter);
                          }
                          break;
                        }
                        throw std::runtime_error("pass-time material bindings differ: "+difference);
                      }
                      if(resolved->camera.view!=captured->camera.view ||
                         resolved->camera.projection!=captured->camera.projection ||
                         resolved->camera.view_projection!=captured->camera.view_projection)
                        throw std::runtime_error("pass-time camera differs from visible capture");
                      if(owned && resolved->world!=captured->world)
                        throw std::runtime_error("published native world differs from visible capture");
                      if(generation_difference) ++animation_generation_differences;
                      else ++checked;
                    } catch(const std::exception& error) {
                      ++mismatched;
                      if(reasons.size()<32 && reasons.insert(error.what()).second)
                        REXLOG_INFO("Native scene material comparison mismatch: {}",error.what());
                      if(owned) throw;
                    }
                    const auto total=checked+mismatched+missing+animation_generation_differences;
                    if(total<=4 || total%10000==0)
                      REXLOG_INFO("Native scene material comparison: checked={} mismatched={} missing={} animation_generation_differences={}",checked,mismatched,missing,animation_generation_differences);
                  }
                  if(owned) {
                    captured=std::move(resolved);
                    if(native_material_setup) ++state.scene_material_binding_bypasses;
                    if(++state.scene_material_constructed<=4 || state.scene_material_constructed%10000==0)
                      REXLOG_INFO("Native scene owned materials: constructed={} rejected={} live_binding_bypasses={} (published constants, native pass inputs and published world)",
                        state.scene_material_constructed,state.scene_material_rejected,state.scene_material_binding_bypasses);
                  }
                  if(!group->geometry) {
                    ++state.scene_group_material_captures;
                    if(captured->material==group->material) ++state.scene_group_material_reused;
                  }
                  group->material=captured->material;
                  object=state.scene_adapter.Observe(*source,state.scene_backend,mesh,ctx.r6.u32,ctx.r7.u32,ctx.r5.s32,*captured);
                } catch(const std::exception& error) {
                  captured.reset(); ++state.scene_native_fallbacks;
                  if(EDF_NATIVE_FLAG(scene_material_owned) &&
                     (++state.scene_material_rejected<=4 || state.scene_material_rejected%10000==0))
                    REXLOG_INFO("Native scene owned material rejected: count={} reason={}",state.scene_material_rejected,error.what());
                  if(state.scene_native_reasons.size()<32 && state.scene_native_reasons.insert(error.what()).second)
                    REXLOG_INFO("Native scene queued fallback: {}",error.what());
                }
                if(captured) {
                  auto view=captured->camera;
                  const auto& v=viewport.viewport; const auto& s=viewport.scissor;
                  view.viewport={v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth};
                  view.scissor={s.left,s.top,s.right,s.bottom}; view.scissor_enabled=key[5]!=0;
                  const auto targets=edf::native::ActiveTargetsLocked(state);
                  if(!group->objects.empty() && (group->view.view!=view.view || group->view.projection!=view.projection ||
                     group->view.view_projection!=view.view_projection || group->view.scissor_enabled!=view.scissor_enabled ||
                     std::memcmp(&group->view.viewport,&view.viewport,sizeof(view.viewport)) ||
                     std::memcmp(&group->view.scissor,&view.scissor,sizeof(view.scissor)) ||
                     group->targets.count!=targets.count || group->targets.colors!=targets.colors || group->targets.depth!=targets.depth))
                    edf::native::FlushNativeQueuedSceneLocked(state,*group);
                  group->view=view; group->targets=targets; group->objects.push_back(edf::native::SelectNativeSceneInstanceLocked(state,object));
                  group->geometry=group->objects.back()->object.geometry;
                  group->material=group->objects.back()->object.material;
                  group->reverse_depth=viewport.reverse_depth;
                  group->vertex=state.active_vertex; group->pixel=state.linked_pixel;
                  edf::native::ConfigureNativeQueuedWorldLocked(state,*group);
                  native_scene_draw=true;
                } else {
                  group->geometry.reset();
                  edf::native::FlushNativeQueuedSceneLocked(state,*group);
                  setup(); // Native rendering changed bindings needed by this fallback draw.
                }
              }
              if(!native_scene_draw) {
                if(prepared_draw) prepared_draw->Draw(recorder);
                else mesh.Draw(recorder,ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
              }
              if(EDF_NATIVE_FLAG(scene_queued) && state.indexed_submitted%100000==0)
                REXLOG_INFO("Native scene queued: objects={} rendered={} draws={} fallback={}",state.scene_adapter.objects(),
                  state.scene_native_objects,state.scene_native_draws,state.scene_native_fallbacks);
            } else mesh.Draw(*state.context.Get(),ctx.r6.u32,ctx.r7.u32,ctx.r5.s32);
          }
          catch (...) { if (visibility) state.context->End(visibility.Get()); throw; }
          if (visibility) {
            state.context->End(visibility.Get());
            state.visibility.push_back({std::move(visibility),{state.active_vertex,state.linked_pixel,key[2],key[1]}});
          }
          ++state.indexed_submitted;
          native_submitted=true;
          edf::native::HookTiming tail_timing(edf::native::HookPhase::IndexedTail);
          const auto probe_x=REXCVAR_GET(edf_native_probe_x),probe_y=REXCVAR_GET(edf_native_probe_y);
          const auto probe_frame=REXCVAR_GET(edf_native_probe_frame);
          const bool probe_window=probe_frame>0 ? state.indexed_output_frames+1>=uint64_t(probe_frame) : state.scene_captures==0;
          if(probe_x>=0 && probe_y>=0 && !state.color_probe_done && probe_window) {
            try {
              const auto& scene=state.scenes.at(state.active_scene);
              uint32_t sample_x=uint32_t(probe_x),sample_y=uint32_t(probe_y);
              const auto width=REXCVAR_GET(edf_native_probe_width),height=REXCVAR_GET(edf_native_probe_height);
              if(width<=0 || height<=0) throw std::runtime_error("invalid diagnostic region size");
              std::array<float,4> rgba{};
              const bool probe_negative=REXCVAR_GET(edf_native_probe_negative);
              if(!scene.color.surface)
                throw std::runtime_error("the colour probe reads the surface through D3D11, which a scene on another backend has not got");
              if((width==1 && height==1) || edf::native::FindNativeInvalidColorPixel(
                  *state.context.Get(),*scene.color.surface.Get(),sample_x,sample_y,
                  uint32_t(width),uint32_t(height),sample_x,sample_y,probe_negative))
                rgba=edf::native::ReadNativeColorPixel(*state.context.Get(),*scene.color.surface.Get(),sample_x,sample_y);
              ++state.color_probe_draws;
              const bool nonfinite=!std::isfinite(rgba[0]) || !std::isfinite(rgba[1]) || !std::isfinite(rgba[2]);
              const bool negative=probe_negative &&
                (rgba[0]<=-1.f || rgba[1]<=-1.f || rgba[2]<=-1.f);
              if(nonfinite || negative) {
                state.color_probe_done=true;
                // Scans since the probe window opened. A hit on the first scan
                // means the pixel was already bad before the blamed draw ran,
                // so the draw is only "the first one scanned", not the producer.
                REXLOG_INFO("Native invalid RGB frame: next_output_candidate={}, kind={}, scans={} (scans==1 means the value predates this draw)",
                  state.indexed_output_frames+1,nonfinite?"nonfinite":"negative",state.color_probe_draws);
                REXLOG_INFO("Native invalid RGB origin: pixel={},{} draw={} VS={:#x} {} source={:#x} PS={:#x} {} source={:#x} rgba={},{},{},{}",
                  sample_x,sample_y,state.indexed_submitted,state.active_vertex,bindings.shader().entry.name,
                  state.shaders.at(state.active_vertex).source_fingerprint,state.linked_pixel,
                  state.shaders.at(state.linked_pixel).bindings->shader().entry.name,
                  state.shaders.at(state.linked_pixel).source_fingerprint,rgba[0],rgba[1],rgba[2],rgba[3]);
                // m_tD_trans is alpha blended and its lighting cannot go
                // negative, so the decoded blend operation is the first thing
                // to rule out: a SUBTRACT or REV_SUBTRACT where the guest asked
                // for ADD drives the target below zero from positive inputs.
                // Everything inside the shader is provably non-negative for the
                // logged constants, so the remaining candidate is the geometry
                // reaching the rasteriser. A vertex at w<=0, or a triangle
                // spanning the eye plane, makes perspective-correct
                // interpolation extrapolate the colour varyings far outside the
                // range the vertex shader can emit.
                try {
                  // Same reason as the clip probe above: the replay needs this
                  // VS bound on the context, and a recorded draw binds nothing
                  // there.
                  if(seam_draws) throw std::runtime_error(
                    "clip positions cannot be replayed for a draw recorded through the backend; "
                    "run with --edf_native_seam_draws=false to diagnose this");
                  const auto clip=mesh.CaptureClipPositions(*state.context.Get(),bindings.shader(),
                    ctx.r6.u32,(std::min)(ctx.r7.u32,24u),ctx.r5.s32);
                  float min_w=std::numeric_limits<float>::infinity(),max_w=-min_w;
                  size_t nonpositive=0,nonfinite_w=0;
                  for(const auto& position:clip) {
                    if(!std::isfinite(position[3])) { ++nonfinite_w; continue; }
                    min_w=(std::min)(min_w,position[3]); max_w=(std::max)(max_w,position[3]);
                    if(position[3]<=0) ++nonpositive;
                  }
                  REXLOG_INFO("Native invalid RGB clip: sampled={}, min_w={}, max_w={}, nonpositive_w={}, nonfinite_w={} (mixed-sign or near-zero w extrapolates interpolated colour)",
                    clip.size(),min_w,max_w,nonpositive,nonfinite_w);
                  for(size_t i=0;i<clip.size() && i<8;++i)
                    REXLOG_INFO("Native invalid RGB clip vertex: index={}, xyzw={},{},{},{}",
                      i,clip[i][0],clip[i][1],clip[i][2],clip[i][3]);
                } catch(const std::exception& error) {
                  REXLOG_INFO("Native invalid RGB clip capture: {}",error.what());
                }
                // Color = Dtex * In.Color + In.Specular, and the lighting terms
                // are provably non-negative for the logged constants, so the
                // sampled texel is the remaining candidate. A UNORM/BC format
                // cannot be negative; a signed or float one can.
                for(const auto* name:{"m_DiffuseTexture0_Sampler","m_ParameterTexture0_Sampler",
                    "m_NormalTexture0_Sampler","m_CubeTexture0_Sampler"}) {
                  auto* bound=state.shaders.at(state.linked_pixel).bindings->ReadTexture(name);
                  if(!bound) continue;
                  auto* view=edf::native::NativeD3D11TextureView(*bound);
                  auto* resource=edf::native::NativeD3D11TextureResource(*bound);
                  if(!view || !resource) continue; // A D3D11-only diagnostic.
                  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; view->GetDesc(&view_desc);
                  D3D11_TEXTURE2D_DESC desc{};
                  resource->GetDesc(&desc);
                  REXLOG_INFO("Native invalid RGB texture: name={}, view_format={}, resource_format={}, {}x{}, mips={}",
                    name,uint32_t(view_desc.Format),uint32_t(desc.Format),desc.Width,desc.Height,desc.MipLevels);
                }
                REXLOG_INFO("Native invalid RGB state: blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, write_mask={}, scissor={}",
                  key[0],key[1],key[2],key[3],key[4],key[5]);
                if(const auto found=state.render_states.find(key);
                   found!=state.render_states.end() && found->second.blend) {
                  D3D11_BLEND_DESC blend{};
                  found->second.blend->GetDesc(&blend);
                  const auto& target=blend.RenderTarget[0];
                  REXLOG_INFO("Native invalid RGB blend: enabled={}, src={}, dest={}, op={}, src_alpha={}, dest_alpha={}, alpha_op={}, mask={}, needs_factor={}, replicate_alpha={} (D3D11_BLEND_OP: 1=ADD 2=SUBTRACT 3=REV_SUBTRACT 4=MIN 5=MAX)",
                    target.BlendEnable!=FALSE,uint32_t(target.SrcBlend),uint32_t(target.DestBlend),
                    uint32_t(target.BlendOp),uint32_t(target.SrcBlendAlpha),uint32_t(target.DestBlendAlpha),
                    uint32_t(target.BlendOpAlpha),uint32_t(target.RenderTargetWriteMask),
                    found->second.requires_blend_factor,found->second.replicate_blend_alpha);
                }
                for(const auto* stage:{&bindings,state.shaders.at(state.linked_pixel).bindings.get()}) {
                  const auto* stage_name=stage->shader().entry.pixel?"PS":"VS";
                  for(const auto* name:{"m_MaterialDiffuse","m_MaterialSpecularColor","m_MaterialSpecularPower",
                      "m_MaterialBumpHeight","m_RefrectionRate","g_LightVector","g_LightDiffuse",
                      "g_LightSpecular","g_LightAmbient","g_HemiSphereVector","g_HemiSphereColor1",
                      "g_HemiSphereColor2","g_FogParam","g_FogColor"}) {
                    const auto values=stage->ReadFloatVector(name);
                    for(size_t lane=0;lane<values.size();++lane)
                      REXLOG_INFO("Native invalid RGB constant: stage={} name={} lane={} value={}",stage_name,name,lane,values[lane]);
                  }
                  for(const auto* name:{"g_mWorld","g_mView","g_mProjection","g_mViewTranspose"}) {
                    if(const auto matrix=stage->ReadFloat4x4(name)) for(size_t row=0;row<4;++row)
                      REXLOG_INFO("Native invalid RGB matrix: stage={} name={} row={} values={},{},{},{}",stage_name,name,row,
                        (*matrix)[row*4],(*matrix)[row*4+1],(*matrix)[row*4+2],(*matrix)[row*4+3]);
                  }
                }
                REXLOG_INFO("Native invalid RGB mesh: declaration={:#x} stride={} first={} base={} count={}",
                  decl,stream.stride,ctx.r6.u32,ctx.r5.s32,ctx.r7.u32);
                const auto declaration_word=[&](size_t at) {
                  uint32_t value=0;
                  for(size_t lane=0;lane<4;++lane) value=(value<<8)|declaration_bytes[at+lane];
                  return value;
                };
                for(uint32_t element=0;element<elements;++element) {
                  const auto at=element*12,offset=declaration_word(at),type=declaration_word(at+4);
                  const auto semantic=declaration_word(at+8);
                  REXLOG_INFO("Native invalid RGB attribute: offset={} type={:#x} semantic={:#x}",offset,type,semantic);
                  const auto usage=(semantic>>16)&255,index=(semantic>>8)&255;
                  if(type!=0x2a23b9 || (usage!=3 && usage!=6) || index!=0) continue;
                  if(offset>stream.stride || stream.stride-offset<12) continue;
                  uint32_t zero=0,invalid=0,samples=(std::min)(ctx.r7.u32,65536u);
                  const auto values=mesh.CaptureSourceFloat3(ctx.r6.u32,samples,ctx.r5.s32,offset);
                  for(uint32_t i=0;i<samples;++i) {
                    const auto& value=values[i];
                    if(value[0]==0 && value[1]==0 && value[2]==0) ++zero;
                    if(!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2])) ++invalid;
                    if(i<3) REXLOG_INFO("Native invalid RGB attribute sample: usage={} index_position={} xyz={},{},{} (native draw snapshot)",
                      usage,ctx.r6.u32+i,value[0],value[1],value[2]);
                  }
                  REXLOG_INFO("Native invalid RGB attribute summary: usage={} indexed_samples={} zero={} nonfinite={}",
                    usage,samples,zero,invalid);
                }
              }
              if(state.color_probe_draws>=uint64_t(std::clamp(REXCVAR_GET(edf_native_probe_draw_limit),1,65536))) {
                state.color_probe_done=true;
                REXLOG_INFO("Native invalid RGB probe exhausted: draws={}, next_output_candidate={}",state.color_probe_draws,state.indexed_output_frames+1);
              }
            } catch(const std::exception& error) {
              state.color_probe_done=true;
              REXLOG_ERROR("Native invalid RGB probe: {}",error.what());
            }
          }
          state.scenes.at(state.active_scene).frame_complete=false;
        }
        if (state.indexed_uploads<=20 || state.indexed_submitted%1000==0) {
          REXLOG_INFO("Native indexed upload: uploaded={}, errors={}, submitted={}, reversed_depth={}, mesh_builds={}, mesh_hits={}, mesh_bytes={}, mesh_entries={}, entry_evictions={}, budget_evictions={}, vertex_mismatches={}, index_mismatches={}",
            state.indexed_uploads,state.indexed_errors,state.indexed_submitted,viewport.reverse_depth,
            state.meshes.builds(),state.meshes.hits(),state.meshes.bytes(),state.meshes.entries(),
            state.meshes.entry_evictions(),state.meshes.budget_evictions(),state.meshes.vertex_mismatches(),state.meshes.index_mismatches());
          if(state.indexed_submitted%100000==0) {
            REXLOG_INFO("Native prepared geometry: hits={} (guarded unchanged snapshots, matching storage and validated range)",prepared_geometry_hits);
            const auto& checks=state.meshes.source_checks();
            REXLOG_INFO("Native mesh source checks: vertex_checks={}, index_checks={}, vertex_candidate_bytes={}, index_candidate_bytes={} (cache-hit checks; not measured memory traffic)",
              checks.vertex_checks,checks.index_checks,checks.vertex_candidate_bytes,checks.index_candidate_bytes);
            const auto& spend=state.meshes.spend();
            if(REXCVAR_GET(edf_native_hook_timings)) REXLOG_INFO("Native mesh acquire spend: calls={}, prologue_ns_avg={}, lookup_ns_avg={}, tail_ns_avg={}, owned_hits={} (a cache hit is prologue+lookup; the tail is construction)",
              spend.calls,spend.calls?spend.prologue_ns/spend.calls:0,
              spend.calls?spend.lookup_ns/spend.calls:0,spend.calls?spend.tail_ns/spend.calls:0,state.meshes.owned_hits());
            REXLOG_INFO("Native published index consumption: reused_builds={}, rejected_generations={} (mesh hits excluded)",
              state.meshes.published_index_reuses(),state.meshes.published_index_rejections());
            size_t vertices=0,vertex_bytes=0,indices=0,index_bytes=0;
            state.model_buffers.VisitVertexStorage([&](uint32_t,const edf::native::NativeVertexBuffer& storage) {
              ++vertices; vertex_bytes+=storage.StorageBytes();
            });
            state.model_buffers.VisitIndexStorage([&](uint32_t,const edf::native::NativeIndexBuffer& storage) {
              ++indices; index_bytes+=storage.StorageBytes();
            });
            REXLOG_INFO("Native model storage: vertices={}, vertex_bytes={}, indices={}, index_bytes={}, vertex_reused_builds={}, vertex_replaced_builds={} (CPU/GPU payload shared with meshes; cache hits excluded)",
              vertices,vertex_bytes,indices,index_bytes,state.meshes.retained_vertex_reuses(),state.meshes.retained_vertex_replacements());
          }
          const auto mismatches=state.meshes.vertex_mismatches()+state.meshes.index_mismatches();
          if(mismatches!=state.reported_mesh_mismatches) {
            state.reported_mesh_mismatches=mismatches;
            const auto& v=state.meshes.last_vertex_mismatch();
            const auto& i=state.meshes.last_index_mismatch();
            REXLOG_INFO("Native geometry mismatch identities: vertex_count={}, last_vertex=(VB={:#x}, IB={:#x}, declaration={:#x}, VS={:#x}, variant={}), index_count={}, last_index=(VB={:#x}, IB={:#x}, declaration={:#x}, VS={:#x}, variant={}) (last detected consumers, not writer PCs)",
              state.meshes.vertex_mismatches(),v[0],v[1],v[2],v[3],v[4],
              state.meshes.index_mismatches(),i[0],i[1],i[2],i[3],i[4]);
          }
        }
      } catch (const std::exception& error) {
        ++state.indexed_errors;
        if (state.indexed_errors<=10) REXLOG_ERROR("Native indexed upload: {} (VS={:#x}, PS={:#x}, caller={:#x})",
          error.what(),state.active_vertex,state.linked_pixel,uint32_t(ctx.lr));
      }
    }
    edf::native::HookTiming coverage_timing(edf::native::HookPhase::IndexedCoverage);
    if(!ctx.r7.u32) ++state.indexed_empty_requests;
    else if(native_submitted) {
      ++state.indexed_nonempty_submitted;
      // Opt-in: enumerating what the content exercises costs a set lookup on
      // every draw, and gameplay submits over a hundred thousand a second.
      if(REXCVAR_GET(edf_native_contract_coverage)) try {
        const edf::native::GuestReader reader(base);
        state.contracts.RecordSubmitted(edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          reader.Word(reader.Add(ctx.r3.u32,11536)),ctx.r4.u32,drawn_stride,drawn_index_width));
      } catch(const std::exception&) { /* Accounting must never fail a drawn frame. */ }
    }
    else {
      ++state.indexed_unsubmitted_requests;
      try {
        const edf::native::GuestReader reader(base);
        const auto declaration=reader.Word(reader.Add(ctx.r3.u32,11536));
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          declaration,ctx.r4.u32),"indexed draw not submitted natively");
      } catch(const std::exception&) {
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Indexed,state.active_vertex,state.linked_pixel,
          0,ctx.r4.u32),"indexed draw not submitted natively; device unreadable");
      }
      const std::array<uint32_t,5> signature{uint32_t(ctx.lr),ctx.r4.u32,state.active_vertex,state.linked_pixel,state.active_target};
      if(state.indexed_unsubmitted_paths.size()<64 && state.indexed_unsubmitted_paths.insert(signature).second) {
        REXLOG_INFO("Native indexed unsubmitted path: caller={:#x}, primitive={}, count={}, scene={:#x}, target={:#x}, active_VS={:#x}, active_PS={:#x}",
          uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,state.active_scene,state.active_target,state.active_vertex,state.linked_pixel);
        try {
          const edf::native::GuestReader reader(base);
          const auto pair=edf::native::ReadShaderPair(reader,ctx.r3.u32);
          REXLOG_INFO("Native indexed unsubmitted bindings: VS={:#x}, PS={:#x}, surface={:#x}, declaration={:#x}, index_buffer={:#x}",
            pair.vertex,pair.pixel,reader.Word(reader.Add(ctx.r3.u32,12168)),
            reader.Word(reader.Add(ctx.r3.u32,11536)),reader.Word(reader.Add(ctx.r3.u32,12164)));
        } catch(const std::exception& error) {
          REXLOG_INFO("Native indexed unsubmitted inspection: {}",error.what());
        }
      }
    }
    const auto now=std::chrono::steady_clock::now();
    if(state.indexed_draws>=1024 && now-state.indexed_coverage_reported>=std::chrono::seconds(10)) {
      state.indexed_coverage_reported=now;
      REXLOG_INFO("Native indexed coverage: requests={}, empty={}, submitted={}, unsubmitted={}, sampled_paths={} (64-path cap; not visual completeness)",
        state.indexed_draws,state.indexed_empty_requests,state.indexed_nonempty_submitted,
        state.indexed_unsubmitted_requests,state.indexed_unsubmitted_paths.size());
      const auto& coverage=state.contracts.counters();
      REXLOG_INFO("Native contract coverage: rejected_draws={}, distinct_rejected={}, omitted={}, errors={}, clean={}, submitted_contracts={} (clean requires zero rejected and zero omitted)",
        coverage.rejected,coverage.distinct_rejected,coverage.omitted_rejected,
        state.indexed_errors,state.contracts.clean(),coverage.distinct_submitted);
      if(const auto path=REXCVAR_GET(edf_native_contract_export);!path.empty()) {
        // Rewritten whole each time: a partial capture from a killed run is
        // still a valid catalog of everything seen up to that point.
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        const auto text=state.contracts.Export();
        file.write(text.data(),std::streamsize(text.size()));
        if(!file) REXLOG_ERROR("Native contract export failed: {}",path);
        else REXLOG_INFO("Native contract export: {}, declarations={}, bytes={}",
          path,state.contracts.declarations().size(),text.size());
      }
      for(const auto& rejection:state.contracts.Rejections())
        REXLOG_INFO("Native contract gap: path={}, VS_source={:#x}, PS_source={:#x}, declaration={:#x}, elements={}, topology={}, draws={}, reason={}",
          edf::native::NativeContractPathName(rejection.contract.path),rejection.contract.vertex_source,
          rejection.contract.pixel_source,rejection.contract.declaration,rejection.contract.elements,
          rejection.contract.topology,rejection.draws,rejection.reason);
    }
  }
  native_timing.Finish();
  edf::native::HookTiming guest_timing(edf::native::HookPhase::IndexedGuest);
  // The native CPU chain calls native implementations directly; it needs no
  // thread-local packet-routing scope. Mask inherited ownership only for legacy.
  const bool native_host=EDF_NATIVE_FLAG(host);
  std::optional<edf::native::NativeConstantOwnership> legacy_scope;
  if(!native_host) legacy_scope.emplace(0);
  if(native_host) {
    // Generated from the unchanged retail CPU-state prefix and original ABI
    // epilogue. Native submission replaces the subsequent inline GPU draw loop.
    // Native-host mode has no GPU emulator to consume retail draw packets.
    // Unsupported, empty and failed draws retain CPU updates too, without
    // rereading the index header. Missing
    // native submissions remain explicit coverage failures, not fallback draws.
    __imp__edf_native_indexed_cpu_tail(ctx,base);
    static thread_local uint64_t omitted=0;
    if(++omitted<=3) REXLOG_INFO("Native indexed routing: retained CPU state; omitted Xbox draw packets (submitted={}, geometry_rejected={})",
      native_submitted,native_geometry_rejected);
  } else __imp__sub_821FE358(ctx,base);
}
namespace edf::native {
namespace {
// A Utility 3D / Vs_Particle list or strip that has passed its contract checks.
struct NativeSceneImmediateDraw {
  ShaderBindings& vertex;
  ShaderBindings& pixel;
  const NativeViewportState& viewport;
  const RenderStateWords& state;
  GuestShaderPair shaders{};
  uint32_t declaration=0,element_count=0;
  std::shared_ptr<const NativeDeclaration> owned_declaration;
  uint32_t primitive=13,stride=0;
  // edf_native_effect_mesh_buckets: the mesh is one built for the count's
  // power-of-two bucket, drawn over these vertices (DrawTransientPrefix).
  // Recorded draws only; the direct path keys the exact count.
  bool bucketed=false;
};
// Zero bytes a bucketed immediate mesh is built from (its cached vertices are
// never drawn: a recorded draw stages its own, DrawTransientPrefix).
std::span<const uint8_t> NativeImmediateBucketBytes(size_t bytes) {
  static thread_local std::vector<uint8_t> zeros;
  if(zeros.size()<bytes) zeros.resize(bytes);
  return {zeros.data(),bytes};
}
// Records the draw from vertices held in HOST memory, laid out as the guest
// lays them out (big-endian words in declaration order): the DrawPrimitiveUP
// hook passes the bytes it read at r6, the full-frame effect pass passes what
// EncodeNativeEffectVertices built (native_full_frame_effects.h). The vertices
// are never read from the guest here; reader and device serve only the blend
// factor RecordDrawSetup reads and the direct path's render-state bind.
template<class Reader>
void RecordNativeSceneImmediate(Bridge& state,const Reader& reader,uint32_t device,
                                const NativeSceneImmediateDraw& draw,std::span<const uint8_t> vertices) {
  const bool strip=draw.primitive==6;
  if(!draw.stride || vertices.size()%draw.stride) throw std::runtime_error("native immediate vertices are not whole vertices");
  const auto count=uint32_t(vertices.size()/draw.stride);
  if(count<3 || count>16384 || (!strip && count%4)) throw std::runtime_error("unsupported native immediate vertex count");
  // A bucketed draw's mesh is keyed and built for the bucket's count: the
  // generated quad and strip indices over `count` vertices are the first
  // index_count of the bucket's, so it draws the same primitives from the same
  // vertices. One mesh then serves every count in the bucket, where a key per
  // exact count built a mesh (and its GPU buffers) for each new particle count.
  const bool bucketed=draw.bucketed && EDF_NATIVE_FLAG(seam_draws);
  const uint32_t mesh_count=bucketed?std::max<uint32_t>(64,std::bit_ceil(count)):count;
  const uint32_t index_count=strip?(count-2)*3:count/4*6;
  const auto owned_indices=state.generated_indices.Get(strip?NativeIndexPattern::Strip:NativeIndexPattern::Quads,mesh_count);
  const auto indices=owned_indices->bytes();
  const auto mesh_vertices=bucketed?NativeImmediateBucketBytes(size_t(mesh_count)*draw.stride):vertices;
  HookTiming acquire_timing(HookPhase::ImmediateAcquire);
  auto& mesh=state.immediate_meshes.Acquire(EnsureSceneBackendLocked(state),draw.vertex.shader(),
    ImmediateStreamKey(mesh_vertices.size(),draw.declaration,draw.shaders.vertex,draw.primitive,draw.viewport.reverse_depth),
    {draw.owned_declaration->bytes().data(),draw.element_count*12},draw.stride,
    mesh_vertices,indices,2,draw.owned_declaration,owned_indices,{},{},{},{},0,{},
    // Where this mesh's dynamic vertices are rewritten when the draw
    // is recorded; the immediate context does it otherwise.
    EDF_NATIVE_FLAG(seam_draws)?&SceneRecorderLocked(state):nullptr);
  acquire_timing.Finish();
  auto render=state.render_states.find(draw.state);
  if(render==state.render_states.end()) render=state.render_states.emplace(draw.state,
    CreateNativeRenderState(state.device.Get(),draw.state)).first;
  if(EDF_NATIVE_FLAG(seam_draws)) {
    HookTiming record_timing(HookPhase::ImmediateRecord);
    auto& recorder=RecordDrawSetup(state,reader,device,{
      draw.vertex,draw.pixel,draw.viewport,draw.state,
      mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
      (uint64_t(draw.shaders.vertex)<<1)|uint64_t(draw.viewport.reverse_depth?1:0),draw.shaders.pixel,
      NativeBackendTopology::TriangleList});
    if(bucketed) mesh.DrawTransientPrefix(recorder,vertices,0,index_count);
    else mesh.DrawTransient(recorder,vertices,0,index_count);
  } else {
    BindActiveTarget(state);
    BindGuestRenderState(render->second,*state.context.Get(),reader,device,&state.bind_generation);
    draw.viewport.Bind(*state.context.Get());
    draw.vertex.Bind(*state.context.Get()); draw.pixel.Bind(*state.context.Get());
    mesh.Draw(*state.context.Get(),0,index_count);
  }
}
}
// Declaration element words as the guest stores them (big-endian).
template<size_t N>
std::array<uint8_t,N*4> NativeFullFrameDeclarationBytes(const std::array<uint32_t,N>& words) {
  std::array<uint8_t,N*4> bytes{};
  for(size_t i=0;i<N;++i) for(size_t byte=0;byte<4;++byte) bytes[i*4+byte]=uint8_t(words[i]>>(24-byte*8));
  return bytes;
}
// One effect draw of the full frame (native_full_frame_effects.h), recorded
// without guest calls. As the producers do: 821BC4C8 stores the draw's
// texture into the technique's sampler list (BindNativeEffectTexture, the
// guest's own write), then the material 821B94E8 activates, [technique+16]
// (NativeEffectTechniqueMaterial; the technique object itself is not a pass
// record), has its program from the model pass cache - rebuilt when the
// stored texture changed it - applied to the shader bindings as a published
// activation is (NativeSceneMaterialProgram::ApplyBindings), with the pass
// camera over its camera globals. Render state: the shared full-frame base
// state, then for the ribbons the draw's blend (0x48/0x4c) and depth write
// (0x30) and the technique's own operations over them, for the particles the
// technique's operations and the blend over them (state_before_activation).
// The vertices are the host bytes EncodeNativeEffectVertices builds, one
// RecordNativeSceneImmediate per guest DrawPrimitiveUP, under the Vs_Particle
// (44-byte) or VS_3DTex (36-byte) declaration the immediate path accepts.
// Throws when any of it is missing.
//
// Split in two: the activation (everything up to the render state, done once
// for a run of draws that share it, see RecordNativeFullFrameEffectsLocked)
// and the draw's DrawPrimitiveUP calls under it. The activation leaves the
// bound shader pair (the registered shaders' own bindings, holding what
// ApplyBindings set until another activation), the resolved render state and
// the immediate declaration.
struct NativeFullFrameEffectActivation {
  ShaderBindings* vertex=nullptr;
  ShaderBindings* pixel=nullptr;
  std::shared_ptr<const NativeSceneGroupMaterial> material;
  RenderStateWords render{};
  GuestShaderPair shaders{};
  const std::shared_ptr<const NativeDeclaration>* declaration=nullptr;
  uint32_t declaration_id=0,device=0;
};
NativeFullFrameEffectActivation ActivateNativeFullFrameEffectLocked(Bridge& state,const GuestReader& reader,
    const NativeSceneCpuWindow<GuestReader>& window,const NativeEffectDraw& draw,const NativeScenePassCamera& camera,
    const NativeViewportState& viewport,const NativeFullFramePassTargets& formats) {
  HookTiming timing(HookPhase::FrameNativeEffectActivate);
  NativeFullFrameEffectActivation result;
  if(draw.texture) {
    const auto texture=state.textures.find(draw.texture);
    if(texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend)
      throw std::runtime_error("native effect texture is not decoded");
  }
  BindNativeEffectTexture(window,draw.effect,draw.technique,draw.texture);
  const auto pass=NativeEffectTechniqueMaterial(window,draw.effect,draw.technique);
  if(!pass) throw std::runtime_error("native effect technique has no material");
  // The provider's reason travels with the draw's one failure (the caller's
  // `failed`), not as a second decline of the same draw.
  std::string provider;
  result.material=NativeModelPassProgramLocked(state,window,pass,false,[&](const std::string& reason) { provider=reason; });
  const auto& material=result.material;
  if(!material || !material->program)
    throw std::runtime_error(provider.empty()?"native effect technique has no program":provider);
  const auto& program=*material->program;
  if(draw.texture && std::none_of(program.inputs.textures.begin(),program.inputs.textures.end(),
       [&](const auto& texture) { return texture.handle==draw.texture; }))
    throw std::runtime_error("native effect texture is not sampled by its technique");
  const auto vertex=state.shaders.find(program.inputs.vertex),pixel=state.shaders.find(program.inputs.pixel);
  if(vertex==state.shaders.end() || pixel==state.shaders.end() || !vertex->second.bindings || !pixel->second.bindings)
    throw std::runtime_error("native effect shaders are not registered");
  auto& vs=VertexBindingsForDraw(vertex->second,viewport.reverse_depth);
  auto& ps=*pixel->second.bindings;
  const std::pair<uint32_t,uint32_t> link{program.inputs.vertex,program.inputs.pixel};
  if(!state.validated_links.contains(link)) {
    ValidateNativeShaderLink(vs.shader(),ps.shader());
    state.validated_links.insert(link);
  }
  auto constants=material->constants;
  for(auto& constant:constants) camera.Apply(constant);
  const auto base=NativeFullFrameBaseState(formats);
  const auto resolved=program.ResolveSamplers(base.samplers);
  std::vector<NativeBackendSampler*> samplers;
  for(const auto& texture:program.inputs.textures) {
    if(texture.slot>=resolved.size()) throw std::runtime_error("invalid native effect sampler slot");
    // The effects are scene materials: FSR upscaling's mip bias applies.
    const auto key=NativeFilteringKey(resolved[texture.slot].words,NativeSceneMaterialFiltering());
    auto cached=state.samplers.find(key);
    if(cached==state.samplers.end())
      cached=state.samplers.emplace(key,&EnsureSceneBackendLocked(state).CreateSampler(DecodeNativeGuestSampler(key))).first;
    samplers.push_back(cached->second);
  }
  state.active_vertex=0;
  state.active_vertex_parameters.reset();
  program.ApplyBindings(vs,ps,constants,samplers);
  state.linked_vertex=program.inputs.vertex; state.linked_pixel=program.inputs.pixel;
  const auto draw_state=[&](NativeMaterialRenderPass pass) {
    if(draw.blend==kNativeEffectBlendAlpha) { ApplyNativeMaterialState(pass,0x48,6); ApplyNativeMaterialState(pass,0x4c,7); }
    else if(draw.blend==kNativeEffectBlendAdditive) { ApplyNativeMaterialState(pass,0x48,1); ApplyNativeMaterialState(pass,0x4c,1); }
    if(draw.sets_depth_write) ApplyNativeMaterialState(pass,0x30,draw.depth_write?1:0);
    return pass;
  };
  const auto render=draw.state_before_activation()?program.ResolveRenderState(draw_state(base.render)):
                                                   draw_state(program.ResolveRenderState(base.render));
  DecodeNativeRenderState(render.words);
  // The immediate path's accepted layouts (see the 821FD8F8 hook's checks):
  // Vs_Particle, VS_3DTex and, for 821A7B58's strips, VS_3D (position and a
  // D3DCOLOR at +12, the "solid" Utility 3D layout).
  const bool particle=draw.kind==NativeEffectDraw::Kind::Particles;
  const bool solid=draw.kind==NativeEffectDraw::Kind::ColourStrip;
  static const auto particle_declaration=NativeDeclaration::Create(NativeFullFrameDeclarationBytes<12>({
    0,0x2a23b9,0, 12,0x2c23a5,0x50000, 20,0x2c23a5,0x50100, 28,0x1a23a6,0xa0000}));
  static const auto ribbon_declaration=NativeDeclaration::Create(NativeFullFrameDeclarationBytes<9>({
    0,0x2a23b9,0, 12,0x2c23a5,0x50000, 20,0x1a23a6,0xa0000}));
  static const auto solid_declaration=NativeDeclaration::Create(NativeFullFrameDeclarationBytes<6>({
    0,0x2a23b9,0, 12,0x182886,0xa0000}));
  result.declaration=particle?&particle_declaration:solid?&solid_declaration:&ribbon_declaration;
  // Synthetic declaration identities: only the immediate mesh cache keys on them.
  result.declaration_id=particle?0xFFFFFF01u:solid?0xFFFFFF03u:0xFFFFFF02u;
  result.device=reader.Word(reader.Add(reader.Word(0x8257bfb4),8));
  result.vertex=&vs; result.pixel=&ps;
  result.render=render.words;
  result.shaders=GuestShaderPair{.pixel=program.inputs.pixel,.vertex=program.inputs.vertex};
  return result;
}
// The draw's DrawPrimitiveUP calls under its activation.
void RecordNativeFullFrameEffectCallsLocked(Bridge& state,const GuestReader& reader,
    const NativeFullFrameEffectActivation& activation,const NativeEffectDraw& draw,const NativeViewportState& viewport) {
  const auto& declaration=*activation.declaration;
  const auto calls=NativeEffectDrawCalls(draw);
  // Encoded off the locks when the pass did (EncodeNativeEffectDrawCalls), the
  // same bytes; encoded here otherwise.
  const bool encoded=!draw.encoded_calls.empty();
  if(encoded && draw.encoded_calls.size()!=calls.size()) throw std::runtime_error("native effect encoded calls do not match the draw");
  for(size_t call=0;call<calls.size();++call) {
    const auto [first,count]=calls[call];
    std::vector<uint8_t> local;
    if(!encoded) local=EncodeNativeEffectVertices(draw,first,count);
    const std::span<const uint8_t> bytes=encoded?std::span<const uint8_t>(draw.encoded_calls[call]):std::span<const uint8_t>(local);
    RecordNativeSceneImmediate(state,reader,activation.device,{*activation.vertex,*activation.pixel,viewport,activation.render,
      activation.shaders,activation.declaration_id,declaration->count(),declaration,draw.primitive(),draw.stride(),
      REXCVAR_GET(edf_native_effect_mesh_buckets)},bytes);
  }
}
struct NativeFullFrameEffectCarry::Held {
  std::optional<NativeFullFrameEffectActivation> activation;
  const NativeEffectDraw* activated=nullptr;
};
NativeFullFrameEffectCarry::NativeFullFrameEffectCarry():held_(std::make_unique<Held>()) {}
NativeFullFrameEffectCarry::~NativeFullFrameEffectCarry()=default;
void NativeFullFrameEffectCarry::Reset() { held_->activation.reset(); held_->activated=nullptr; }
// A run of adjacent draws that NativeEffectDrawsShareActivation is activated
// once: the activation reads only the fields that predicate compares (never
// the vertices), so the next draw's would bind the same texture word, program,
// constants, samplers and render state onto the same bindings, which only an
// activation changes (the immediate recording reads them). A failed draw
// drops the activation and goes to `failed` once, with its reason; the next
// draw activates again, as it would alone. With a carry the run continues
// from the previous call's last activation (NativeFullFrameEffectCarry); the
// pass camera, viewport and formats are the pass's own, the same in every
// call of one hold.
uint64_t RecordNativeFullFrameEffectsLocked(Bridge& state,const GuestReader& reader,const NativeSceneCpuWindow<GuestReader>& window,
    std::span<const NativeEffectDraw> draws,const NativeScenePassCamera& camera,const NativeViewportState& viewport,
    const NativeFullFramePassTargets& formats,const std::function<void(const std::exception&)>& failed,
    NativeFullFrameEffectCarry* carry) {
  uint64_t recorded=0;
  NativeFullFrameEffectCarry::Held local;
  auto& held=carry?carry->held():local;
  auto& activation=held.activation;
  for(const auto& draw:draws) {
    try {
      // Reuse off (native_reuse.h): every draw activates on its own.
      if(!activation || !NativeReuseAllowed() || !NativeEffectDrawsShareActivation(*held.activated,draw)) {
        activation.reset(); held.activated=nullptr;
        activation=ActivateNativeFullFrameEffectLocked(state,reader,window,draw,camera,viewport,formats);
        held.activated=&draw;
        if(carry) ++carry->activations;
      } else if(carry) ++carry->shared;
      RecordNativeFullFrameEffectCallsLocked(state,reader,*activation,draw,viewport);
      ++recorded;
    } catch(const std::exception& error) { activation.reset(); held.activated=nullptr; failed(error); }
  }
  return recorded;
}
}
REX_EXTERN(__imp__sub_821FD8F8);
REX_EXTERN(__imp__edf_native_immediate_cpu_tail);
REX_HOOK_RAW(sub_821FD8F8) {
  edf::native::HookTiming native_timing(edf::native::HookPhase::ImmediateNative);
  bool native_submitted=false;
  if (EDF_NATIVE_FLAG(shader_bridge)) {
    auto& state = edf::native::State();
    edf::native::HookTiming submission_wait(edf::native::HookPhase::ImmediateSubmissionWait);
    std::lock_guard submission(state.submissions);
    submission_wait.Finish();
    edf::native::HookTiming context_wait(edf::native::HookPhase::ImmediateContextWait);
    std::lock_guard lock(state.mutex);
    context_wait.Finish();
    edf::native::HookTiming classify_timing(edf::native::HookPhase::ImmediateClassify);
    bool movie_draw=false;
    // Setters publish identity independently of material activation. Retain a
    // per-invocation copy while this hook holds the submission/state locks.
    std::optional<edf::native::GuestShaderPair> draw_shaders;
    auto bound_shaders=[&]() -> const edf::native::GuestShaderPair& {
      if(!draw_shaders)
        draw_shaders=state.shader_bindings.Pair(ctx.r3.u32);
      return *draw_shaders;
    };
    // What the pair's identities admit, memoized per pair against the two
    // registry generations (native_immediate_classify.h): the same answer the
    // lookups below each path used to make per draw. Throws as bound_shaders
    // does, inside each path's own handler.
    const decltype(state.immediate_pairs)::Entry* pair_entry=nullptr;
    auto pair_class=[&]() -> const decltype(state.immediate_pairs)::Entry& {
      if(!pair_entry) pair_entry=&edf::native::ClassifyImmediatePairLocked(state,bound_shaders());
      return *pair_entry;
    };
    using PairClass=edf::native::NativeImmediatePairClass;
    if (!state.active_target && state.scenes.contains(state.active_output)) try {
      const auto& pair=bound_shaders();
      const auto movie_kind=pair_class().kind.movie;
      movie_draw=movie_kind!=PairClass::Movie::None;
      const bool movie_sd=movie_kind==PairClass::Movie::Sd;
      if (movie_draw) {
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        auto word=[&](uint32_t offset) {return reader.Word(reader.Add(ctx.r3.u32,offset));};
        auto& scene=state.scenes.at(state.active_output);
        if (word(12168)!=scene.output_surface || ctx.r4.u32!=5 || ctx.r5.u32!=4 || ctx.r7.u32!=16)
          throw std::runtime_error("unsupported movie output/topology");
        const auto declaration=state.declarations.Get(state.declaration_bindings.at(ctx.r3.u32));
        if (declaration->count()!=2 ||
            declaration->Words<6>()!=std::array<uint32_t,6>{0,0x2c23a5,0,8,0x2c23a5,0x50000})
          throw std::runtime_error("unsupported movie vertex declaration");
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        if (viewport.reverse_depth) throw std::runtime_error("unimplemented reversed movie projection");
        if (!state.movie_vertex) {
          const auto effect=edf::native::MakeNativeMovieEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_movie.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[1],"native_movie.fx"));
          auto pixel_sd=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[2],"native_movie.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel_sd->shader());
          auto vertices=std::make_unique<edf::native::QuadStream>(state.device.Get(),vertex->shader());
          state.movie_bindings[0].emplace(*vertex,*pixel);
          state.movie_bindings[1].emplace(*vertex,*pixel_sd);
          state.movie_vertex=std::move(vertex); state.movie_pixel=std::move(pixel);
          state.movie_pixel_sd=std::move(pixel_sd); state.movie_vertices=std::move(vertices);
        }
        // Choose the program actually bound by the game, not a heuristic based
        // on the output resolution (SD movies may fill a 1280x720 target).
        auto& movie_pixel=movie_sd ? *state.movie_pixel_sd : *state.movie_pixel;
        // CPU constant setters 82149248/82149358 copy float4 rows to
        // device+(112+register)*16 / device+(368+register)*16 respectively.
        auto registers=[&](uint32_t offset,size_t bytes) {return std::span<const uint8_t>{reader.Bytes(reader.Add(ctx.r3.u32,offset),bytes),bytes};};
        const auto& movie_plan=*state.movie_bindings[movie_sd?1:0];
        // At any size but 1280x720 a full-screen (or canvas-placed) movie is
        // fitted into the centred 16:9 rectangle, the whole target on 16:9
        // (native_canvas_constants.h), moved onto host pixel edges by the
        // guest's half-pixel convention.
        const float movie_center=REXCVAR_GET(edf_native_pixel_centers)?
          edf::native::GuestPixelCenterOffset(edf::native::ReadVertexCenterWord(reader,ctx.r3.u32)):0.0f;
        const auto movie_layout=edf::native::MapNativeMovieRegisters(registers(1792,160),
          {reader.Bytes(ctx.r6.u32,64),64},scene.output.sampled.width,scene.output.sampled.height,movie_center);
        if(movie_layout.framing!=edf::native::NativeMovieFraming::Unchanged && state.movie_draws<3)
          REXLOG_INFO("Native movie framing: {} into the 16:9 area of {}x{} (pixel centre {})",
            movie_layout.framing==edf::native::NativeMovieFraming::FullTarget?"full-target quad":"canvas quad",
            scene.output.sampled.width,scene.output.sampled.height,movie_center);
        movie_plan.SetConstants(*state.movie_vertex,movie_pixel,movie_layout.registers,registers(5888,16));
        movie_pixel.ClearTextures(); movie_pixel.ClearSamplers();
        for(uint32_t i=0;i<3;++i) {
          // 8213BA98 stores the raw SetTexture handle at (3068+slot)*4.
          const auto handle=word(12272+i*4);
          const auto texture=state.textures.find(handle);
          const auto creation=state.texture_creations.find(handle);
          if (texture==state.textures.end() || !texture->second.content_valid ||
              creation==state.texture_creations.end() || creation->second.format!=0x28000002)
            throw std::runtime_error("movie draw missing decoded native plane");
          movie_plan.SetTexture(movie_pixel,i,texture->second.backend);
          const auto offset=1024+i*24;
          const auto key=edf::native::SamplerStateKey({word(offset),word(offset+12),word(offset+16),word(offset+20)});
          auto cached=state.samplers.find(key);
          if(cached==state.samplers.end()) {
            const auto desc=edf::native::DecodeNativeGuestSampler(key);
            cached=state.samplers.emplace(key,&edf::native::EnsureSceneBackendLocked(state).CreateSampler(desc)).first;
          }
          movie_plan.SetSampler(movie_pixel,i,cached->second);
        }
        const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
        if ((key[1]&3)!=0) throw std::runtime_error("movie requires an unbound depth/stencil surface");
        auto render=state.render_states.find(key);
        if(render==state.render_states.end()) render=state.render_states.emplace(key,edf::native::CreateNativeRenderState(state.device.Get(),key)).first;
        if(EDF_NATIVE_FLAG(seam_draws)) {
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            *state.movie_vertex,movie_pixel,viewport,key,
            edf::native::QuadStream::Layout(),edf::native::kNativeQuadLayoutId,
            // The reversed-depth variant is a different compiled shader under the
            // same guest handle, so it belongs in the identity: a cache keyed on
            // the handle alone hands back the pipeline built from the other one.
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
            pair.pixel,edf::native::NativeBackendTopology::TriangleList});
          state.movie_vertices->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,
                                     {reader.Bytes(ctx.r6.u32,64),64});
        } else {
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          state.movie_vertex->Bind(*state.context.Get()); movie_pixel.Bind(*state.context.Get());
          state.movie_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,64),64});
        }
        native_submitted=true;
        scene.frame_complete=false;
        ++state.movie_draws;
        state.movie_pacing_active.store(true,std::memory_order_relaxed);
        if(REXCVAR_GET(edf_native_publish_frames) && scene.output.content_valid) {
          if(scene.output.surface)
            state.presentation_frames->Publish(*scene.output.surface.Get(),edf::native::NativeFrameKind::Movie,
              state.display_gamma?&*state.display_gamma:nullptr);
          else {
            edf::native::PublishSceneSharedLocked(state,scene.output,edf::native::NativeFrameKind::Movie);
            edf::native::SubmitSceneFrameLocked(state);
          }
        }
        if(state.movie_draws<=3 || state.movie_draws==30 || state.movie_draws==60 || state.movie_draws==120) {
          REXLOG_INFO("Native movie draw: submitted={}, PS={}, output_initialized={}, frame_complete=false",
            state.movie_draws,movie_pixel.shader().entry.name,scene.output.content_valid);
          const auto prefix=REXCVAR_GET(edf_native_scene_capture);
          if(!prefix.empty() && scene.output.content_valid) {
            const auto path=std::filesystem::path(prefix+".movie."+std::to_string(state.movie_draws)+".bmp");
            if(std::filesystem::exists(path)) throw std::runtime_error("native movie capture path already exists");
            const auto bytes=edf::native::CaptureOutputBmp(state,scene);
            std::ofstream output(path,std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            output.close();
            if(!output) throw std::runtime_error("native movie capture write failed");
          }
        }
      }
    } catch(const std::exception& error) {
      if(++state.movie_draw_errors<=10) REXLOG_ERROR("Native movie draw: {}",error.what());
    }
    bool xui_draw=false;
    const auto xui_scene_owner=state.active_scene?state.active_scene:state.active_output;
    if(!movie_draw && !state.active_target && state.scenes.contains(xui_scene_owner)) try {
      const auto& pair=bound_shaders();
      const auto xui_kind=pair_class().kind.xui;
      xui_draw=xui_kind!=PairClass::Xui::None;
      if(xui_draw) {
        const edf::native::GuestReader backing(base);
        const auto device=ctx.r3.u32;
        const edf::native::GuestReadWindow reader(backing,backing.Add(device,1024),12416-1024);
        const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,device);
        auto& scene=state.scenes.at(xui_scene_owner);
        const bool scene_draw=state.active_scene!=0;
        const auto expected_surface=scene_draw?scene.color_surface:scene.output_surface;
        auto& target=scene_draw?scene.color:scene.output;
        if(!expected_surface || snapshot.surface!=expected_surface || ctx.r4.u32!=4 || ctx.r7.u32!=8 ||
           !ctx.r5.u32 || ctx.r5.u32%3 || ctx.r5.u32>16384)
          throw std::runtime_error("unsupported XUI textured output/topology");
        const auto declaration=state.declarations.Get(state.declaration_bindings.at(device));
        if(declaration->count()!=1 ||
           declaration->Words<3>()!=std::array<uint32_t,3>{0,0x2c23a5,0})
          throw std::runtime_error("unsupported XUI position declaration");
        auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
        if(edf::native::NativeRenderDimensions()[0]>0)
          viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(target.sampled.width)/1280,
            float(target.sampled.height)/720,false);
        const auto key=snapshot.render;
        if(!scene_draw && (viewport.reverse_depth || (key[1]&3)))
          throw std::runtime_error("unimplemented XUI depth contract");
        const bool solid=xui_kind==PairClass::Xui::Solid;
        const bool mask=xui_kind==PairClass::Xui::Mask;
        const auto texture=state.textures.find(snapshot.texture);
        if(!solid && (texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend))
          throw std::runtime_error("XUI textured brush has no native texture");
        if(!solid) {
          // Asked of the seam, not of a D3D11 view: a texture created on the
          // scene's own backend has no view to ask, and dereferencing one is a
          // null read rather than a refusal.
          if(texture->second.cube || !target.backend_surface ||
             texture->second.backend.get()==target.backend_surface->texture())
            throw std::runtime_error("unsupported or aliased XUI brush texture");
        }
        if(!state.xui_vertex) {
          const auto effect=edf::native::MakeNativeXuiTextureEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_xui.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[1],"native_xui.fx"));
          auto solid_pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[2],"native_xui.fx"));
          auto mask_pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[3],"native_xui.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),solid_pixel->shader());
          edf::native::ValidateNativeShaderLink(vertex->shader(),mask_pixel->shader());
          auto vertices=std::make_unique<edf::native::PositionTriangleStream>(state.device.Get(),vertex->shader());
          state.xui_vertex_bindings.emplace(*vertex);
          state.xui_pixel_bindings[0].emplace(*pixel,false);
          state.xui_pixel_bindings[1].emplace(*solid_pixel,true);
          state.xui_pixel_bindings[2].emplace(*mask_pixel,false);
          state.xui_vertex=std::move(vertex); state.xui_pixel=std::move(pixel); state.xui_vertices=std::move(vertices);
          state.xui_solid_pixel=std::move(solid_pixel); state.xui_mask_pixel=std::move(mask_pixel);
        }
        if(viewport.reverse_depth && !state.xui_reversed_vertex) {
          const auto effect=edf::native::MakeNativeXuiTextureEffect();
          auto reversed=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_xui.fx",true));
          state.xui_reversed_vertex_bindings.emplace(*reversed);
          state.xui_reversed_vertex=std::move(reversed);
        }
        auto& vertex=viewport.reverse_depth?*state.xui_reversed_vertex:*state.xui_vertex;
        auto& pixel=solid ? *state.xui_solid_pixel : mask ? *state.xui_mask_pixel : *state.xui_pixel;
        // Split into the three parts a batch would separate: converting guest
        // data, binding, and the draw itself. 2.87 million of these went
        // through here with no timing at all, so what they cost - and which
        // part of them is worth batching - was unknown.
        edf::native::HookTiming xui_total(edf::native::HookPhase::XuiNative);
        edf::native::HookTiming xui_decode(edf::native::HookPhase::XuiDecode);
        auto registers=[&](uint32_t offset,size_t bytes){
          return std::span<const uint8_t>{reader.Bytes(reader.Add(device,offset),bytes),bytes};};
        const auto vertex_registers=registers(1792,192);
        if(edf::native::NativeRenderDimensions()[0]>0 && state.xui_draws<5) {
          REXLOG_INFO("Native XUI resolution trace: viewport={},{} {}x{}, scissor={},{},{},{}, target={}x{}",
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom,
            target.sampled.width,target.sampled.height);
          for(uint32_t row=0;row<8;++row) {
            const auto offset=device+1792+row*16;
            REXLOG_INFO("Native XUI resolution matrix: row={}, values={},{},{},{}",row,
              std::bit_cast<float>(reader.Word(offset)),std::bit_cast<float>(reader.Word(offset+4)),
              std::bit_cast<float>(reader.Word(offset+8)),std::bit_cast<float>(reader.Word(offset+12)));
          }
        }
        const auto& vertex_plan=viewport.reverse_depth?*state.xui_reversed_vertex_bindings:*state.xui_vertex_bindings;
        const bool native_canvas=edf::native::NativeRenderDimensions()[0]>0;
        const float canvas_x=native_canvas?float(target.sampled.width)/1280.0f:1.0f;
        const float canvas_y=native_canvas?float(target.sampled.height)/720.0f:1.0f;
        // 2D canvas layout on an output that is not 16:9 (edf_hud_safe_area,
        // native_display_layout.h), per draw so a solid fade still covers the
        // frame; its scissor follows. XUI drawn into the HDR scene keeps the
        // legacy full-target mapping.
        edf::native::NativeClipAffine xui_layout;
        if(native_canvas && !scene_draw) {
          xui_layout=edf::native::NativeHudLayout(target.sampled.width,target.sampled.height);
          if(!xui_layout.identity()) {
            const size_t count=size_t(ctx.r5.u32)*8;
            xui_layout=edf::native::NativeCanvasDrawAffine(xui_layout,edf::native::NativeXuiClipBounds(vertex_registers,
              registers(5872,16),edf::native::NativeXuiCanvasProjection(vertex_registers.subspan(64,64),canvas_x,canvas_y),
              {reader.Bytes(ctx.r6.u32,count),count}),solid);
            viewport=edf::native::MapNativeCanvasViewportScissor(viewport,xui_layout,target.sampled.width,target.sampled.height);
          }
        }
        vertex_plan.SetConstants(vertex,vertex_registers,registers(5872,16),canvas_x,canvas_y,xui_layout);
        const auto& pixel_plan=*state.xui_pixel_bindings[solid?1:mask?2:0];
        pixel_plan.SetConstants(pixel,registers(5904,16),solid?registers(5888,16):std::span<const uint8_t>{});
        if(!solid) {
        pixel_plan.SetTexture(pixel,texture->second.backend);
        pixel_plan.SetSampler(pixel,edf::native::SamplerLocked(state,
          edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0))));
        }
        if(REXCVAR_GET(edf_native_batch_audit)) {
          // State a batch must share, and constants it would have to carry per
          // draw, hashed apart - so the answer says not just "could these
          // merge" but what a merge would have to do about their differences.
          const auto mix=[](uint64_t hash,uint64_t value) {
            hash^=value; return hash*1099511628211ull;
          };
          uint64_t shape=1469598103934665603ull;
          for(const auto word:key) shape=mix(shape,word);
          shape=mix(shape,uint64_t(solid?1:mask?2:0));
          shape=mix(shape,reinterpret_cast<uintptr_t>(texture==state.textures.end()?nullptr:texture->second.backend.get()));
          shape=mix(shape,uint64_t(viewport.reverse_depth));
          shape=mix(shape,uint64_t(viewport.viewport.Width)*8191+uint64_t(viewport.viewport.Height));
          uint64_t constants=1469598103934665603ull;
          for(const auto byte:vertex_registers) constants=mix(constants,byte);
          ++state.xui_batch_draws;
          if(shape==state.xui_last_state && state.xui_batch_draws>1) {
            ++state.xui_batch_run;
            ++state.xui_batch_collapsible;
            if(constants!=state.xui_last_constants) ++state.xui_constants_differ;
          } else {
            state.xui_batch_longest=(std::max)(state.xui_batch_longest,state.xui_batch_run);
            if(state.xui_batch_run) ++state.xui_batch_runs;
            state.xui_batch_run=1;
          }
          state.xui_last_state=shape;
          state.xui_last_constants=constants;
          // What differs from the draw before, part by part, and how often
          // only the vertex constants do: the draws transient batching appends
          // (nothing differs) and the ones only constants carried per vertex
          // could (only the vertex constants differ). The recorder also needs
          // equal scissor rectangles and samplers, which the shape above
          // leaves out, so they are counted here.
          uint64_t scissor=1469598103934665603ull;
          for(const auto value:{viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom})
            scissor=mix(scissor,uint64_t(uint32_t(value)));
          uint64_t pixel_constants=1469598103934665603ull;
          for(const auto byte:registers(5904,16)) pixel_constants=mix(pixel_constants,byte);
          if(solid) for(const auto byte:registers(5888,16)) pixel_constants=mix(pixel_constants,byte);
          uint64_t sampler=0;
          if(!solid) for(const auto word:edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0)))
            sampler=mix(sampler^1469598103934665603ull,word);
          uint64_t render_words=1469598103934665603ull;
          for(const auto word:key) render_words=mix(render_words,word);
          const std::array<uint64_t,9> parts{render_words,uint64_t(solid?1:mask?2:0),
            reinterpret_cast<uintptr_t>(texture==state.textures.end()?nullptr:texture->second.backend.get()),
            uint64_t(viewport.reverse_depth),uint64_t(viewport.viewport.Width)*8191+uint64_t(viewport.viewport.Height),
            scissor,sampler,pixel_constants,constants};
          if(state.xui_batch_draws>1) {
            bool state_differs=false;
            for(size_t part=0;part<parts.size();++part) if(parts[part]!=state.xui_audit_parts[part]) {
              ++state.xui_audit_breaks[part];
              if(part+1<parts.size()) state_differs=true;
            }
            if(!state_differs) {
              if(parts.back()==state.xui_audit_parts.back()) ++state.xui_audit_identical;
              else ++state.xui_audit_constants_only;
            }
          }
          state.xui_audit_parts=parts;
          if(state.xui_batch_draws%500000==0) {
            REXLOG_INFO("Native XUI batch audit: draws={}, runs={}, longest_run={}, collapsible={} ({:.1f}% share the state of the draw before), of those {} also change constants ({:.1f}%)",
              state.xui_batch_draws,state.xui_batch_runs,state.xui_batch_longest,
              state.xui_batch_collapsible,
              100.0*double(state.xui_batch_collapsible)/double(state.xui_batch_draws),
              state.xui_constants_differ,
              state.xui_batch_collapsible?100.0*double(state.xui_constants_differ)/double(state.xui_batch_collapsible):0.0);
            const auto& breaks=state.xui_audit_breaks;
            REXLOG_INFO("Native XUI batch breaks: draws={}, identical_to_previous={} (appendable), only_vertex_constants_differ={}, differs: render_state={}, pixel_shader={}, texture={}, reverse_depth={}, viewport={}, scissor={}, sampler={}, pixel_constants={}, vertex_constants={}",
              state.xui_batch_draws,state.xui_audit_identical,state.xui_audit_constants_only,
              breaks[0],breaks[1],breaks[2],breaks[3],breaks[4],breaks[5],breaks[6],breaks[7],breaks[8]);
          }
        }
        xui_decode.Finish();
        edf::native::HookTiming xui_bind(edf::native::HookPhase::XuiBind);
        auto& render=edf::native::RenderStateLocked(state,key);
        const bool xui_seam=EDF_NATIVE_FLAG(seam_draws);
        if(!xui_seam) {
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          vertex.Bind(*state.context.Get()); pixel.Bind(*state.context.Get());
        }
        xui_bind.Finish();
        edf::native::HookTiming xui_draw(edf::native::HookPhase::XuiDraw);
        const size_t bytes=size_t(ctx.r5.u32)*8;
        if(xui_seam) {
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            vertex,pixel,viewport,key,
            edf::native::PositionTriangleStream::Layout(),edf::native::kNativePositionLayoutId,
            // The reversed-depth variant is a different compiled shader under the
            // same guest handle, so it belongs in the identity: a cache keyed on
            // the handle alone hands back the pipeline built from the other one.
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
            pair.pixel,edf::native::NativeBackendTopology::TriangleList});
          state.xui_vertices->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,
                                   {reader.Bytes(ctx.r6.u32,bytes),bytes});
        } else state.xui_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,bytes),bytes});
        xui_draw.Finish();
        native_submitted=true;
        // Partial alpha geometry cannot establish initialized full-frame pixels.
        // Preserve the preceding clear/post pass's initialization state.
        scene.frame_complete=false;
        if(++state.xui_draws<=5 || state.xui_draws%10000==0)
          REXLOG_INFO("Native XUI brush draw: submitted={}, PS={}, vertices={}, target_initialized={}, scene={}, reverse_depth={}",
            state.xui_draws,pixel.shader().entry.name,ctx.r5.u32,target.content_valid,scene_draw,viewport.reverse_depth);
      }
    } catch(const std::exception& error) {
      if(++state.xui_errors<=10) REXLOG_ERROR("Native XUI textured draw: {}",error.what());
    }
    bool font_draw=false;
    if(!movie_draw && !xui_draw && !state.active_target && state.scenes.contains(state.active_output)) try {
      const edf::native::GuestReader font_backing(base);
      const auto& pair=bound_shaders();
      // Shared declaration/VS/PS owned by retail font setup821AD3B8. Read
      // current handles rather than retaining identities across font releases.
      const auto font=edf::native::ReadGuestWords<3>(font_backing,0x8257C06C);
      font_draw=font[0] && font[1] && font[2] && pair.vertex==font[1] && pair.pixel==font[2];
      if(font_draw) {
        const auto device=ctx.r3.u32;
        const edf::native::GuestReadWindow reader(font_backing,font_backing.Add(device,1024),12416-1024);
        if(reader.Word(0x82556148)!=0x820179A8)
          throw std::runtime_error("unsupported font source pointer");
        const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,device);
        auto& scene=state.scenes.at(state.active_output);
        if(state.declaration_bindings.at(device)!=font[0] || snapshot.surface!=scene.output_surface ||
           ctx.r4.u32!=13 || ctx.r7.u32!=16 || !ctx.r5.u32 || ctx.r5.u32%4 || ctx.r5.u32>16384)
          throw std::runtime_error("unsupported font output/declaration/topology");
        const auto declaration=state.declarations.Get(font[0]);
        if(declaration->count()!=2 || !edf::native::IsFontVertexDeclaration(declaration->count(),
           declaration->Words<6>()))
          throw std::runtime_error("unsupported font vertex elements");
        auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
        if(edf::native::NativeRenderDimensions()[0]>0)
          viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(scene.output.sampled.width)/1280,
            float(scene.output.sampled.height)/720,true);
        const auto font_layout=edf::native::NativeHudLayout(scene.output.sampled.width,scene.output.sampled.height);
        viewport=edf::native::MapNativeCanvasViewportScissor(viewport,font_layout,
          scene.output.sampled.width,scene.output.sampled.height);
        const auto key=snapshot.render;
        if(viewport.reverse_depth || (key[1]&3))
          throw std::runtime_error("unimplemented font depth contract");
        const auto texture=state.textures.find(snapshot.texture);
        if(texture==state.textures.end() || !texture->second.content_valid || !texture->second.backend)
          throw std::runtime_error("font atlas has no native texture");
        // Asked of the seam; see the XUI brush above.
        if(texture->second.cube || !scene.output.backend_surface ||
           texture->second.backend.get()==scene.output.backend_surface->texture())
          throw std::runtime_error("unsupported or aliased font atlas");
        if(!state.font_vertex) {
          const auto effect=edf::native::MakeNativeFontEffect();
          auto vertex=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[0],"native_font.fx"));
          auto pixel=std::make_unique<edf::native::ShaderBindings>(state.device.Get(),
            edf::native::CompileNativeShader(state.device.Get(),effect,effect.entries[1],"native_font.fx"));
          edf::native::ValidateNativeShaderLink(vertex->shader(),pixel->shader());
          auto vertices=std::make_unique<edf::native::QuadStream>(state.device.Get(),vertex->shader());
          const edf::native::NativeFontBindings plan(*vertex,*pixel);
          state.font_bindings=plan;
          state.font_vertex=std::move(vertex); state.font_pixel=std::move(pixel);
          state.font_vertices=std::move(vertices);
        }
        // Font producer821ADCE8 writes c1 directly for each glyph run. The
        // native cbuffer packs float2 fields, unlike the guest's register slots.
        const std::span<const uint8_t> vs{reader.Bytes(reader.Add(device,1808),64),64};
        const std::span<const uint8_t> ps{reader.Bytes(reader.Add(device,5888),32),32};
        const bool font_canvas=edf::native::NativeRenderDimensions()[0]>0;
        if(font_canvas && state.font_draws<5) {
          REXLOG_INFO("Native font canvas trace: viewport={},{} {}x{}, scissor_enabled={}, scissor={},{},{},{}, offset={},{} scale={},{}",
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            snapshot.viewport.scissor_enabled,viewport.scissor.left,viewport.scissor.top,viewport.scissor.right,viewport.scissor.bottom,
            std::bit_cast<float>(reader.Word(device+1840)),std::bit_cast<float>(reader.Word(device+1844)),
            std::bit_cast<float>(reader.Word(device+1856)),std::bit_cast<float>(reader.Word(device+1860)));
        }
        // Text follows the 2D canvas layout (native_display_layout.h); its
        // scissor was mapped with it above.
        state.font_bindings->SetConstants(*state.font_vertex,*state.font_pixel,vs,ps,
          font_canvas?float(scene.output.sampled.width)/1280.0f:1.0f,
          font_canvas?float(scene.output.sampled.height)/720.0f:1.0f,font_layout);
        state.font_bindings->SetTexture(*state.font_pixel,texture->second.backend);
        state.font_bindings->SetSampler(*state.font_pixel,edf::native::SamplerLocked(state,
          edf::native::SamplerStateKey(edf::native::ReadSamplerWords(reader,device,0))));
        auto& render=edf::native::RenderStateLocked(state,key);
        const size_t bytes=size_t(ctx.r5.u32)*16;
        if(EDF_NATIVE_FLAG(seam_draws)) {
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            *state.font_vertex,*state.font_pixel,viewport,key,
            edf::native::QuadStream::Layout(),edf::native::kNativeQuadLayoutId,
            // The reversed-depth variant is a different compiled shader under the
            // same guest handle, so it belongs in the identity: a cache keyed on
            // the handle alone hands back the pipeline built from the other one.
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),
            pair.pixel,edf::native::NativeBackendTopology::TriangleList});
          state.font_vertices->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,
                                    {reader.Bytes(ctx.r6.u32,bytes),bytes});
        } else {
          edf::native::BindActiveTarget(state);
          edf::native::BindGuestRenderState(render,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
          state.font_vertex->Bind(*state.context.Get()); state.font_pixel->Bind(*state.context.Get());
          state.font_vertices->Draw(*state.context.Get(),{reader.Bytes(ctx.r6.u32,bytes),bytes});
        }
        native_submitted=true;
        scene.frame_complete=false;
        if(++state.font_draws<=5 || state.font_draws%10000==0)
          REXLOG_INFO("Native font draw: submitted={}, vertices={}, output_initialized={}",
            state.font_draws,ctx.r5.u32,scene.output.content_valid);
        const auto prefix=REXCVAR_GET(edf_native_scene_capture);
        if(!prefix.empty() && scene.output.content_valid &&
           (state.font_draws==1 || state.font_draws==100 || state.font_draws==1000)) {
          const auto path=std::filesystem::path(prefix+".font."+std::to_string(state.font_draws)+".bmp");
          if(std::filesystem::exists(path)) throw std::runtime_error("native font capture path already exists");
          const auto bytes=edf::native::CaptureOutputBmp(state,scene);
          std::ofstream output(path,std::ios::binary);
          output.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
          output.close();
          if(!output) throw std::runtime_error("native font capture write failed");
        }
      }
    } catch(const std::exception& error) {
      if(++state.font_errors<=10) REXLOG_ERROR("Native font draw: {}",error.what());
    }
    classify_timing.Finish();
    edf::native::HookTiming utility3d_timing(edf::native::HookPhase::ImmediateUtility3D);
    bool utility_3d_draw=false;
    if(!movie_draw && !xui_draw && !font_draw && state.active_scene && !state.active_target) try {
      const auto& pair=bound_shaders();
      const auto& classified=pair_class();
      if(classified.payload.vertex && classified.payload.pixel) {
        auto* const vertex=classified.payload.vertex;
        const auto& vs=vertex->bindings->shader();
        auto& ps=*classified.payload.pixel->bindings;
        const auto kind=classified.kind.utility3d;
        const bool solid=kind==PairClass::Utility3D::Solid;
        const bool textured=kind==PairClass::Utility3D::Textured;
        const bool particle=kind==PairClass::Utility3D::Particle || kind==PairClass::Utility3D::ZParticle;
        utility_3d_draw=solid || textured || particle;
        if(utility_3d_draw) {
          const edf::native::GuestReader backing(base);
          const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
          const uint32_t stride=solid?16:particle?44:36;
          const uint32_t element_count=solid?2:particle?4:3;
          const bool strip=ctx.r4.u32==6;
          if(pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel ||
             (solid?!strip:particle?ctx.r4.u32!=13:(!strip && ctx.r4.u32!=13)) ||
             ctx.r7.u32!=stride || ctx.r5.u32<3 || ctx.r5.u32>16384 || (!strip && ctx.r5.u32%4))
            throw std::runtime_error("unsupported native scene immediate contract");
          auto& scene=state.scenes.at(state.active_scene);
          if(!scene.color_surface || reader.Word(reader.Add(ctx.r3.u32,12168))!=scene.color_surface)
            throw std::runtime_error("native scene immediate surface mismatch");
          const auto declaration=state.declaration_bindings.at(ctx.r3.u32);
          const auto owned_declaration=state.declarations.Get(declaration);
          if(owned_declaration->count()!=element_count)
            throw std::runtime_error("unsupported Utility 3D declaration count");
          const auto* elements=owned_declaration->bytes().data();
          auto word=[&](size_t at){return edf::native::GuestBlockWord(elements+at);};
          const size_t color=(element_count-1)*12;
          if(word(0)!=0 || word(4)!=0x2a23b9 || (word(8)&0xffffff00)!=0 ||
             word(color)!=(solid?12u:particle?28u:20u) || word(color+4)!=(solid?0x182886u:0x1a23a6u) ||
             (word(color+8)&0xffffff00)!=0xa0000 ||
             (!solid && (word(12)!=12 || word(16)!=0x2c23a5 || (word(20)&0xffffff00)!=0x50000)) ||
             (particle && (word(24)!=20 || word(28)!=0x2c23a5 || (word(32)&0xffffff00)!=0x50100)))
            throw std::runtime_error("unsupported Utility 3D vertex layout");
          const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
          auto& bindings=edf::native::VertexBindingsForDraw(*vertex,viewport.reverse_depth);
          if(!bindings.HasAllTextureInputs() || !ps.HasAllTextureInputs())
            throw std::runtime_error("Utility 3D missing texture inputs");
          if(edf::native::SamplesTarget(bindings,scene.color) || edf::native::SamplesTarget(ps,scene.color))
            throw std::runtime_error("native scene immediate samples its target");
          // The one guest read of the vertices; recording takes host bytes.
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          const auto key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
          edf::native::RecordNativeSceneImmediate(state,reader,ctx.r3.u32,{
            bindings,ps,viewport,key,pair,declaration,element_count,owned_declaration,ctx.r4.u32,stride},vertices);
          native_submitted=true; scene.frame_complete=false;
          auto& reported=state.scene_immediate_variants_reported[solid?0:textured?(strip?1:2):kind==PairClass::Utility3D::ZParticle?4:3];
          if(++state.utility_3d_draws<=5 || !reported || state.utility_3d_draws%10000==0)
            REXLOG_INFO("Native scene immediate: submitted={}, vertices={}, reverse_depth={}, scene={:#x}, VS={}, PS={}, stride={}",
              state.utility_3d_draws,ctx.r5.u32,viewport.reverse_depth,state.active_scene,
              vs.entry.name,ps.shader().entry.name,stride);
          reported=true;
        }
      }
    } catch(const std::exception& error) {
      if(++state.utility_3d_errors<=10) REXLOG_ERROR("Native Utility 3D strip: {}",error.what());
    }
    utility3d_timing.Finish();
    edf::native::HookTiming immediate_tail_timing(edf::native::HookPhase::ImmediateTail);
    bool utility_draw=false;
    const auto utility_scene_owner=state.active_scene?state.active_scene:state.active_output;
    if(!movie_draw && !xui_draw && !font_draw && !state.active_target && state.scenes.contains(utility_scene_owner)) try {
      const auto& pair=bound_shaders();
      const auto& classified=pair_class();
      if(classified.payload.vertex && classified.payload.pixel) {
        auto* const vertex=classified.payload.vertex;
        auto& vs=*vertex->bindings; auto& ps=*classified.payload.pixel->bindings;
        const bool textured=classified.kind.utility2d==PairClass::Utility2D::Textured;
        utility_draw=classified.kind.utility2d!=PairClass::Utility2D::None;
        if(utility_draw) {
          const edf::native::GuestReader backing(base);
          const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
          const auto snapshot=edf::native::ReadAuditedXuiDeviceWords(reader,ctx.r3.u32);
          auto& scene=state.scenes.at(utility_scene_owner);
          const bool scene_draw=state.active_scene!=0;
          const auto expected_surface=scene_draw?scene.color_surface:scene.output_surface;
          auto& target=scene_draw?scene.color:scene.output;
          const uint32_t stride=textured?20:12,element_count=textured?3:2;
          const bool lines=ctx.r4.u32==2;
          if(pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel ||
             !expected_surface || snapshot.surface!=expected_surface ||
             (!lines && ctx.r4.u32!=13) || ctx.r7.u32!=stride || !ctx.r5.u32 || ctx.r5.u32%(lines?2:4) || ctx.r5.u32>16384)
            throw std::runtime_error("unsupported Utility output/binding/topology");
          const auto declaration_handle=state.declaration_bindings.at(ctx.r3.u32);
          const auto owned_declaration=state.declarations.Get(declaration_handle);
          if(owned_declaration->count()!=element_count)
            throw std::runtime_error("unsupported Utility declaration count");
          const auto* declaration=owned_declaration->bytes().data();
          auto word=[&](size_t offset){return edf::native::GuestBlockWord(declaration+offset);};
          std::shared_ptr<const edf::native::NativeDeclaration> native_declaration;
          try { native_declaration=owned_declaration->Utility2D(textured); }
          catch(const std::exception&) {
            if(state.utility_errors<10) for(uint32_t element=0;element<element_count;++element)
              REXLOG_INFO("Native Utility element: stride={}, element={}, offset={:#x}, type={:#x}, semantic={:#x}",
                stride,element,word(element*12),word(element*12+4),word(element*12+8));
            throw std::runtime_error("unsupported Utility vertex elements");
          }
          auto viewport=edf::native::DecodeDrawViewport(snapshot.viewport);
          if(edf::native::NativeRenderDimensions()[0]>0)
            viewport=edf::native::ScaleNativeCanvasScissor(viewport,float(edf::native::NativeRenderDimensions()[0])/1280,
              float(edf::native::NativeRenderDimensions()[1])/720,true);
          // Ordinary output has no depth attachment. The HDR scene does, and
          // uses the same reversed-clip shader contract as indexed scene draws.
          if(!scene_draw && (viewport.reverse_depth || (snapshot.render[1]&3)))
            throw std::runtime_error("unsupported Utility output depth contract");
          auto& bindings=edf::native::VertexBindingsForDraw(*vertex,viewport.reverse_depth);
          if(!bindings.HasAllTextureInputs()) throw std::runtime_error("Utility vertex shader has missing native texture inputs");
          if(!ps.HasAllTextureInputs()) throw std::runtime_error("Utility has missing native texture inputs");
          if(edf::native::SamplesTarget(ps,target) || edf::native::SamplesTarget(bindings,target))
            throw std::runtime_error("Utility samples its active surface");
          const auto owned_indices=state.generated_indices.Get(lines?edf::native::NativeIndexPattern::Lines:
            edf::native::NativeIndexPattern::Quads,ctx.r5.u32);
          const auto indices=owned_indices->bytes();
          const size_t bytes=size_t(ctx.r5.u32)*stride;
          const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
          // 2D canvas layout on an output that is not 16:9 (edf_hud_safe_area,
          // native_display_layout.h). The activation uploaded the legacy
          // full-target canvas; this draw re-maps the raw constants through
          // its own layout (a fade keeps covering the frame) and its scissor
          // with them. Draws into the HDR scene belong to the 3D view and
          // keep the legacy mapping.
          if(const auto& dims=edf::native::NativeRenderDimensions();dims[0]>0 && !scene_draw) {
            const auto layout=edf::native::NativeHudLayout(target.sampled.width,target.sampled.height);
            if(!layout.identity() && (vertex->canvas_uploaded&3)==3) {
              const float kx=float(dims[0])/1280.0f,ky=float(dims[1])/720.0f;
              const auto affine=edf::native::NativeCanvasDrawAffine(layout,edf::native::NativeCanvasVertexBounds(vertices,stride,
                edf::native::ScaleNativeCanvasXY(vertex->canvas_scale,kx,ky),
                edf::native::ScaleNativeCanvasXY(vertex->canvas_offset,kx,ky)),!textured);
              bindings.SetGuestFloatRegisters("_g_DX2DScale",edf::native::MapNativeCanvasXY(vertex->canvas_scale,kx,ky,affine,false));
              bindings.SetGuestFloatRegisters("_g_DX2DOffset",edf::native::MapNativeCanvasXY(vertex->canvas_offset,kx,ky,affine,true));
              viewport=edf::native::MapNativeCanvasViewportScissor(viewport,affine,target.sampled.width,target.sampled.height);
            }
          }
          auto& mesh=state.immediate_meshes.Acquire(edf::native::EnsureSceneBackendLocked(state),bindings.shader(),
            edf::native::ImmediateStreamKey(vertices.size(),declaration_handle,pair.vertex,ctx.r4.u32,viewport.reverse_depth),
            native_declaration->bytes(),stride,
            vertices,indices,2,native_declaration,owned_indices,{},{},{},{},0,{},
            // Where this mesh's dynamic vertices are rewritten when the draw
            // is recorded; the immediate context does it otherwise.
            EDF_NATIVE_FLAG(seam_draws)?&edf::native::SceneRecorderLocked(state):nullptr);
          auto& render=edf::native::RenderStateLocked(state,snapshot.render);
          if(EDF_NATIVE_FLAG(seam_draws)) {
            auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
              bindings,ps,viewport,snapshot.render,
              mesh.input_layout().elements(),mesh.input_layout().fingerprint(),
              // The reversed-depth variant is a different compiled shader under
              // the same guest handle, so it belongs in the identity.
              (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),pair.pixel,
              lines?edf::native::NativeBackendTopology::LineList
                   :edf::native::NativeBackendTopology::TriangleList});
            // state.recorded.pipeline: the pipeline RecordDrawSetup just bound.
            if(NativeTransientBatchingEnabled() && state.recorded.pipeline)
              mesh.DrawTransientExpanded(recorder,*state.recorded.pipeline,vertices,0,uint32_t(indices.size()/2),
                lines?edf::native::NativeBackendTopology::LineList:edf::native::NativeBackendTopology::TriangleList);
            else if(lines) mesh.DrawLinesTransient(recorder,vertices,0,uint32_t(indices.size()/2));
            else mesh.DrawTransient(recorder,vertices,0,uint32_t(indices.size()/2));
          } else {
            edf::native::BindActiveTarget(state);
            edf::native::BindGuestRenderState(render,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation); viewport.Bind(*state.context.Get());
            bindings.Bind(*state.context.Get()); ps.Bind(*state.context.Get());
            if(lines) mesh.DrawLines(*state.context.Get(),0,uint32_t(indices.size()/2));
            else mesh.Draw(*state.context.Get(),0,uint32_t(indices.size()/2));
          }
          native_submitted=true; scene.frame_complete=false;
          auto& variant_reported=state.utility_variants_reported[(textured?2:0)+(lines?1:0)];
          if(++state.utility_draws<=5 || !variant_reported)
            REXLOG_INFO("Native Utility draw: submitted={}, VS={}, PS={}, vertices={}, lines={}, scene={}, reverse_depth={}",
              state.utility_draws,vs.shader().entry.name,ps.shader().entry.name,ctx.r5.u32,lines,scene_draw,viewport.reverse_depth);
          variant_reported=true;
        }
      }
    } catch(const std::exception& error) {
      if(++state.utility_errors<=10) REXLOG_ERROR("Native Utility quad: {}",error.what());
    }
    const bool output_candidate=!movie_draw && !xui_draw && !font_draw && !utility_draw && !state.active_target && state.scenes.contains(state.active_output) &&
      state.shaders.contains(state.linked_pixel) &&
      state.shaders.at(state.linked_pixel).bindings->shader().entry.name=="PS_Bloom";
    bool output_draw=false;
    if (output_candidate) {
      try {
        const edf::native::GuestReader reader(base);
        const auto& pair=bound_shaders();
        output_draw=pair.vertex==state.active_vertex &&
          pair.pixel==state.linked_pixel &&
          ctx.r4.u32==13 && ctx.r7.u32==16;
        if (!output_draw && ++state.output_unhandled<=5) {
          const auto actual_vs=pair.vertex;
          const auto actual_ps=pair.pixel;
          REXLOG_INFO("Native ordinary output unhandled draw: caller={:#x}, primitive={}, stride={}, VS={:#x}, PS={:#x}; UI coverage incomplete",
            uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,actual_vs,actual_ps);
          for (const auto handle:{actual_vs,actual_ps}) if (state.embedded_shaders.contains(handle)) {
            const auto identity=state.embedded_shaders.at(handle);
            REXLOG_INFO("Native embedded shader identity: handle={:#x}, source={:#x}, pixel={}",handle,identity.source,identity.pixel);
          }
          for(const auto handle:{actual_vs,actual_ps}) if(state.shaders.contains(handle)) {
            const auto& shader=state.shaders.at(handle).bindings->shader();
            REXLOG_INFO("Native source shader identity: handle={:#x}, entry={}, pixel={}, source_bytes={}, fingerprint={:#x}",
              handle,shader.entry.name,shader.entry.pixel,shader.source_bytes,shader.source_fingerprint);
          }
        }
      } catch (const std::exception& error) { REXLOG_ERROR("Native ordinary output selection: {}",error.what()); }
    }
    // Observe every unsupported output pair, not just draws following Bloom.
    // Otherwise an unimplemented menu shader can disappear without evidence.
    if(!movie_draw && !xui_draw && !font_draw && !utility_draw && !output_draw && !state.active_target &&
       state.scenes.contains(state.active_output) && state.unsupported_output_pairs.size()<64) try {
      const auto& pair=bound_shaders();
      const std::array<uint32_t,4> key{pair.vertex,pair.pixel,ctx.r4.u32,ctx.r7.u32};
      edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
        edf::native::NativeContractPath::Output,pair.vertex,pair.pixel,0,ctx.r4.u32,ctx.r7.u32),
        "no native implementation for this output shader pair");
      if(state.unsupported_output_pairs.insert(key).second) {
        REXLOG_INFO("Native unsupported output shader pair: caller={:#x}, VS={:#x}, PS={:#x}, primitive={}, stride={}, vertices={}",
          uint32_t(ctx.lr),pair.vertex,pair.pixel,ctx.r4.u32,ctx.r7.u32,ctx.r5.u32);
        for(const auto handle:{pair.vertex,pair.pixel}) {
          if(const auto found=state.embedded_shaders.find(handle);found!=state.embedded_shaders.end())
            REXLOG_INFO("Native unsupported embedded source: handle={:#x}, source={:#x}, pixel={}",
              handle,found->second.source,found->second.pixel);
          if(const auto found=state.shaders.find(handle);found!=state.shaders.end()) {
            const auto& shader=found->second.bindings->shader();
            REXLOG_INFO("Native unsupported material source: handle={:#x}, entry={}, fingerprint={:#x}",
              handle,shader.entry.name,shader.source_fingerprint);
          }
        }
      }
    } catch(const std::exception& error) {
      if(++state.output_unhandled<=5) REXLOG_ERROR("Native unsupported output inspection: {}",error.what());
    }
    if (state.render_targets.contains(state.active_target) || output_draw) {
      ++state.immediate_draws;
      try {
        const edf::native::GuestReader backing(base);
        const edf::native::GuestReadWindow reader(backing,backing.Add(ctx.r3.u32,1024),12416-1024);
        if (output_draw && reader.Word(reader.Add(ctx.r3.u32,12168))!=state.scenes.at(state.active_output).output_surface)
          throw std::runtime_error("ordinary output does not match guest bound surface");
        const auto declaration = state.declaration_bindings.at(ctx.r3.u32);
        const auto owned_declaration=state.declarations.Get(declaration);
        const auto count = owned_declaration->count();
        if (state.immediate_draws <= 5) REXLOG_INFO("Native immediate draw: primitive={}, vertices={}, stride={}, declaration={:#x}, elements={}",
                    ctx.r4.u32,ctx.r5.u32,ctx.r7.u32,declaration,count);
        if(state.immediate_draws<=5) for (uint32_t i = 0; i < count; ++i) {
          const auto element = owned_declaration->Words<3>(i*12);
          REXLOG_INFO("Native immediate element: stream_offset={:#x}, type={:#x}, method_usage_index={:#x}",
                      element[0],element[1],element[2]);
        }
        if (ctx.r4.u32 != 13 || ctx.r7.u32 != 16 || !ctx.r5.u32 || ctx.r5.u32 % 4 || ctx.r5.u32 > 16384 || count != 2 ||
            owned_declaration->Words<6>()!=std::array<uint32_t,6>{0,0x2c23a5,0,8,0x2c23a5,0x50000})
          throw std::runtime_error("unsupported native immediate vertex declaration/topology");
        const auto found = state.shaders.find(state.active_vertex);
        if (found == state.shaders.end()) throw std::runtime_error("missing native immediate vertex shader");
        const auto& pair=bound_shaders();
        if (pair.vertex!=state.active_vertex || pair.pixel!=state.linked_pixel)
          throw std::runtime_error("native immediate shader pair differs from guest device");
        auto& shader = found->second;
        const size_t bytes = size_t(ctx.r5.u32)*16;
        const auto render_key=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
        auto render_state = state.render_states.find(render_key);
        if (render_state == state.render_states.end()) {
          auto native = edf::native::CreateNativeRenderState(state.device.Get(),render_key);
          render_state = state.render_states.emplace(render_key,std::move(native)).first;
          REXLOG_INFO("Native render state: cached={}, blend={:#x}, depth={:#x}, raster={:#x}, alpha={:#x}, write_mask={}, scissor={}",
                      state.render_states.size(),render_key[0],render_key[1],render_key[2],render_key[3],render_key[4],render_key[5]);
        }
        const bool post_seam=EDF_NATIVE_FLAG(seam_draws);
        if(!post_seam)
          edf::native::BindGuestRenderState(render_state->second,*state.context.Get(),reader,ctx.r3.u32,&state.bind_generation);
        const auto viewport=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
        if (state.immediate_draws<=15) {
          REXLOG_INFO("Native post pass: VS={}, PS={}, viewport={},{},{}x{}, depth={}..{}, reverse={}",
            shader.bindings->shader().entry.name,state.shaders.at(state.linked_pixel).bindings->shader().entry.name,
            viewport.viewport.TopLeftX,viewport.viewport.TopLeftY,viewport.viewport.Width,viewport.viewport.Height,
            viewport.viewport.MinDepth,viewport.viewport.MaxDepth,viewport.reverse_depth);
          for (uint32_t i=0;i<4;++i) {
            const auto vertex=reader.Add(ctx.r6.u32,i*16);
            REXLOG_INFO("Native post vertex: index={}, position={},{} UV={},{}",i,
              std::bit_cast<float>(reader.Word(vertex)),std::bit_cast<float>(reader.Word(reader.Add(vertex,4))),
              std::bit_cast<float>(reader.Word(reader.Add(vertex,8))),std::bit_cast<float>(reader.Word(reader.Add(vertex,12))));
          }
        }
        if(!post_seam) viewport.Bind(*state.context.Get());
        auto& bindings=edf::native::VertexBindingsForDraw(shader,viewport.reverse_depth);
        auto& quads=viewport.reverse_depth ? shader.reversed_quads : shader.quads;
        if (!quads) quads=std::make_unique<edf::native::QuadStream>(state.device.Get(),bindings.shader());
        const std::span<const uint8_t> vertices{reader.Bytes(ctx.r6.u32,bytes),bytes};
        auto& pixel=*state.shaders.at(state.linked_pixel).bindings;
        if(!post_seam) { bindings.Bind(*state.context.Get()); pixel.Bind(*state.context.Get()); }
        auto& target=output_draw ? state.scenes.at(state.active_output).output : state.render_targets.at(state.active_target).native;
        if(auto* finish_audit=edf::native::post_finish_recorder; finish_audit && finish_audit->seen.native_armed) {
          // Finish audit: the extent this pass really draws to, and the tone
          // constants the live native bindings carry (no guest setter writes them).
          try {
            std::array<float,3> tone{NAN,NAN,NAN};
            const std::array<const char*,3> names{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite","g_PostEffect_ToneMap"};
            for(size_t c=0;c<names.size();++c)
              if(const auto value=pixel.ReadFloatVector(names[c]);!value.empty()) tone[c]=value[0];
            finish_audit->seen.NativeDraw(int32_t(target.sampled.width),int32_t(target.sampled.height),tone);
          } catch(const std::exception& error) { finish_audit->Fail(error); }
        }
        // Read the actual post-pass views before drawing, not a guessed target
        // from the resource registry after later passes may have changed it.
        const auto post_prefix=REXCVAR_GET(edf_native_output_capture_scene_color)
          ? REXCVAR_GET(edf_native_scene_capture) : std::string{};
        const auto post_frame=state.indexed_output_frames+1;
        if((output_draw || state.active_target) && pixel.shader().source_fingerprint==0x6b7926f9747c6933ull &&
           REXCVAR_GET(edf_native_output_capture_scene_color) && !post_prefix.empty() &&
           edf::native::NativeSceneDrew(state.indexed_submitted,state.scene_indexed_start,state.scene_full_frame) &&
           edf::native::ShouldCaptureNativeOutput(post_frame,state.output_captures,
             REXCVAR_GET(edf_native_output_capture_limit),REXCVAR_GET(edf_native_output_capture_interval),
             REXCVAR_GET(edf_native_output_capture_start_frame)) &&
           (state.post_input_capture_frame!=post_frame || state.post_input_capture_pass<32)) {
          if(state.post_input_capture_frame!=post_frame) {
            state.post_input_capture_frame=post_frame; state.post_input_capture_pass=0;
          }
          const auto pass=++state.post_input_capture_pass;
          try {
            REXLOG_INFO("Native post chain pass: frame={}, pass={}, shader={}, target={}x{}",
              post_frame,pass,pixel.shader().entry.name,target.sampled.width,target.sampled.height);
            for(const auto* name:{"m_DiffuseTexture0_Sampler","m_DiffuseTexture1_Sampler","m_Tone_Sampler","m_OldTone_Sampler"}) {
              auto* bound=pixel.ReadTexture(name);
              if(!bound) continue; // A pass only reflects the inputs it consumes.
              auto* view=edf::native::NativeD3D11TextureView(*bound);
              auto* texture=edf::native::NativeD3D11TextureResource(*bound);
              if(!view || !texture) continue; // A D3D11-only diagnostic.
              D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{}; view->GetDesc(&view_desc);
              if(view_desc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MostDetailedMip!=0)
                throw std::runtime_error("unsupported post diagnostic view");
              D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
              const auto value=edf::native::ReadNativeColorPixel(*state.context.Get(),*texture,desc.Width/2,desc.Height/2);
              REXLOG_INFO("Native exact post input: frame={}, pass={}, name={}, {}x{}, view_format={}, center={},{},{},{}",
                post_frame,pass,name,desc.Width,desc.Height,uint32_t(view_desc.Format),value[0],value[1],value[2],value[3]);
              // The saved BMP clamps to [0,1]; the tone curve's behaviour depends
              // on whether anything actually exceeds 1. Report the real range.
              const auto hdr=edf::native::InspectNativeHdrColor(*state.context.Get(),*texture);
              REXLOG_INFO("Native exact post input range: frame={}, pass={}, name={}, pixels={}, nonfinite={}, min={},{},{}, max={},{},{}, mean={},{},{}, above_1={}, above_2={}, above_4={}, above_8={}",
                post_frame,pass,name,hdr.pixels,hdr.nonfinite_pixels,
                hdr.minimum[0],hdr.minimum[1],hdr.minimum[2],hdr.maximum[0],hdr.maximum[1],hdr.maximum[2],
                hdr.mean[0],hdr.mean[1],hdr.mean[2],hdr.above[0],hdr.above[1],hdr.above[2],hdr.above[3]);
              if(hdr.negative_pixels)
                REXLOG_WARN("Native exact post input negatives: frame={}, pass={}, name={}, negative_pixels={} of {}, worst_at={},{} (negative radiance; the tone curve maps a large negative to white)",
                  post_frame,pass,name,hdr.negative_pixels,hdr.pixels,hdr.worst_x,hdr.worst_y);
              const auto path=std::filesystem::path(post_prefix+".pass."+std::to_string(pass)+"."+pixel.shader().entry.name+".input."+name+"."+std::to_string(post_frame)+".bmp");
              if(std::filesystem::exists(path)) throw std::runtime_error("post input capture already exists");
              const auto bmp=edf::native::CaptureNativeHdrBmp(*state.context.Get(),*texture);
              std::ofstream file(path,std::ios::binary);
              file.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); file.close();
              if(!file) throw std::runtime_error("post input capture write failed");
            }
            for(const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite",
                                  "g_PostEffect_ToneMap"}) {
              const auto value=pixel.ReadFloatVector(name);
              if(!value.empty()) REXLOG_INFO("Native exact post constant: frame={}, pass={}, {}={}",post_frame,pass,name,value[0]);
            }
            // The sampling geometry decides whether a reduction averages the
            // intended footprint and whether a blur preserves its input's mean.
            // Report the authored values, not an assumed normalized kernel.
            if(const auto offsets=pixel.ReadFloatArray("m_DownsampleUVOffset");!offsets.empty()) {
              std::string text;
              for(size_t i=0;i+1<offsets.size();i+=2)
                text+=std::format("{}({},{})",text.empty()?"":" ",offsets[i],offsets[i+1]);
              REXLOG_INFO("Native exact post downsample offsets: frame={}, pass={}, count={}, uv={}",
                post_frame,pass,offsets.size()/2,text);
            }
            if(const auto taps=pixel.ReadFloatArray("m_GaussBlurUVOffset");!taps.empty()) {
              float weight_sum=0;
              std::string text;
              for(size_t i=0;i+2<taps.size();i+=3) {
                weight_sum+=taps[i+2];
                text+=std::format("{}({},{})*{}",text.empty()?"":" ",taps[i],taps[i+1],taps[i+2]);
              }
              REXLOG_INFO("Native exact post blur kernel: frame={}, pass={}, taps={}, weight_sum={}, offsets={}",
                post_frame,pass,taps.size()/3,weight_sum,text);
            }
          } catch(const std::exception& error) {
            // Optional diagnostics must never suppress the real draw.
            REXLOG_ERROR("Native post input capture: {}",error.what());
          }
        }
        const bool initialized=edf::native::CanInitializeReductionTarget(bindings.shader(),pixel.shader(),
          vertices,viewport,render_key,target.sampled.width,target.sampled.height,pixel.HasAllTextureInputs());
        // The offset follows the guest's live PA_SU_VTX_CNTL rather than a
        // fixed assumption, exactly as the SDK's own GPU path selects it. Only
        // the audited full-screen post passes consume it so far: indexed world
        // geometry, font, movie and the remaining immediate draws each need
        // their own validation before the same offset is extended to them.
        const auto centers_word=edf::native::ReadVertexCenterWord(reader,ctx.r3.u32);
        if(state.vertex_center_word!=centers_word) {
          state.vertex_center_word=centers_word;
          const auto centers=edf::native::DecodeGuestVertexCenters(centers_word);
          REXLOG_INFO("Native guest vertex centers: PA_SU_VTX_CNTL={:#x}, integer_centers={}, rounding={}, quantization={}, enabled={}",
            centers_word,centers.integer_centers,centers.rounding,centers.quantization,
            REXCVAR_GET(edf_native_pixel_centers));
        }
        const float center_offset=REXCVAR_GET(edf_native_pixel_centers)
          ? edf::native::GuestPixelCenterOffset(centers_word) : 0.f;
        const bool shift_centers=initialized && center_offset!=0.f;
        // The shadow render's guest post (edf_native_shadow_render) holds the
        // tone history: the target already holds this frame's value from the
        // native post, so the 0.025-per-draw blend is not applied twice.
        const bool shadow_tone_hold=native_shadow_guest && !output_draw && target.content_valid &&
          pixel.shader().entry.name=="PS_Downsample_Tone";
        if(shadow_tone_hold) ++native_shadow_guest->tone_holds;
        else if(post_seam) {
          auto shifted=viewport;
          if(shift_centers) {
            shifted.viewport.TopLeftX+=center_offset;
            shifted.viewport.TopLeftY+=center_offset;
          }
          auto& recorder=edf::native::RecordDrawSetup(state,reader,ctx.r3.u32,{
            bindings,pixel,shifted,render_key,
            edf::native::QuadStream::Layout(),edf::native::kNativeQuadLayoutId,
            (uint64_t(pair.vertex)<<1)|uint64_t(viewport.reverse_depth?1:0),pair.pixel,
            edf::native::NativeBackendTopology::TriangleList});
          quads->Draw(edf::native::EnsureSceneBackendLocked(state),recorder,vertices);
        } else {
          if(shift_centers) {
            auto shifted=viewport.viewport;
            shifted.TopLeftX+=center_offset; shifted.TopLeftY+=center_offset;
            state.context->RSSetViewports(1,&shifted);
          }
          try { quads->Draw(*state.context.Get(),vertices); }
          catch(...) { if(shift_centers) viewport.Bind(*state.context.Get()); throw; }
          if(shift_centers) viewport.Bind(*state.context.Get());
        }
        if(output_draw && state.scene_gpu_timer && state.scene_gpu_timer_resolved &&
           state.scene_gpu_timer_owner==state.active_output) {
          try { state.scene_gpu_timer->End(); }
          catch(const std::exception& error) {
            state.scene_gpu_timer.reset();
            REXLOG_ERROR("Native frame GPU timing end: {}",error.what());
          }
          state.scene_gpu_timer_owner=0;
          state.scene_gpu_timer_resolved=false;
        }
        if(state.scene_gpu_timer_owner && state.scene_gpu_timer_resolved &&
           pixel.shader().entry.name=="PS_Downsample_Tone") {
          for(const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_ToneMap"}) {
            const auto value=pixel.ReadFloatVector(name);
            if(!value.empty()) REXLOG_INFO("Native tone history constant: {}={}, scene={}",name,value[0],state.scene_begins);
          }
        }
        native_submitted=true;
        ++state.native_quad_draws;
        if(!shadow_tone_hold) target.content_valid=initialized;
        if (output_draw && ++state.output_draws<=5)
          REXLOG_INFO("Native final bloom: draws={}, initialized={}, frame_complete=false",state.output_draws,initialized);
        const auto bloom_now=std::chrono::steady_clock::now();
        if (output_draw && (state.output_draws<=5 ||
            (!REXCVAR_GET(edf_native_scene_capture).empty() &&
             bloom_now-state.bloom_parameters_reported>=std::chrono::seconds(10)))) {
          state.bloom_parameters_reported=bloom_now;
          for (const auto* name:{"g_PostEffect_MiddleGray","g_PostEffect_LuminanceWhite"}) {
            const auto value=pixel.ReadFloatVector(name);
            if (!value.empty()) REXLOG_INFO("Native bloom constant: {}={}, indexed_frame={}, output_draw={}",
              name,value[0],state.indexed_output_frames,state.output_draws);
          }
        }
        if (state.native_quad_draws <= 15 || state.native_quad_draws % 1000 == 0)
          REXLOG_INFO("Native immediate draw: submitted={}, errors={}, contents_valid={}",state.native_quad_draws,state.native_quad_errors,initialized);
      } catch (const std::exception& error) {
        ++state.native_quad_errors;
        if (output_draw) state.scenes.at(state.active_output).output.content_valid=false;
        else state.render_targets.at(state.active_target).native.content_valid=false;
        if (state.native_quad_errors <= 10) REXLOG_ERROR("Native immediate draw: {}",error.what());
      }
    }
    ++state.immediate_requests;
    if(!ctx.r5.u32) ++state.immediate_empty;
    else if(native_submitted) ++state.immediate_submitted;
    else {
      ++state.immediate_unsubmitted;
      try {
        const edf::native::GuestReader reader(base);
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Immediate,state.active_vertex,state.linked_pixel,
          reader.Word(reader.Add(ctx.r3.u32,11536)),ctx.r4.u32,ctx.r7.u32),
          "immediate draw not submitted natively");
      } catch(const std::exception&) {
        edf::native::ReportNativeContractRejection(state,edf::native::MakeNativeContract(state,
          edf::native::NativeContractPath::Immediate,state.active_vertex,state.linked_pixel,
          0,ctx.r4.u32,ctx.r7.u32),"immediate draw not submitted natively; device unreadable");
      }
      // This is evidence of routing coverage, not automatically a pixel bug:
      // the guest may issue draws outside a valid target or during teardown.
      if(state.immediate_unsubmitted_paths.size()<64) {
        const std::array<uint32_t,5> key{uint32_t(ctx.lr),ctx.r4.u32,ctx.r7.u32,state.active_vertex,state.linked_pixel};
        if(state.immediate_unsubmitted_paths.insert(key).second) {
          REXLOG_INFO("Native immediate unsubmitted path: caller={:#x}, primitive={}, stride={}, vertices={}, active_VS={:#x}, active_PS={:#x}, target={:#x}, output={:#x}",
            key[0],key[1],key[2],ctx.r5.u32,key[3],key[4],state.active_target,state.active_output);
          try {
            const edf::native::GuestReader reader(base);
            const auto surface=reader.Word(reader.Add(ctx.r3.u32,12168));
            uint32_t scene_owner=0;
            for(const auto& [owner,scene]:state.scenes)
              if(scene.output_surface==surface) {scene_owner=owner; break;}
            REXLOG_INFO("Native unsubmitted target: guest_surface={:#x}, registered_surface={}, known_output_owner={:#x}, scene_count={}",
              surface,state.surface_creations.contains(surface),scene_owner,state.scenes.size());
            REXLOG_INFO("Native unsubmitted scene: active_scene={:#x}",state.active_scene);
            const auto pair=edf::native::ReadShaderPair(reader,ctx.r3.u32);
            const auto draw_view=edf::native::ReadNativeDrawViewport(reader,ctx.r3.u32);
            const auto render_words=edf::native::ReadAuditedRenderStateWords(reader,ctx.r3.u32);
            REXLOG_INFO("Native unsubmitted bindings: VS={:#x}, PS={:#x}, reverse_depth={}, depth_state={:#x}",
              pair.vertex,pair.pixel,draw_view.reverse_depth,render_words[1]);
            for(auto handle:{pair.vertex,pair.pixel}) {
              const auto embedded=state.embedded_shaders.find(handle);
              if(embedded!=state.embedded_shaders.end())
                REXLOG_INFO("Native unsubmitted embedded shader: handle={:#x}, source={:#x}, pixel={}",
                  handle,embedded->second.source,embedded->second.pixel);
              const auto found=state.shaders.find(handle);
              if(found!=state.shaders.end()) {
                const auto& shader=found->second.bindings->shader();
                REXLOG_INFO("Native unsubmitted shader: handle={:#x}, entry={}, fingerprint={:#x}",
                  handle,shader.entry.name,shader.source_fingerprint);
              }
            }
            const auto declaration=reader.Word(reader.Add(ctx.r3.u32,11536));
            const auto count=reader.Word(reader.Add(declaration,24));
            if(count<=16) for(uint32_t element=0;element<count;++element) {
              const auto words=edf::native::ReadGuestWords<3>(reader,reader.Add(declaration,52+element*12));
              REXLOG_INFO("Native unsubmitted element: element={}, offset={:#x}, type={:#x}, semantic={:#x}",
                element,words[0],words[1],words[2]);
            }
          } catch(const std::exception& error) {
            REXLOG_INFO("Native unsubmitted target inspection: {}",error.what());
          }
        }
      }
    }
    const auto now=std::chrono::steady_clock::now();
    if(state.immediate_requests>=1024 && now-state.immediate_coverage_reported>=std::chrono::seconds(10)) {
      state.immediate_coverage_reported=now;
      REXLOG_INFO("Native immediate coverage: requests={}, empty={}, submitted={}, unsubmitted={}, sampled_paths={} (64-path cap; not visual completeness)",
        state.immediate_requests,state.immediate_empty,state.immediate_submitted,state.immediate_unsubmitted,state.immediate_unsubmitted_paths.size());
      REXLOG_INFO("Native immediate mesh cache: builds={}, hits={}, updates={}, bytes={}, entries={}, entry_evictions={}, budget_evictions={}",
        state.immediate_meshes.builds(),state.immediate_meshes.hits(),state.immediate_meshes.updates(),state.immediate_meshes.bytes(),
        state.immediate_meshes.entries(),state.immediate_meshes.entry_evictions(),state.immediate_meshes.budget_evictions());
    }
  }
  const bool native_host=EDF_NATIVE_FLAG(host);
  std::optional<edf::native::NativeConstantOwnership> legacy_scope;
  if(!native_host) legacy_scope.emplace(0);
  native_timing.Finish();
  edf::native::HookTiming guest_timing(edf::native::HookPhase::ImmediateGuest);
  if(native_host) __imp__edf_native_immediate_cpu_tail(ctx,base);
  else __imp__sub_821FD8F8(ctx,base);
}
REX_EXTERN(__imp__sub_8213C328);
REX_EXTERN(__imp__sub_8213ECB0);
REX_EXTERN(__imp__edf_native_main_state_cpu_tail);
REX_HOOK_RAW(sub_8213ECB0) {
  if(!edf::native::NativeConstantOwnership::OwnsMainStatePackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213ECB0(ctx,base); return;
  }
  __imp__edf_native_main_state_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native main-state ownership: retained CPU updates/dirty mask; omitted inline Xbox packets");
}
REX_EXTERN(__imp__sub_8213E950);
REX_EXTERN(__imp__edf_native_shader_upload_cpu_tail);
REX_HOOK_RAW(sub_8213E950) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderUpload(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E950(ctx,base); return;
  }
  __imp__edf_native_shader_upload_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-upload ownership: retained CPU metadata; omitted Xbox allocation/copy/packets");
}
REX_EXTERN(__imp__sub_8213E800);
REX_EXTERN(__imp__edf_native_shader_output_cpu_tail);
REX_HOOK_RAW(sub_8213E800) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderOutputPatch(ctx.r29.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E800(ctx,base); return;
  }
  // E950 keeps its device in r29 at this exact call. Preserve E800's CPU output
  // through r6, but do not rewrite Xbox instructions via E678/E748.
  __imp__edf_native_shader_output_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-output ownership: retained CPU output; omitted Xbox instruction patches");
}
REX_EXTERN(__imp__sub_8213E070);
REX_HOOK_RAW(sub_8213E070) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderMicrocodePatch(ctx.r6.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213E070(ctx,base); return;
  }
  // Recovered native HLSL/input layouts replace the Xbox instruction patcher.
  // Keep EB68/E950's surrounding shader cache/stride metadata updates intact.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-patch ownership: omitted Xbox microcode rewrite");
}
REX_EXTERN(__imp__sub_8213D750);
REX_EXTERN(__imp__edf_native_derived_cpu_tail);
REX_HOOK_RAW(sub_8213D750) {
  if(!edf::native::NativeConstantOwnership::OwnsDerivedStatePackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213D750(ctx,base); return;
  }
  // Keep the exact CPU state calculation, flag transitions and dirty-mask return.
  // Only the command-buffer rollover and packet write are removed by extraction.
  __imp__edf_native_derived_cpu_tail(ctx,base);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native derived-state ownership: preserved CPU updates; omitted Xbox packet block");
}
REX_EXTERN(__imp__sub_8213EAB0);
REX_HOOK_RAW(sub_8213EAB0) {
  if(!edf::native::NativeConstantOwnership::OwnsShaderLoadPackets(ctx.r3.u32,uint32_t(ctx.lr))) {
    __imp__sub_8213EAB0(ctx,base); return;
  }
  // Native-host tails retain ECB0's CPU updates regardless of draw success, but
  // don't copy the guest program relocation table into Xenos load packets.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native shader-load ownership: omitted Xbox program-load packets");
}
REX_EXTERN(__imp__sub_8213D938);
REX_HOOK_RAW(sub_8213D938) {
  if(!edf::native::NativeConstantOwnership::OwnsSpecialRenderPacket(
      ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u64,ctx.r6.u32)) {
    __imp__sub_8213D938(ctx,base); return;
  }
  // Both simple and tiled branches write only packets/cursor40. Preserve the
  // exact dirty-mask return used by the CPU prefix's subsequent bank dispatch.
  ctx.r3.u64=ctx.r4.u64 & ~uint64_t(0x100);
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native special-render ownership: omitted Xbox packets; preserved dirty-mask return");
}
REX_HOOK_RAW(sub_8213C328) {
  if(!edf::native::NativeConstantOwnership::OwnsImmediateAllocation(
      ctx.r3.u32,uint32_t(ctx.lr),ctx.r4.u32,ctx.r5.u32)) {
    __imp__sub_8213C328(ctx,base);
    return;
  }
  // FD428 has already applied derived CPU state, cleared dirty banks and
  // restored its temporary stride byte. Returning no guest storage takes its
  // existing no-buffer exit, preserving cursor40 while bypassing GPU packets.
  // FD8F8 then skips its redundant vertex memcpy and cursor13076 commit.
  // The native draw already succeeded. Do NOT run the allocator's failure path
  // or set device+10809's allocation-failure bit; no allocation was attempted.
  // GPU-only restore bit4096 and scratch fields13076/13080/13088 remain untouched.
  ctx.r3.u64=0;
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native immediate ownership: omitted Xbox vertex allocation/copy and draw packets");
}
REX_EXTERN(__imp__sub_8213DF00);
REX_HOOK_RAW(sub_8213DF00) {
  if(!edf::native::NativeConstantOwnership::Owns(ctx.r3.u32,ctx.r5.u32,ctx.r6.u32)) {
    __imp__sub_8213DF00(ctx,base);
    return;
  }
  // The original groups dirty float4 registers into GPU packet payloads and
  // advances cursor40. It never updates the source constants. Native-host
  // mode has no GPU emulator consuming this packet allocation/copy.
  // The caller still owns its CPU dirty-mask clear and all other derived state.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native constant ownership: omitted Xbox float-constant packet encoding, bank={:#x}",ctx.r5.u32);
}
REX_EXTERN(__imp__sub_8213DB60);
REX_HOOK_RAW(sub_8213DB60) {
  if(!edf::native::NativeConstantOwnership::OwnsRenderWords(ctx.r3.u32,ctx.r5.u32,ctx.r6.u32,ctx.r4.u64)) {
    __imp__sub_8213DB60(ctx,base);
    return;
  }
  // Pure packet copy in the audited native CPU-tail render-state path.
  // Preserve all CPU source words; the original caller clears its dirty mask.
  // Unknown banks/extents and all nonnative draws continue through the encoder.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native render ownership: omitted Xbox state packet encoding, dirty={:#x}",ctx.r4.u64);
}
REX_EXTERN(__imp__sub_8213DDA0);
REX_EXTERN(__imp__sub_8213DC20);
REX_HOOK_RAW(sub_8213DC20) {
  if(!edf::native::NativeConstantOwnership::OwnsVectorStatePackets(ctx.r3.u32,ctx.r4.u64)) {
    __imp__sub_8213DC20(ctx,base); return;
  }
  // Preserve CPU plane/control records and the caller's dirty clear. This
  // removes only the redundant GPU packet copy, not native clipping behavior.
  static thread_local uint64_t omitted=0;
  if(++omitted<=3) REXLOG_INFO("Native vector-state ownership: omitted Xbox packet encoding");
}
REX_HOOK_RAW(sub_8213DDA0) {
  if(!edf::native::NativeConstantOwnership::OwnsFetchWords(ctx.r3.u32,ctx.r4.u64)) {
    __imp__sub_8213DDA0(ctx,base);
    return;
  }
  // DDA0 reads device+1024 descriptors and writes only command packets/cursor40.
  // Its caller retains descriptor updates and the device+16 dirty-mask clear.
  // Native-host mode consumes neither the packet payloads nor GPU cache packets,
  // including when native submission failed and was recorded as unsubmitted.
  static thread_local uint64_t removed=0;
  if(++removed<=3)
    REXLOG_INFO("Native fetch ownership: omitted Xbox descriptor packet encoding, dirty={:#x}",ctx.r4.u64);
}
namespace edf::native {
namespace {
// The scene publication, queue, ownership and preload flags a native pass
// drawing published scene state depends on; the first one off, or null.
const char* NativeScenePassMissingFlag() {
  const std::pair<const char*,bool> required[]{
    {"edf_native_frame_dispatch",EDF_NATIVE_FLAG(frame_dispatch)},
    {"edf_native_scene_tree",EDF_NATIVE_FLAG(scene_tree)},
    {"edf_native_scene_tree_published",EDF_NATIVE_FLAG(scene_tree_published)},
    {"edf_native_scene_queued",EDF_NATIVE_FLAG(scene_queued)},
    {"edf_native_scene_visibility",EDF_NATIVE_FLAG(scene_visibility)},
    {"edf_native_scene_sources_owned",EDF_NATIVE_FLAG(scene_sources_owned)},
    {"edf_native_scene_membership_owned",EDF_NATIVE_FLAG(scene_membership_owned)},
    {"edf_native_scene_selection_owned",EDF_NATIVE_FLAG(scene_selection_owned)},
    {"edf_native_scene_camera_owned",EDF_NATIVE_FLAG(scene_camera_owned)},
    {"edf_native_scene_geometry_owned",EDF_NATIVE_FLAG(scene_geometry_owned)},
    {"edf_native_scene_material_owned",EDF_NATIVE_FLAG(scene_material_owned)},
    {"edf_native_scene_preload",EDF_NATIVE_FLAG(scene_preload)},
    {"edf_native_scene_group_order",EDF_NATIVE_FLAG(scene_group_order)}};
  for(const auto& [name,enabled]:required) if(!enabled) return name;
  return nullptr;
}
}
bool NativeStaticWorldPassEnabled() {
  if(!EDF_NATIVE_FLAG(static_world_pass)) return false;
  if(const auto* name=NativeScenePassMissingFlag()) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_INFO("Native static world pass disabled: requires {} (original group traversal retained)",name);
    return false;
  }
  return true;
}
bool NativeModelPassEnabled() {
  if(!EDF_NATIVE_FLAG(model_pass)) return false;
  const char* name=!EDF_NATIVE_FLAG(model_publication)?"edf_native_model_publication":
    !EDF_NATIVE_FLAG(shader_bridge)?"edf_native_shader_bridge":NativeScenePassMissingFlag();
  if(name) {
    static std::atomic<bool> reported=false;
    if(!reported.exchange(true))
      REXLOG_INFO("Native model pass disabled: requires {} (original model draws retained)",name);
    return false;
  }
  return true;
}
namespace {
void LogNativeStaticWorldGroup(uint32_t group,uint64_t recorded) {
  // Same line and sampling as the guest group hook; a native group enters no boundary.
  static std::atomic<uint64_t> shapes=0;
  const auto count=++shapes;
  if(count<=4 || !(count&(count-1)))
    REXLOG_INFO("Native static group execution: group={:#x} completed=true recorded_batches={} compatibility_calls=0 boundary_mask={:#x} occurrences={}",
      group,recorded,0u,count);
}
// Handoff bind effects of one owed group, keyed like the group material cache:
// the published group material (its program and constants generation; held,
// so neither address is reused while cached), for this instance and device.
// Built once per key; unreadable effects return null and the group binds
// through ActivateNativeMaterial, which reports the failure as before.
std::shared_ptr<const NativeStaticWorldBindEffects> NativeStaticWorldBindEffectsFor(const GuestReader& reader,
    uint32_t instance,uint32_t device,const std::shared_ptr<const NativeSceneGroupMaterial>& material) {
  if(!material) return nullptr;
  struct Entry {
    std::shared_ptr<const NativeSceneGroupMaterial> material;
    std::shared_ptr<const NativeSceneMaterialProgram> program;
    std::shared_ptr<const NativeStaticWorldBindEffects> effects;
  };
  static std::mutex mutex;
  static std::map<std::pair<uint32_t,uint32_t>,Entry> cache;
  const std::pair key{instance,device};
  {
    std::lock_guard lock(mutex);
    const auto found=cache.find(key);
    if(found!=cache.end() && found->second.material==material && found->second.program==material->program)
      return found->second.effects;
  }
  std::shared_ptr<const NativeStaticWorldBindEffects> effects;
  try { effects=std::make_shared<const NativeStaticWorldBindEffects>(ReadNativeStaticWorldBindEffects(reader,instance,device)); }
  catch(const std::exception&) { return nullptr; }
  std::lock_guard lock(mutex);
  if(cache.size()>=16384) cache.clear();
  cache.insert_or_assign(key,Entry{material,material->program,effects});
  return effects;
}
// Retirement allocation and legacy tag of a composed bind, from the frames the
// setters get under ActivateNativeMaterial's 144-byte frame below `frame`: the
// texture setter's 160 bytes (tag at its r1-80), the shader setters' 128 (r1-48).
uint32_t ReserveNativeHandoffRetirement(const PPCContext& frame,uint8_t* base,uint32_t device,NativeStaticWorldRetirement kind) {
  const bool texture=kind==NativeStaticWorldRetirement::Texture;
  const uint32_t below=144+(texture?160:128);
  auto work=frame;
  if(work.r1.u32<below) throw std::runtime_error("invalid native handoff retirement stack");
  work.r1.u64=work.r1.u32-below; work.r3.u64=device;
  work.lr=texture?0x8213BBE0:kind==NativeStaticWorldRetirement::Pixel?0x82149664:0x82149924;
  EnterNativeSceneBoundary(NativeSceneBoundary::RetirementAllocation);
  sub_82141440(work,base);
  return work.r3.u32;
}
uint32_t NativeHandoffRetirementTag(const GuestReader& reader,const PPCContext& frame,NativeStaticWorldRetirement kind) {
  const uint32_t below=144+(kind==NativeStaticWorldRetirement::Texture?80:48);
  if(frame.r1.u32<below) throw std::runtime_error("invalid native handoff retirement tag");
  return reader.Word(frame.r1.u32-below);
}
}
// Replaces 821C3BB8(owner+240) for one world owner. Native groups draw from the
// scene publication with explicit pass inputs; the device state they owe is
// handed off before any guest group and at the end, so later stages inherit
// what the sequential guest groups would have left. Guest calls run unlocked:
// the bridge locks are taken once per contiguous run of native groups and
// released before every handoff (its activations run guest code) and every
// fallback group. A group's pass-invariant work - eligibility, the chained
// pass state, the resolved and interned material, its pipeline and world
// binding - comes from NativeStaticWorldGroupCache while its key matches; the
// key's comment lists why every input of that work is compared or signalled.
// Per frame only the instances' worlds, the camera and the queues change.
void RenderNativeStaticWorldPass(PPCContext& ctx,uint8_t* base,uint32_t owner) {
  const GuestReader reader(base);
  static NativeStaticWorldPassCounters counters;
  using GroupCache=NativeStaticWorldGroupCache<NativeStaticPassView,std::optional<NativeStaticGroupMaterial>>;
  // Read and written only under state.mutex.
  static GroupCache cache;
  static NativeSceneInstanceReuse reuse;
  const auto publication=native_scene_publication;
  auto* queues=native_scene_queues;
  const auto order=native_scene_pass_camera?NativeStaticWorldOrder(publication.get(),owner,queues):nullptr;
  if(!order) {
    const auto count=++counters.original;
    if(count<=4 || count%1000==0)
      REXLOG_INFO("Native static world pass: original traversal owner={:#x} count={} (no publication, group order, camera or native queues)",owner,count);
    auto work=ctx; work.r3.u64=reader.Add(owner,240); work.lr=0x820B434C; sub_821C3BB8(work,base);
    return;
  }
  HookTiming timing(HookPhase::RenderQueued);
  auto frame=ctx;
  if(frame.r1.u32<112) throw std::runtime_error("invalid native static world pass stack");
  frame.r1.u64=frame.r1.u32-112;
  reader.StoreWord(frame.r1.u32,ctx.r1.u32);
  const auto device=reader.Word(reader.Add(reader.Word(0x8257BFB4),8));
  auto& state=State();
  // One acquisition per run of native groups (submission gate, then registry).
  std::unique_lock run_submission(state.submissions,std::defer_lock);
  std::unique_lock run_lock(state.mutex,std::defer_lock);
  const auto lock_run=[&] { if(!run_lock.owns_lock()) { run_submission.lock(); run_lock.lock(); } };
  const auto unlock_run=[&] { if(run_lock.owns_lock()) { run_lock.unlock(); run_submission.unlock(); } };
  // Pass state and view come from the device once per pass and again after
  // each guest group; native groups chain it and never read bound state.
  NativeScenePassCursorState cursor;
  const auto load_pass=[&] {
    NativeSceneMaterialPassState pass;
    pass.render=ReadNativeMaterialRenderPass(reader,device);
    for(uint32_t slot=0;slot<16;++slot) pass.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
    std::lock_guard lock(state.mutex);
    cursor=NativeScenePassCursorState{device,pass.Inputs(),ReadNativeDrawViewportWords(reader,device),ActiveTargetsLocked(state)};
  };
  load_pass();
  struct Owed { uint32_t material; std::shared_ptr<const NativeSceneMaterialProgram> program; std::shared_ptr<const NativeSceneGroupMaterial> published; };
  struct World { uint32_t vertex; VertexParameterRange parameter; std::array<uint8_t,64> bytes; };
  std::vector<Owed> owed;
  std::optional<World> world;
  NativeMaterialRenderPass owed_start;
  const auto handoff=[&] {
    // Ends the run: the activations below are guest code.
    unlock_run();
    if(owed.empty()) return;
    HookTiming handoff_timing(HookPhase::QueuedHandoff);
    std::vector<NativeStaticWorldHandoffGroup> groups;
    std::vector<std::shared_ptr<const NativeStaticWorldBindEffects>> effects;
    groups.reserve(owed.size()); effects.reserve(owed.size());
    for(const auto& group:owed) {
      uint32_t slots=0;
      for(const auto& operation:group.program->sampler_operations) slots|=1u<<operation.slot;
      effects.push_back(NativeStaticWorldBindEffectsFor(reader,group.material,device,group.published));
      groups.push_back({group.program->inputs.state_overrides,slots,effects.back().get()});
    }
    auto work=frame;
    const auto replays=counters.replays,binds=counters.binds;
    const auto writes=HandOffNativeStaticWorld(reader,device,owed_start,cursor.material.samplers,groups,[&](size_t index) {
      // 821B94E8 through the activation hook: CPU program, host shader
      // bindings and setter publications, exactly as the guest group would.
      HookTiming replay_timing(HookPhase::QueuedHandoffReplays);
      work.r3.u64=owed[index].material; work.lr=0x821D979C; sub_821B94E8(work,base);
      ++counters.replays;
    },[&](size_t index) {
      // A group without composable effects: its binds and uploads through the
      // activation, so intermediate textures and shaders retire and registers
      // only it wrote hold its values.
      HookTiming bind_timing(HookPhase::QueuedHandoffBinds);
      ActivateNativeMaterial(work,base,owed[index].material,device,false);
      ++counters.binds;
    },[&](NativeStaticWorldRetirement kind) { return ReserveNativeHandoffRetirement(frame,base,device,kind); },
      [&](NativeStaticWorldRetirement kind) { return NativeHandoffRetirementTag(reader,frame,kind); });
    counters.composed+=owed.size()-(counters.replays-replays)-(counters.binds-binds);
    if(writes.render!=cursor.material.render) throw std::runtime_error("native static world handoff diverged from the pass cursor");
    for(const auto offset:writes.operations) {
      const auto setter=NativeMaterialStateSetter(offset);
      if(offset==0x44) {
        std::lock_guard lock(state.mutex);
        state.render_state_snapshots.PublishBlend(device,ReadGuestWords<4>(reader,reader.Add(device,10336)),setter);
      } else if(offset!=0x64) PublishNativeRenderState(base,device,setter);
    }
    {
      std::lock_guard submission(state.submissions);
      std::lock_guard lock(state.mutex);
      if(world) {
        // The last group's final instance owns g_mWorld after its activation defaults.
        auto& shader=state.shaders.at(world->vertex);
        RestoreNativeSceneWorld(reader,device,world->parameter.first,world->bytes,[&](const auto& restored) {
          shader.bindings->PatchGuestFloatRegisters(world->parameter.normal,0,restored);
          if(!shader.reversed_mirrors) shader.reversed_bindings->PatchGuestFloatRegisters(world->parameter.reversed,0,restored);
        });
      }
      state.recorded={}; ++state.bind_generation;
    }
    if(REXCVAR_GET(edf_native_material_state_audit) || REXCVAR_GET(edf_native_material_sampler_audit)) {
      NativeSceneMaterialPassState actual;
      actual.render=ReadNativeMaterialRenderPass(reader,device);
      for(uint32_t slot=0;slot<16;++slot) actual.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
      actual=actual.Inputs();
      if(actual!=cursor.material) {
        REXLOG_ERROR("Native static world handoff mismatch: owner={:#x} render_equal={} samplers_equal={}",
          owner,actual.render==cursor.material.render,actual.samplers==cursor.material.samplers);
        throw std::runtime_error("native static world handoff differs from the pass cursor");
      }
    }
    owed.clear(); world.reset(); ++counters.handoffs;
  };
  const auto report=[](const std::string& reason) {
    static std::set<std::string> reported;
    if(reported.size()<32 && reported.insert(reason).second) REXLOG_INFO("Native static world group declined: {}",reason);
  };
  const auto native=[&](uint32_t group) -> std::optional<NativeStaticWorldFallback> {
    using F=NativeStaticWorldFallback;
    // 821D96D8 drains a non-empty guest queue; an empty one draws nothing.
    if(reader.Word(reader.Add(group,4))!=reader.Word(reader.Add(group,8))) return F::GuestQueue;
    if(!queues->Contains(group)) { ++counters.empty_groups; LogNativeStaticWorldGroup(group,0); return {}; }
    lock_run();
    HookTiming eligibility_timing(HookPhase::QueuedEligibility);
    std::shared_ptr<const NativeSceneGroupMaterial> material;
    std::optional<NativeSceneGeometrySource> setup;
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> retained;
    for(const auto& published:publication->group_materials) if(published->group==group) { material=published; break; }
    if(const auto* source=NativeSceneSourcesForPass(state).FindGroup(group)) {
      const auto latest=state.scene_adapter.GroupGeometry(group,source->revision);
      for(const auto& published:publication->group_geometry)
        if(published==latest && published->group==group && published->setup &&
           published->geometry->backend()==state.scene_backend.get()) { setup=published->setup; retained=published->geometry; break; }
    }
    if(!material || !material->program || !setup) return F::Program;
    const auto& program=*material->program;
    if(!program.CanDeferCpuActivation()) return F::Scissor;
    GroupCache::Key key{.group=group,.device=device,.vertex=program.inputs.vertex,.pixel=program.inputs.pixel,
      .program=material->program,.setup=*setup,.geometry=retained,.backend=state.scene_backend,
      .pass=cursor.material,.view=NativeStaticPassView{*cursor.viewport,cursor.targets},
      .filtering=REXCVAR_GET(edf_native_anisotropic_filtering),.shaders=state.shader_registry_generation};
    // Miss diagnostic: why a group with an entry missed, per component, plus
    // whether the published object or the stack moved (observed, not keyed).
    using M=NativeStaticWorldMiss;
    uint32_t missed=0;
    std::optional<uint32_t> changed_read;
    std::string changed_constant;
    if(const auto* existing=cache.Find(group)) {
      missed=NativeStaticWorldKeyDifferences(existing->key,key);
      if(existing->observed.stack!=frame.r1.u32) missed|=NativeStaticWorldMissBit(M::Stack);
      if(existing->observed.published!=material) missed|=NativeStaticWorldMissBit(M::Published);
    } else missed=NativeStaticWorldMissBit(M::Absent);
    // A matching entry's eligibility holds while every guest byte its
    // assessment recorded still holds what it read and its per-frame witness
    // (stack, fence, retirement slots) holds now; otherwise assess again,
    // recording what this assessment reads.
    auto* candidate=cache.Candidate(key);
    if(candidate && !candidate->reads.Unchanged(reader)) {
      missed|=NativeStaticWorldMissBit(M::Reads); changed_read=candidate->reads.FirstChange(reader); candidate=nullptr;
    } else if(candidate && !candidate->eligibility.Holds(reader,frame.r1.u32)) {
      missed|=NativeStaticWorldMissBit(M::Witness); candidate=nullptr;
    }
    NativeRecordedReads reads;
    NativeStaticEligibilityWitness witness;
    if(!candidate && AssessNativeStaticGroup(NativeRecordingReader(reader,reads),device,frame.r1.u32,*setup,&witness)!=
       NativeStaticGroupEligibility::Supported) return F::Eligibility;
    eligibility_timing.Finish();
    HookTiming resolve_timing(HookPhase::QueuedResolve);
    NativeSceneMaterialPassState next;
    auto constants=material->constants;
    try {
      // After and its decode read only the incoming pass and the program.
      if(candidate) next=candidate->next;
      else { next=cursor.material.After(program); DecodeNativeRenderState(next.render.words); }
      for(auto& constant:constants) {
        if(native_scene_pass_camera->Apply(constant) || !constant.global ||
           (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
        if(!native_scene_pass_animation) throw std::runtime_error("missing native animation pass");
        native_scene_pass_animation->Apply(constant);
      }
    } catch(const std::exception& error) { report(error.what()); return F::PassState; }
    // The entry's material is this pass's when the constants match but for
    // those feeding only the camera, which is derived from them into its
    // capture. A miss resolves into fresh with the first instance.
    std::optional<NativeStaticGroupMaterial> fresh;
    auto* shared=&fresh;
    M stale=M::Constants;
    size_t differed=0;
    if(candidate && candidate->material && GroupCache::Current(*candidate,constants,
       candidate->material->resolved.capture.material.get(),candidate->material->resolved.capture.camera,&stale,&differed)) {
      shared=&candidate->material; ++cache.hits;
    } else {
      ++cache.misses;
      if(candidate) {
        missed|=NativeStaticWorldMissBit(stale);
        if(differed<constants.size()) changed_constant=constants[differed].name;
      }
      const auto events=cache.Missed(missed);
      if(events<=4 || events%10000==0) {
        std::string reasons;
        for(size_t i=0;i<kNativeStaticWorldMissNames.size();++i)
          if(missed>>i&1) { if(!reasons.empty()) reasons+=','; reasons+=kNativeStaticWorldMissNames[i]; }
        std::string read="-";
        if(changed_read) read=*changed_read-device<0x4000?std::format("device+{:#x}",*changed_read-device):std::format("{:#x}",*changed_read);
        REXLOG_INFO("Native static world cache miss: events={} group={:#x} reasons=[{}] read={} constant={} totals: {}",
          events,group,reasons,read,changed_constant.empty()?std::string("-"):changed_constant,cache.MissSummary());
      }
    }
    resolve_timing.Finish();
    // Resolve every instance before recording: a decline returns the whole
    // group, with its selections restored, to the guest callback.
    const auto instances=queues->Take(group);
    uint64_t batches=0;
    std::vector<NativeStaticInstanceWorld> resolved;
    NativeStaticGroupResolution resolution;
    const NativeStaticPassInputs pass{device,constants,cursor.material.render,cursor.material.samplers,
      NativeStaticPassView{*cursor.viewport,cursor.targets}};
    {
      HookTiming instances_timing(HookPhase::QueuedInstances);
      // One group resolve: its instances share geometry, program and these
      // pass inputs, so they share one material and view and record as
      // instanced draws. Per instance only its source, world and object.
      try {
        const auto decline=ResolveNativeStaticGroupInstances(instances,resolved,[&] {
          resolution=ResolveNativePublishedStaticGroupLocked(state,reader,*publication,
            {group,setup->count,material,setup},instances,pass,nullptr,*shared,true);
          return resolution.decline;
        },[&](uint32_t instance) {
          return ResolveNativePublishedStaticInstanceWorldLocked(state,reader,*publication,resolution,instance,device,&reuse);
        });
        if(const auto* reason=NativeStaticWorldDeclineReason(decline)) report(reason);
      } catch(const std::exception& error) { report(error.what()); resolved.clear(); }
    }
    if(resolved.size()!=instances.size() || !resolution.material) {
      for(auto at=instances.rbegin();at!=instances.rend();++at) queues->Push(group,*at);
      return F::Instance;
    }
    // A miss resolved under exactly this key (its geometry is the one the
    // instances resolved against). The eligibility reads are this frame's, or
    // the candidate's, just revalidated.
    if(shared==&fresh && fresh && fresh->geometry==retained.get()) {
      const auto captured=fresh->resolved.capture.material;
      const auto camera=fresh->resolved.capture.camera;
      if(candidate) { reads=std::move(candidate->reads); witness=std::move(candidate->eligibility); }
      auto& stored=cache.Store(std::move(key),constants,std::move(reads),std::move(witness),next,std::move(fresh),captured.get(),camera,
        {frame.r1.u32,material});
      // fresh moved into the entry: the resolution's material is the entry's.
      resolution.material=&*stored.material;
    }
    NativeQueuedSceneGroup batch;
    batch.targets=cursor.targets;
    batch.objects.reserve(resolved.size());
    const auto draws=state.scene_native_draws;
    // The shared capture gives every instance one view, so this is one flush.
    try {
      HookTiming record_timing(HookPhase::QueuedRecord);
      batch.view=resolution.view;
      for(const auto& result:resolved) batch.objects.push_back(result.object);
      FlushNativeQueuedSceneLocked(state,batch);
    }
    catch(const std::exception& error) {
      // Nothing of this group is owed to the guest yet: hand its selections back.
      report(error.what());
      ++state.bind_generation; state.recorded={};
      for(auto at=instances.rbegin();at!=instances.rend();++at) queues->Push(group,*at);
      return F::Instance;
    }
    // Each flush already advanced bind_generation and cleared recorded.
    batches=batch.execution.recordings();
    counters.draws+=state.scene_native_draws-draws;
    // Keep the guest path's lookup hint current with what this frame drew.
    state.scene_adapter.RememberGroupMaterial(group,resolution.material->resolved.capture.material);
    world=World{material->program->inputs.vertex,*resolution.material->world_parameter,resolved.back().world};
    if(owed.empty()) owed_start=cursor.material.render;
    owed.push_back({setup->material,material->program,material});
    cursor.material=std::move(next);
    ++counters.native_groups; counters.instances+=instances.size(); counters.batches+=batches;
    LogNativeStaticWorldGroup(group,batches);
    return {};
  };
  const auto fallback=[&](uint32_t group,NativeStaticWorldFallback reason) {
    unlock_run();
    const auto count=++counters.fallbacks[size_t(reason)];
    if(count<=4 || !(count&(count-1)))
      REXLOG_INFO("Native static world fallback: group={:#x} reason={} count={}",group,kNativeStaticWorldFallbackNames[size_t(reason)],count);
    NativeMaterialPassCursor guest;
    struct RestoreMaterialPass {
      NativeMaterialPassCursor* previous=native_material_pass_cursor;
      ~RestoreMaterialPass() { native_material_pass_cursor=previous; }
    } restore;
    native_material_pass_cursor=REXCVAR_GET(edf_native_scene_pass_owned)?&guest:nullptr;
    auto work=frame; work.r3.u64=group; work.lr=0x821C3C04; sub_821D96D8(work,base);
    load_pass();
  };
  WalkNativeStaticWorld(*order,native,fallback,handoff);
  uint64_t hits=0,misses=0,stores=0;
  size_t entries=0;
  lock_run();
  cache.EndPass(); reuse.EndPass();
  hits=cache.hits; misses=cache.misses; stores=cache.stores; entries=cache.size();
  const auto reused=reuse.reuses,allocated=reuse.allocations;
  unlock_run();
  const auto passes=++counters.passes;
  if(passes<=4 || passes%1000==0) {
    uint64_t fallbacks=0;
    for(const auto count:counters.fallbacks) fallbacks+=count;
    const auto& f=counters.fallbacks;
    REXLOG_INFO("Native static world pass: passes={} native_groups={} empty_groups={} instances={} draws={} batches={} fallback_groups={} "
      "guest_queue={} program={} scissor={} eligibility={} pass_state={} instance={} handoffs={} replays={} binds={} composed={} original={} "
      "cache_hits={} cache_misses={} cache_stores={} cache_entries={} instance_reuses={} instance_allocations={}",
      passes,counters.native_groups,counters.empty_groups,counters.instances,counters.draws,counters.batches,fallbacks,
      f[0],f[1],f[2],f[3],f[4],f[5],counters.handoffs,counters.replays,counters.binds,counters.composed,counters.original,
      hits,misses,stores,entries,reused,allocated);
  }
}
// Material program of one model pass record (Bridge::model_pass_loads, the
// static group material shape), shared by the model pass and the full frame's
// Models and Sky passes: the program is rebuilt only when its recorded program
// bytes or host identities changed; constant values are re-read at every use,
// since the guest activation uploads them live. That includes the pass-owned
// camera and animation globals (the static pass substitutes its published
// camera; a model draw uploads whatever the globals hold now), and excludes
// g_mWorld, which each record supplies, and with skip_palette g_mWorldArray,
// which a palette object packs from its pose. Failures retry once per tick.
std::shared_ptr<const NativeSceneGroupMaterial> NativeModelPassProgramLocked(Bridge& state,const NativeSceneCpuWindow<GuestReader>& window,
    uint32_t pass,bool skip_palette,const std::function<void(const std::string&)>& report) {
  const auto refresh=[&](Bridge::ModelPassLoad& load) {
    const auto& schema=*load.schema;
    const auto& published=load.published->constants;
    if(published.size()!=load.layout.size()) throw std::runtime_error("native model material constants do not match their layout");
    std::optional<std::vector<NativeSceneMaterialInputs::Constant>> constants;
    for(size_t i=0;i<load.layout.size();++i) {
      const auto& slot=load.layout[i];
      if(published[i].global && (published[i].name=="g_mWorld" || (skip_palette && published[i].name=="g_mWorldArray"))) continue;
      const auto* data=ReadNativeSceneMaterialConstant(window,schema,slot);
      const auto& old=published[i].registers;
      if(old.size()==slot.bytes && std::equal(old.begin(),old.end(),data)) continue;
      if(!constants) constants=published;
      (*constants)[i].registers.assign(data,data+slot.bytes);
    }
    if(!constants) return;
    auto material=std::make_shared<NativeSceneGroupMaterial>(*load.published);
    material->constants=std::move(*constants);
    load.published=std::move(material);
    ++state.model_pass_constants;
  };
  auto& load=state.model_pass_loads[pass];
  if(load.published && NativeSceneMaterialHostCurrent(state,*load.published->program,pass,load.schema) &&
     load.reads.Unchanged(window)) {
    // A failed refresh rebuilds below: only a rebuild failure, which returns
    // no program, is the caller's to report (and to count as a decline).
    try { refresh(load); ++state.model_pass_reused; return load.published; }
    catch(const std::exception& error) {
      static std::set<std::string> noted;  // Under the bridge mutex.
      if(noted.size()<32 && noted.insert(error.what()).second)
        REXLOG_INFO("Native model pass program refresh failed, rebuilding: {}",error.what());
    }
  }
  if(!load.published && load.failed_tick==native_render_budget.tick) return nullptr;
  try {
    NativeRecordedReads reads;
    const NativeRecordingReader recorder(window,reads);
    auto build=BuildNativeSceneMaterialLocked(state,recorder,window,pass,load.published?load.published->program.get():nullptr);
    auto material=std::make_shared<NativeSceneGroupMaterial>();
    material->group=pass;
    material->program=build.reused?load.published->program:std::move(build.program);
    material->constants=std::move(build.constants);
    load.schema=std::move(build.schema); load.layout=std::move(build.layout); load.published=std::move(material);
    load.reads=std::move(reads); load.failed_tick=UINT64_MAX;
    ++state.model_pass_loaded;
    return load.published;
  } catch(const std::exception& error) {
    load.published.reset(); load.failed_tick=native_render_budget.tick;
    report(error.what());
    return nullptr;
  }
}
// The geometry source of one model batch under one pass record: as
// native_scene_geometry.h, pass=*(material+108), shader=**pass.
NativeSceneGeometrySource NativeModelGeometrySource(const GuestReader& reader,const NativeModelBatchLayout& batch,uint32_t pass) {
  return NativeSceneGeometrySource{batch.vertex.owner,batch.index.owner,batch.declaration,batch.stride,batch.draw_count,
    pass,reader.Word(reader.Word(reader.Word(reader.Add(pass,108))))};
}
// Retained geometry of one batch under one vertex shader
// (Bridge::model_geometry_loads), reloaded through the guarded observed-set
// copy, as the static preload does, when changed. Null when its buffers are
// not the batch's published generations or its shader is not registered.
std::shared_ptr<const NativeIndexedMesh::RetainedDraw> NativeModelGeometryLocked(Bridge& state,const GuestReader& reader,
    const NativeSceneGeometrySource& source,const NativeModelBatchLayout& batch) {
  const auto* vb=state.model_buffers.Find(source.vertex,NativeModelBuffers::Kind::Vertex);
  const auto* ib=state.model_buffers.Find(source.index,NativeModelBuffers::Kind::Index);
  if(!vb || !ib || !vb->physical || !ib->physical || vb->generation!=batch.vertex.generation ||
     ib->generation!=batch.index.generation || vb->stride!=source.stride || !vb->bytes || !ib->bytes) return nullptr;
  const auto registered=state.shaders.find(source.shader);
  if(registered==state.shaders.end() || !registered->second.bindings) return nullptr;
  const auto declaration=state.declarations.Get(source.declaration);
  const auto& shader=registered->second.bindings->shader();
  const auto index_contents=ib->index_contents?ib->index_contents:(ib->index_storage?ib->index_storage->SourceSnapshot():nullptr);
  NativeBufferWrites::SnapshotPolicy policy{};
  policy.audit_revisions=REXCVAR_GET(edf_native_retirement_audit);
  if(!policy.audit_revisions && state.mesh_watch_audit.expired()) {
    policy.verify_interval=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_interval),0,1<<20));
    policy.verify_initial=uint64_t(std::clamp(REXCVAR_GET(edf_native_geometry_verify_initial),0,1<<16));
  }
  using View=NativeBufferWrites::SnapshotIdentityView;
  const auto versions=BufferWrites().TryValidateObservedSet(std::array<View,2>{{
    {source.vertex,*vb->physical,vb->bytes,&vb->vertex_contents},
    {source.index,*ib->physical,ib->bytes,&index_contents}}},policy);
  auto& load=state.model_geometry_loads[{batch.address,source.shader}];
  const bool same=load.geometry && load.source==source && load.vertex_generation==vb->generation &&
    load.index_generation==ib->generation && load.declaration==declaration &&
    load.shader.Get()==shader.bytecode.Get() && load.geometry->backend()==state.scene_backend.get();
  const auto same_versions=[&](const std::array<NativeBufferWrites::ObservedVersion,2>& observed) {
    for(size_t i=0;i<2;++i)
      if(observed[i].lifetime!=load.versions[i].lifetime || observed[i].revision!=load.versions[i].revision) return false;
    return true;
  };
  if(versions && same && same_versions(*versions)) return load.geometry;
  using Snapshots=std::array<NativeBufferWrites::ObservedSnapshot,2>;
  std::optional<Snapshots> observed;
  if(versions) observed=Snapshots{{
    {(*versions)[0],vb->vertex_contents,false,true,false},
    {(*versions)[1],index_contents,false,true,false}}};
  else {
    using Source=NativeBufferWrites::SnapshotSource;
    observed=BufferWrites().CopyObservedSet(std::array<Source,2>{{
      {source.vertex,*vb->physical,{reader.Bytes(vb->address,vb->bytes),vb->bytes},vb->vertex_contents},
      {source.index,*ib->physical,{reader.Bytes(ib->address,ib->bytes),ib->bytes},index_contents}}},nullptr,policy);
  }
  if(!observed) return nullptr;
  if(same && (*observed)[0].contents==vb->vertex_contents && (*observed)[1].contents==index_contents &&
     same_versions({(*observed)[0].version,(*observed)[1].version})) return load.geometry;
  const auto vertex_generation=vb->generation,index_generation=ib->generation;
  auto geometry=RetainNativeSceneGeometryLocked(state,source,*vb,*ib,declaration,shader,*observed);
  load=Bridge::ModelGeometryLoad{source,vertex_generation,index_generation,
    {(*observed)[0].version,(*observed)[1].version},declaration,shader.bytecode,geometry};
  ++state.model_geometry_loaded;
  return geometry;
}
// Replaces one 821C9C20 object (rigid path: per record 821A17D8 then 821B2C28,
// which binds stream/declaration/indices per batch and runs 821B94E8 +
// 821FE358 per material pass; with edf_native_model_pass_skinned also the
// palette path: 821A1738 once, then 821A17D8 only for records with rec+48
// clear). Everything is resolved from the layout and
// pose publications, the per-pass-record material programs and retained
// batch geometry before anything is recorded; a decline leaves guest and
// device state untouched and returns false so the caller runs the original.
// After recording, the object's guest-visible CPU effects are handed off the
// way the static world pass hands off its groups.
bool RenderNativeModelPass(PPCContext& ctx,uint8_t* base,const std::vector<NativePoseMatrix>* interpolated,
    bool interpolating,bool render_dependent) {
  using D=NativeModelPassDecline;
  static NativeModelPassCounters counters;
  const GuestReader reader(base);
  const auto instance=ctx.r3.u32,vector=ctx.r4.u32;
  ++counters.objects;
  const auto summary=[&] {
    if(counters.objects>4 && counters.objects%1000) return;
    std::string reasons;
    for(size_t i=0;i<counters.fallbacks.size();++i)
      if(counters.fallbacks[i]) reasons+=std::format(" {}={}",kNativeModelPassDeclineNames[i],counters.fallbacks[i]);
    REXLOG_INFO("Native model pass: objects={} native={} draws={} passes={} handoffs={} replays={} fallbacks={}{}",
      counters.objects,counters.native,counters.draws,counters.passes,counters.handoffs,counters.replays,
      counters.Fallbacks(),reasons);
  };
  const auto report=[](const std::string& reason) {
    static std::set<std::string> reported;
    if(reported.size()<32 && reported.insert(reason).second) REXLOG_INFO("Native model pass declined: {}",reason);
  };
  const auto decline=[&](D reason) {
    const auto count=++counters.fallbacks[size_t(reason)];
    if(count<=4 || !(count&(count-1)))
      REXLOG_INFO("Native model pass fallback: instance={:#x} reason={} count={}",instance,kNativeModelPassDeclineNames[size_t(reason)],count);
    summary();
    return false;
  };
  // Publication gate: rigid (or palette-skinned when enabled), layout current,
  // pose published for this tick and not render-dependent.
  auto& models=::ModelPublications();
  const auto published=models.Find(instance);
  if(published && render_dependent) models.MarkRenderDependent(instance,published.generation);
  const auto identity=ReadGuestWords<2>(reader,instance);
  const auto poses=models.AcquirePoses();
  const auto gate=GateNativeModelPass(published,published && models.Current(instance,identity[0],identity[1],vector),
    reader.Bytes(reader.Add(instance,12),1)[0],poses.get(),native_render_budget.tick,
    render_dependent || (published && models.RenderDependent(instance,published.generation)),
    REXCVAR_GET(edf_native_model_pass_skinned));
  if(!gate) return decline(*gate.decline);
  if(interpolating && (!interpolated || interpolated->size()!=gate.pose->matrices.size())) return decline(D::Pose);
  const auto& matrices=interpolating?*interpolated:gate.pose->matrices;
  const auto& layout=*published.layout;
  const auto plan=NativeModelDrawPlan(layout);
  // 821A17D8(*(*0x8257C02C+32)): a null parameter uploads nothing, which the
  // native world cannot represent.
  const auto parameter=reader.Word(reader.Add(reader.Word(0x8257C02C),32));
  const auto world_storage=parameter?reader.Word(parameter):0;
  if(!world_storage) return decline(D::World);
  // Palette path: 821A1738(*(*0x8257C02C+36)) packs the pose into the scratch
  // at descriptor+0, clamped to descriptor+16, before the first record. A null
  // descriptor (the guest skips the pack) or storage is not represented.
  // Records that do not upload their bone draw with the g_mWorld storage as
  // it stands, so its entry bytes seed the world plan.
  uint32_t palette_storage=0,palette_limit=0;
  std::array<uint8_t,64> world_entry{};
  if(layout.skinned) {
    const auto descriptor=reader.Word(reader.Add(reader.Word(0x8257C02C),36));
    palette_storage=descriptor?reader.Word(descriptor):0;
    if(!palette_storage) return decline(D::Palette);
    palette_limit=reader.Word(reader.Add(descriptor,16));
    try {
      const auto count=NativeBonePaletteCount(uint32_t(matrices.size()),palette_limit);
      if(count) reader.WritableBytes(palette_storage,size_t(count)*kNativeBonePaletteBytes,4);
      std::memcpy(world_entry.data(),reader.Bytes(world_storage,64),64);
    } catch(const std::exception& error) { report(error.what()); return decline(D::Palette); }
  }
  std::vector<std::array<uint8_t,64>> worlds;
  try { worlds=NativeModelWorldPlan(layout,matrices,world_entry); }
  catch(const std::exception& error) { report(error.what()); return decline(D::World); }
  const auto device=reader.Word(reader.Add(reader.Word(0x8257BFB4),8));
  // The 821C9C20 and 821B2C28 frames the setters and activations run under.
  if(ctx.r1.u32<4096+128+176) return decline(D::Eligibility);
  const uint32_t stack=ctx.r1.u32-128-176;
  const auto batch_of=[&](const NativeModelDraw& draw) -> const NativeModelBatchLayout& {
    return layout.meshes[draw.mesh].batches[draw.batch];
  };
  const auto source_of=[&](const NativeModelDraw& draw) { return NativeModelGeometrySource(reader,batch_of(draw),draw.pass); };
  // Static-group eligibility per pass: the same descriptor shape and CPU
  // activation contract. Guest reads only.
  try {
    for(const auto& draw:plan)
      if(const auto result=AssessNativeStaticGroup(reader,device,stack,source_of(draw));
         result!=NativeStaticGroupEligibility::Supported) {
        static std::array<std::atomic<uint64_t>,8> reasons{};
        const auto count=++reasons.at(size_t(result)&7);
        if(count<=4 || !(count&(count-1)))
          REXLOG_INFO("Native model pass eligibility: reason={} occurrences={}",uint32_t(result),count);
        return decline(D::Eligibility);
      }
  } catch(const std::exception& error) { report(error.what()); return decline(D::Eligibility); }
  // Pass inputs at object entry: nothing runs between here and the original's
  // first activation, and each pass chains the state its predecessor left.
  NativeSceneMaterialPassState start;
  try {
    start.render=ReadNativeMaterialRenderPass(reader,device);
    for(uint32_t slot=0;slot<16;++slot) start.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
    start=start.Inputs();
  } catch(const std::exception& error) { report(error.what()); return decline(D::PassState); }
  auto& state=State();
  std::vector<std::shared_ptr<const NativeSceneGroupMaterial>> materials;
  std::vector<NativeStaticWorldHandoffGroup> owed;
  NativeSceneMaterialPassState cursor=start;
  {
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    if(!state.initialized) return decline(D::Scene);
    if(BufferWrites().Pending()) return decline(D::Pending);
    if(!state.active_scene || state.active_target || !state.scenes.contains(state.active_scene)) return decline(D::Scene);
    const auto targets=ActiveTargetsLocked(state);
    if(!targets.count) return decline(D::Scene);
    EnsureSceneBackendLocked(state);
    const auto viewport=DecodeDrawViewport(ReadNativeDrawViewportWords(reader,device));
    const NativeSceneCpuWindow window(reader);
    if(state.model_pass_loads.size()>8192) state.model_pass_loads.clear();
    if(state.model_geometry_loads.size()>8192) state.model_geometry_loads.clear();
    // Program and geometry of each pass record (NativeModelPassProgramLocked,
    // NativeModelGeometryLocked, shared with the full frame).
    const auto program_of=[&](uint32_t pass) { return NativeModelPassProgramLocked(state,window,pass,layout.skinned,report); };
    const auto geometry_of=[&](const NativeSceneGeometrySource& source,const NativeModelBatchLayout& batch) {
      return NativeModelGeometryLocked(state,reader,source,batch);
    };
    struct Resolved { std::shared_ptr<const NativeSceneInstance> object; NativeSceneView view; };
    std::vector<Resolved> resolved;
    resolved.reserve(plan.size());
    static uint64_t ids=0;
    for(const auto& draw:plan) {
      const auto source=source_of(draw);
      const auto material=program_of(draw.pass);
      if(!material || !material->program) return decline(D::Program);
      const auto& program=*material->program;
      if(!program.CanDeferCpuActivation()) return decline(D::Scissor);
      std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
      try { geometry=geometry_of(source,batch_of(draw)); }
      catch(const std::exception& error) { report(error.what()); }
      if(!geometry || geometry->backend()!=state.scene_backend.get()) return decline(D::Geometry);
      // g_mWorldArray of a palette object: the pass's global must name the
      // scratch 821A1738 packed, and the shader's array (the slot's reflected
      // bytes) must hold every packed bone; the tail keeps the live scratch.
      const std::vector<NativeSceneMaterialInputs::Constant>* constants=&material->constants;
      std::vector<NativeSceneMaterialInputs::Constant> palette_constants;
      if(layout.skinned) {
        const auto& load=state.model_pass_loads.at(draw.pass);
        for(size_t i=0;i<material->constants.size();++i) {
          const auto& constant=material->constants[i];
          if(constant.name!="g_mWorldArray") continue;
          if(!constant.global || constant.pixel || !load.schema || i>=load.layout.size()) return decline(D::Palette);
          std::optional<std::vector<uint8_t>> registers;
          try {
            const auto& schema=*load.schema;
            const auto& slot=load.layout[i];
            if(slot.group!=1 || slot.parameter>=schema[1].size() || schema[1][slot.parameter].ReadValue(window,true).data!=palette_storage)
              return decline(D::Palette);
            const auto* live=ReadNativeSceneMaterialConstant(window,schema,slot);
            registers=NativeModelPaletteRegisters(matrices,palette_limit,{live,slot.bytes});
          } catch(const std::exception& error) { report(error.what()); return decline(D::Palette); }
          if(!registers) return decline(D::Palette);
          if(constants!=&palette_constants) { palette_constants=material->constants; constants=&palette_constants; }
          palette_constants[i].registers=std::move(*registers);
        }
      }
      NativeSceneResolvedMaterial result;
      NativeSceneMaterialPassState next;
      try {
        next=cursor.After(program);
        DecodeNativeRenderState(next.render.words);
        NativeBackendPipelineDesc desc;
        desc.vertex_id=(uint64_t(program.inputs.vertex)<<1)|uint64_t(viewport.reverse_depth);
        desc.pixel_id=program.inputs.pixel;
        desc.input_layout=geometry->input_layout().elements(); desc.input_layout_id=geometry->input_layout().fingerprint();
        desc.render_targets=targets.count; desc.rtv_format=targets.rtv_format;
        desc.dsv_format=targets.dsv_format; desc.sample_count=targets.samples;
        result=program.Resolve(desc,viewport.reverse_depth,*constants,cursor.render,cursor.samplers,
          REXCVAR_GET(edf_native_anisotropic_filtering),layout.skinned);
      } catch(const std::exception& error) { report(error.what()); return decline(D::PassState); }
      // g_mWorld exactly as 821A17D8 stores it: the record's bone, column-major,
      // or on the palette path, for a record with rec+48 set, what the storage
      // holds. A palette shader need not declare g_mWorld at all.
      try {
        if(!layout.skinned || NativeSceneCaptureBindsWorld(result.capture))
          ApplyNativeScenePublishedWorld(result.capture,worlds[draw.mesh]);
      } catch(const std::exception& error) { report(error.what()); return decline(D::World); }
      auto object=std::make_shared<NativeSceneInstance>();
      object->id=(uint64_t(1)<<62)+ ++ids; object->changed_tick=UINT64_MAX;
      object->object.geometry=std::move(geometry); object->object.material=result.capture.material;
      object->object.world=result.capture.world; object->previous=result.capture.world;
      resolved.push_back({std::move(object),NativeStaticInstanceView(result.capture.camera,viewport,result.render.words[5]!=0)});
      uint32_t slots=0;
      for(const auto& operation:program.sampler_operations) slots|=1u<<operation.slot;
      owed.push_back({program.inputs.state_overrides,slots});
      materials.push_back(material);
      cursor=std::move(next);
    }
    // The handoff's combined state writes must be representable and land on
    // the chained cursor before anything is recorded.
    try {
      if(!owed.empty() && NativeStaticWorldStateHandoff(start.render,owed).render!=cursor.render) return decline(D::PassState);
    } catch(const std::exception& error) { report(error.what()); return decline(D::PassState); }
    // Record in guest order. A partial recording cannot be rolled back; a
    // throw past this point reaches TryNativeModelPass, which runs the original.
    NativeQueuedSceneGroup batch;
    batch.targets=targets;
    const auto draws=state.scene_native_draws;
    try {
      for(auto& item:resolved) {
        if(!batch.objects.empty() && (batch.view.view!=item.view.view || batch.view.projection!=item.view.projection ||
           batch.view.view_projection!=item.view.view_projection || batch.view.scissor_enabled!=item.view.scissor_enabled))
          FlushNativeQueuedSceneLocked(state,batch);
        batch.view=item.view; batch.objects.push_back(std::move(item.object));
      }
      FlushNativeQueuedSceneLocked(state,batch);
    } catch(const std::exception&) {
      // The recorder may hold part of this object's bindings: invalidate its cache.
      ++state.bind_generation; state.recorded={};
      throw;
    }
    ++state.bind_generation;
    counters.draws+=state.scene_native_draws-draws; counters.passes+=plan.size();
  }
  // Device-state handoff: guest calls run unlocked, in guest order.
  auto frame=ctx;
  frame.r1.u64=ctx.r1.u32-128; reader.StoreWord(frame.r1.u32,ctx.r1.u32);
  frame.r1.u64=stack; reader.StoreWord(stack,ctx.r1.u32-128);
  // 1. 821A1738 on the palette path: the scratch holds the packed pose, as the
  // 821A1738 hook stores it. Written, not proven unread: later skinned draws
  // pack before they upload, but the scratch is shared and other readers of
  // it were not audited, and the material replays below upload from it.
  if(layout.skinned) {
    const auto bones=NativeModelPaletteWords(matrices,palette_limit);
    for(uint32_t bone=0;bone<bones.size();++bone)
      reader.StoreCpuWords(reader.Add(palette_storage,bone*kNativeBonePaletteBytes),bones[bone]);
  }
  // 2. 821A17D8 of the last uploading record: g_mWorld's storage holds its bone
  // (on the palette path, unchanged when no record uploads).
  if(const auto uploaded=NativeModelLastWorldUpload(layout))
    reader.StoreCpuWords(world_storage,NativeModelWorldWords(matrices[layout.meshes[*uploaded].bone]));
  // 3. 821B2C28's stream/declaration/index setters for the last batch.
  const NativeModelBatchLayout* last=nullptr;
  for(const auto& mesh:layout.meshes) if(!mesh.batches.empty()) last=&mesh.batches.back();
  if(last) {
    NativeSceneGeometryInstallState progress;
    auto work=frame;
    ::InstallNativeStaticGeometry(work,base,device,NativeSceneGeometrySource{last->vertex.owner,last->index.owner,
      last->declaration,last->stride,last->draw_count,last->passes.empty()?0:last->passes.back(),0},progress);
  }
  // 4. Material activations: the last pass and each sampler slot's last
  // binder through 821B94E8, then every pass's combined render words, dirty
  // masks and sampler words, exactly as for static world groups.
  if(!owed.empty()) {
    auto work=frame;
    // As for static world groups: a pass not replayed still binds and uploads,
    // so its textures and shaders retire in guest order; composed from its
    // cached effects, or through the activation when they are unreadable.
    std::vector<std::shared_ptr<const NativeStaticWorldBindEffects>> effects;
    effects.reserve(owed.size());
    for(size_t index=0;index<owed.size();++index) {
      effects.push_back(NativeStaticWorldBindEffectsFor(reader,plan[index].pass,device,materials[index]));
      owed[index].effects=effects.back().get();
    }
    const auto writes=HandOffNativeStaticWorld(reader,device,start.render,cursor.samplers,owed,[&](size_t index) {
      work.r3.u64=plan[index].pass; work.lr=0x821B2ECC; sub_821B94E8(work,base);
      ++counters.replays;
    },[&](size_t index) {
      ActivateNativeMaterial(work,base,plan[index].pass,device,false);
    },[&](NativeStaticWorldRetirement kind) { return ReserveNativeHandoffRetirement(frame,base,device,kind); },
      [&](NativeStaticWorldRetirement kind) { return NativeHandoffRetirementTag(reader,frame,kind); });
    if(writes.render!=cursor.render) throw std::runtime_error("native model handoff diverged from the pass cursor");
    for(const auto offset:writes.operations) {
      const auto setter=NativeMaterialStateSetter(offset);
      if(offset==0x44) {
        std::lock_guard lock(state.mutex);
        state.render_state_snapshots.PublishBlend(device,ReadGuestWords<4>(reader,reader.Add(device,10336)),setter);
      } else if(offset!=0x64) PublishNativeRenderState(base,device,setter);
    }
    if(REXCVAR_GET(edf_native_material_state_audit) || REXCVAR_GET(edf_native_material_sampler_audit)) {
      NativeSceneMaterialPassState actual;
      actual.render=ReadNativeMaterialRenderPass(reader,device);
      for(uint32_t slot=0;slot<16;++slot) actual.samplers[slot]=ReadNativeMaterialSamplerPass(reader,device,slot);
      actual=actual.Inputs();
      if(actual!=cursor) {
        REXLOG_ERROR("Native model handoff mismatch: instance={:#x} render_equal={} samplers_equal={}",
          instance,actual.render==cursor.render,actual.samplers==cursor.samplers);
        throw std::runtime_error("native model handoff differs from the pass cursor");
      }
    }
  }
  {
    std::lock_guard submission(state.submissions);
    std::lock_guard lock(state.mutex);
    state.recorded={}; ++state.bind_generation;
  }
  ++counters.handoffs; ++counters.native;
  summary();
  return true;
}
}
