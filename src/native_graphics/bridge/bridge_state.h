#pragma once
// The bridge's shared state: the State() singleton (Bridge: locks, registered shaders, targets, scene and
// submission bookkeeping, caches) and the helper types it holds. Moved from guest_shader_bridge.cpp
// unchanged. The accessors (State, BufferWrites, PacingState) and VertexBindingsForDraw are inline here, as
// they were inlined in the bridge's one translation unit before the split: a function-local static in an
// inline function is one object program-wide. A file that includes this shares the one Bridge with the
// bridge; see Bridge for which lock guards what. active_movie_decode is defined in bridge_state.cpp.
#include "hook_timing.h"
#include "../guest_shader_bridge.h"
#include "../guest_mesh_watch_audit.h"
#include "../d3d11_bindings.h"
#include "../d3d11_completion.h"
#include "../d3d11_frame_handoff.h"
#include "../d3d11_gpu_timer.h"
#include "../d3d11_mesh.h"
#include "../d3d11_quads.h"
#include "../d3d11_render_state.h"
#include "../d3d11_signals.h"
#include "../d3d11_texture.h"
#include "../native_backend_frame_queue.h"
#include "../native_buffer_writes.h"
#include "../native_canvas_constants.h"
#include "../native_constant_cache.h"
#include "../native_contract_ledger.h"
#include "../native_d3d12_preview.h"
#include "../native_declarations.h"
#include "../native_display_gamma.h"
#include "../native_font_bindings.h"
#include "../native_frame_flight.h"
#include "../native_fsr.h"
#include "../native_generated_indices.h"
#include "../native_immediate_classify.h"
#include "../native_material_parameters.h"
#include "../native_model_buffers.h"
#include "../native_motion_vectors.h"
#include "../native_movie_bindings.h"
#include "../native_pacing.h"
#include "../native_recorded_reads.h"
#include "../native_render_backend.h"
#include "../native_render_state_decode.h"
#include "../native_render_state_snapshot.h"
#include "../native_sampler_decode.h"
#include "../native_scene.h"
#include "../native_scene_adapter.h"
#include "../native_scene_geometry.h"
#include "../native_scene_material.h"
#include "../native_scene_membership.h"
#include "../native_scene_sources.h"
#include "../native_scene_static_walk.h"
#include "../native_shader_state.h"
#include "../native_submission_cursors.h"
#include "../native_xui_bindings.h"
#include <wrl/client.h>
#include <d3d11.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace edf::native {
struct VertexParameterRange {
  std::string name;
  uint32_t first,count;
  ShaderBindings::FloatRegisterBinding normal,reversed;
};
struct RegisteredShader {
  uint32_t owner;
  std::unique_ptr<ShaderBindings> bindings;
  std::unique_ptr<QuadStream> quads;
  std::unique_ptr<ShaderBindings> reversed_bindings;
  std::unique_ptr<QuadStream> reversed_quads;
  uint64_t source_fingerprint=0;
  // Whether the reversed-depth variant reflects the same constant buffers as
  // the normal one, so its bytes can be mirrored from it at draw time instead
  // of every material and instance being uploaded into both.
  bool reversed_mirrors=false;
  // The last raw _g_DX2DScale/_g_DX2DOffset this Utility shader uploaded
  // (guest bytes, before any canvas mapping): a Utility draw on a target that
  // is not 16:9 re-maps them through its own canvas layout
  // (native_display_layout.h). Bit 0 scale, bit 1 offset.
  std::array<uint8_t,16> canvas_scale{},canvas_offset{};
  uint8_t canvas_uploaded=0;
  struct ParameterBinding {
    ShaderBindings::FloatRegisterBinding binding;
    bool canvas_xy=false;
  };
  struct ParameterUpload {
    size_t index=0;
    std::array<const ShaderBindings::FloatRegisterBinding*,2> targets{};
    std::array<size_t,2> sizes{};
    size_t maximum=0;
    bool canvas_xy=false;
  };
  struct UploadPlan {
    std::vector<ParameterUpload> parameters;
    uint64_t optimized_out=0;
    bool ready=false;
  };
  struct ParameterPlan {
    std::array<std::vector<ParameterBinding>,4> groups;
    std::array<bool,4> ready{};
    std::array<std::vector<ShaderBindings::ResourceBinding>,2> textures;
    std::array<bool,2> textures_ready{};
    std::shared_ptr<const std::vector<VertexParameterRange>> vertex_ranges;
    // Normal-only and normal+reversed uploads use different active sets.
    std::array<std::array<UploadPlan,4>,2> uploads;
  };
  using ParameterOwner=std::weak_ptr<const NativeMaterialParameters::Groups>;
  std::array<std::map<ParameterOwner,ParameterPlan,std::owner_less<ParameterOwner>>,2> parameter_plans;
  std::array<ParameterOwner,2> last_plan_owner;
  std::array<ParameterPlan*,2> last_plan{};
  using PublishedProgramOwner=std::weak_ptr<const NativeSceneMaterialProgram>;
  std::map<PublishedProgramOwner,std::shared_ptr<const std::vector<VertexParameterRange>>,
    std::owner_less<PublishedProgramOwner>> published_vertex_ranges;
  std::shared_ptr<const std::vector<VertexParameterRange>> ResolvePublishedVertexRanges(
      const std::shared_ptr<const NativeSceneMaterialProgram>& program) {
    const auto found=published_vertex_ranges.find(PublishedProgramOwner(program));
    if(found!=published_vertex_ranges.end()) return found->second;
    if(!reversed_bindings) throw std::runtime_error("published vertex parameters have no reversed shader");
    auto ranges=std::make_shared<std::vector<VertexParameterRange>>();
    for(const auto& range:program->inputs.vertex_registers) {
      if(range.first>256 || range.count>256-range.first)
        throw std::runtime_error("invalid published activation register range");
      ranges->push_back({range.name,range.first,range.count,
        bindings->ResolveFloatRegisters(range.name),reversed_bindings->ResolveFloatRegisters(range.name)});
    }
    std::erase_if(published_vertex_ranges,[](const auto& entry) { return entry.first.expired(); });
    published_vertex_ranges.emplace(PublishedProgramOwner(program),ranges);
    return ranges;
  }
  ParameterPlan& MaterialPlan(const std::shared_ptr<const NativeMaterialParameters::Groups>& material,bool reverse) {
    const size_t slot=reverse?1:0;
    auto& previous=last_plan_owner[slot];
    if(last_plan[slot] && !previous.owner_before(material) && !material.owner_before(previous))
      return *last_plan[slot];
    auto& cache=parameter_plans[slot];
    auto found=cache.find(ParameterOwner(material));
    if(found==cache.end()) {
      std::erase_if(cache,[](const auto& entry){return entry.first.expired();});
      found=cache.try_emplace(ParameterOwner(material)).first;
    }
    previous=material; last_plan[slot]=&found->second;
    return found->second;
  }
  const std::vector<ShaderBindings::ResourceBinding>& ResolveTextures(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group) {
    auto& plan=MaterialPlan(material,false);
    if(!plan.textures_ready[group]) {
      std::vector<ShaderBindings::ResourceBinding> fresh;
      fresh.reserve(material->textures[group].size());
      for(const auto& texture:material->textures[group]) fresh.push_back(bindings->ResolveResource(texture.name));
      plan.textures[group]=std::move(fresh); plan.textures_ready[group]=true;
    }
    return plan.textures[group];
  }
  const std::vector<ParameterBinding>& ResolveParameters(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group,bool reverse) {
    auto& plan=MaterialPlan(material,reverse);
    if(!plan.ready[group]) {
      std::vector<ParameterBinding> fresh;
      fresh.reserve((*material)[group].size());
      const auto& destination=reverse?*reversed_bindings:*bindings;
      for(const auto& parameter:(*material)[group])
        fresh.push_back({destination.ResolveFloatRegisters(parameter.name),
          IsNativeCanvasXY(destination.shader().source_fingerprint,destination.shader().entry.name,parameter.name,group)});
      plan.groups[group]=std::move(fresh); plan.ready[group]=true;
    }
    return plan.groups[group];
  }
  const UploadPlan& ResolveUploads(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material,size_t group,bool alternate) {
    auto& plan=MaterialPlan(material,false).uploads[alternate?1:0][group];
    if(!plan.ready) {
      // Resolved vectors are immutable after publication; their binding tokens
      // stay owned by this shader's material plans for the upload plan's life.
      const auto& normal=ResolveParameters(material,group,false);
      const auto* reversed=alternate?&ResolveParameters(material,group,true):nullptr;
      UploadPlan fresh;
      fresh.parameters.reserve((*material)[group].size());
      for(size_t index=0;index<(*material)[group].size();++index) {
        ParameterUpload upload;
        upload.index=index; upload.canvas_xy=normal[index].canvas_xy;
        upload.targets={&normal[index].binding,reversed?&(*reversed)[index].binding:nullptr};
        for(size_t target=0;target<upload.targets.size();++target) {
          if(!upload.targets[target]) continue;
          const auto required=upload.targets[target]->bytes();
          if(!required) { ++fresh.optimized_out; upload.targets[target]=nullptr; continue; }
          upload.sizes[target]=(group&1)?required:size_t((*material)[group][index].registers)*16;
          upload.maximum=std::max(upload.maximum,upload.sizes[target]);
        }
        if(upload.targets[0] || upload.targets[1]) fresh.parameters.push_back(upload);
      }
      fresh.ready=true; plan=std::move(fresh);
    }
    return plan;
  }
  std::shared_ptr<const std::vector<VertexParameterRange>> ResolveVertexRanges(
      const std::shared_ptr<const NativeMaterialParameters::Groups>& material) {
    auto& plan=MaterialPlan(material,false);
    if(!plan.vertex_ranges) {
      if(!reversed_bindings) throw std::runtime_error("vertex parameter has no reversed binding plan");
      auto fresh=std::make_shared<std::vector<VertexParameterRange>>();
      for(size_t group=0;group<2;++group) {
        const auto& normal=ResolveParameters(material,group,false);
        const auto& reversed=ResolveParameters(material,group,true);
        size_t index=0;
        for(const auto& parameter:(*material)[group]) {
          if(parameter.registers) {
            if(parameter.first>256 || parameter.registers>256-parameter.first)
              throw std::runtime_error("invalid vertex parameter register range");
            fresh->push_back({parameter.name,parameter.first,parameter.registers,normal[index].binding,reversed[index].binding});
          }
          ++index;
        }
      }
      // Publish only a complete plan. The active shared owner survives material
      // retirement; binding tokens still reject a replaced shader generation.
      plan.vertex_ranges=std::move(fresh);
    }
    return plan.vertex_ranges;
  }
};
// The vertex bindings a draw with this depth convention uses, with the
// reversed variant brought up to date from the normal one where it mirrors it.
// Every draw path that picks a variant goes through here, so a variant can
// never be drawn with the constants of the activation before last.
inline ShaderBindings& VertexBindingsForDraw(RegisteredShader& shader,bool reverse_depth) {
  if(!reverse_depth) return *shader.bindings;
  if(shader.reversed_mirrors) shader.reversed_bindings->MirrorConstantsFrom(*shader.bindings);
  return *shader.reversed_bindings;
}
struct TextureCreation {
  uint32_t width, height, depth, levels, usage, format, pool, type, caller;
};
struct RegisteredTarget {
  uint32_t texture_handle, surface_handle;
  NativeRenderTarget native;
};
inline NativeBufferWrites& BufferWrites() {
  static NativeBufferWrites writes;
  return writes;
}
struct GuestStream {
  uint32_t resource, offset, stride;
};
struct NativeScene {
  NativeRenderTarget color;
  NativeDepthTarget depth;
  NativeRenderTarget output;
  uint32_t output_surface=0;
  uint32_t color_surface=0;
  uint32_t samples=1;
  bool frame_complete=false;
  std::unordered_map<uint32_t,NativeRenderTarget> direct_outputs;
};
struct SurfaceCreation { uint32_t width,height,format,msaa; };
struct EmbeddedShader { uint32_t source; bool pixel; };
struct MoviePlaneLock { uint32_t texture,pitch,pixels; };
struct MovieDecodeLocks { std::vector<MoviePlaneLock> planes; bool failed=false; };
extern thread_local constinit MovieDecodeLocks* active_movie_decode;
struct DrawVisibility {
  Microsoft::WRL::ComPtr<ID3D11Query> query;
  std::array<uint32_t,4> key; // VS, PS, raster state, depth state.
};
struct NativePacingState {
  // Separate from the rendering mutex: sleeping must not block host UI paints.
  std::mutex mutex;
  NativePacingClock clock;
  uint64_t calls=0;
};
inline NativePacingState& PacingState() {
  static NativePacingState state;
  return state;
}
// FSR native AA (native_fsr.h, edf_native_fsr), under the bridge locks.
// `frame` arms one helper call: its scene passes draw jittered and the
// scene's resolve to owner+104 (ResolveScene: the native post's, or the
// 8219C930 hook's mode 1) dispatches FSR and maps owner+104 to the output.
// Armed by the first accepted view's BeginView (ArmNativeFsrFrameLocked);
// cleared by the dispatch, or unconsumed at EndScene / the next frame
// (counted as dropped: a direct-frame publication, or a post that failed
// before resolving).
struct NativeFsrBridgeState {
  NativeFsrUpscaler upscaler;
  NativeFsrResetTracker resets;
  bool frame=false,opaque=false;
  uint32_t owner=0;
  uint64_t helper_frame=0;
  int32_t index=0;
  NativeFsrMode mode=NativeFsrMode::Off;
  NativeFsrJitter jitter;
  NativeFsrCameraParams camera;       // from the unjittered pass camera
  NativeMotionVectorOutput motion;    // workstream B's, for the last view
  std::chrono::steady_clock::time_point last_dispatch{};
  uint64_t armed=0,dispatched=0,dropped=0,failures=0,history_resets=0;
};
struct Bridge {
  // Game command ordering is separate from immediate-context access. A swap
  // may hold this gate while releasing mutex between polls so the host can paint.
  //
  // Full frame vs simulation (the render helper's thread and the main loop):
  // - The simulation's per-step work takes the mutex alone, not the gate: the
  //   step's publication (membership, preloads, adapter Publish), cameras,
  //   world updates (820B2DF8), walk plans (PublishStaticWalkPlans), registry
  //   layout decodes and the list-link touches. (Object birth/retirement, 820B33B0/820B2870/
  //   820B2AC0, still takes both; it is not per step.) The gate is held by the
  //   swap across its GPU and pacing wait, so a step that took it waited out
  //   the render thread's pacing. What it hands the frame is swapped in as immutable
  //   generations (publication, cameras, world animations, registry snapshot,
  //   trees), which AcquireInputs takes once per frame. The static world's
  //   route words are read live from guest memory at selection
  //   (NativeFullFrameLiveRoutes), which needs no bridge lock.
  // - The full frame's passes plan off both locks over those generations and
  //   take gate and mutex together only in short slices (NativeLockSlices)
  //   around what they share with the simulation and other hooks: the model
  //   pass caches (model_pass_loads, model_geometry_loads, over model_buffers,
  //   shaders and declarations), the backend's pipeline/sampler caches and
  //   mesh cache (program Resolve, geometry retention), the adapter's material
  //   intern table, and the scene recorder/targets. Nothing a slice returns
  //   points into locked state except through a shared_ptr, and the recording
  //   slice re-validates the active scene, backend and targets it planned for.
  // - Recorder state (scene recorder, active targets, bind_generation,
  //   recorded, scene_recorded_*) is written only by the render thread, under
  //   both locks; the simulation never records.
  BridgeGate submissions;
  BridgeMutex mutex;
  std::filesystem::path root;
  bool initialized=false;
  std::string scene_backend_name;
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  // Created when --edf_native_backend names one. The renderer still draws
  // through its direct D3D11 path; this exists so the backend can be created
  // and reported inside the real process, which is where device creation
  // actually fails, and so paths can be moved onto it one at a time.
  std::unique_ptr<edf::native::NativeRenderBackend> backend;
  // The scene's resources, separate from the selection above because the two
  // answer different questions while the port is under way: --edf_native_backend
  // is what the player picked, --edf_native_scene_backend is what the half-ported
  // scene can actually share targets with. See the cvar.
  std::shared_ptr<edf::native::NativeRenderBackend> scene_backend;
  NativeSceneAdapter scene_adapter;
  // Advanced whenever state.shaders gains, replaces or releases an entry, so
  // anything derived from a registered shader's bindings can tell it is stale.
  uint64_t shader_registry_generation=0;
  uint64_t scene_publication_tick=0,scene_published_selections=0,scene_current_selections=0;
  NativeSceneRenderer scene_renderer;
  NativeFsrBridgeState fsr;
  std::vector<NativeSceneSnapshot> scene_recorded_snapshots;
  // Full-frame pass frames (models, sky) whose snapshots a recording uses;
  // released with scene_recorded_snapshots at submission.
  std::vector<std::shared_ptr<const void>> scene_recorded_frames;
  uint64_t scene_native_objects=0,scene_native_draws=0,scene_native_fallbacks=0;
  uint64_t scene_native_direct_instances=0,scene_native_direct_retries=0;
  uint64_t scene_native_direct_worlds=0;
  std::set<std::string> scene_native_reasons;
  std::unique_ptr<NativeFrameHandoff> presentation_frames;
  std::weak_ptr<GuestMeshWatchAudit> mesh_watch_audit;
  std::optional<NativeDisplayGamma> display_gamma;
  uint32_t display_gamma_device=0;
  std::map<uint32_t,uint32_t> native_published_completions;
  NativeSubmissionCursors submission_cursors;
  struct SwapClock { NativePacingClock clock; uint64_t sampled=0;
    std::unique_ptr<NativeFrameFlight> flight; };
  std::map<uint32_t,SwapClock> swap_clocks;
  std::unordered_map<uint32_t, RegisteredShader> shaders;
  std::shared_ptr<const std::vector<VertexParameterRange>> active_vertex_parameters;
  uint64_t instance_parameter_updates=0, instance_parameter_errors=0;
  NativeSceneSources scene_sources;
  NativeSceneMembership scene_membership;
  NativeStaticWalkPlans static_walk_plans;
  NativeStaticWalkAudit static_walk_audit;
  uint64_t static_walk_lists=0,static_walk_misses=0,static_walk_stale=0,static_walk_members=0;
  uint64_t static_walk_direct_reuses=0,static_walk_source_reuses=0,static_walk_abandoned=0,static_walk_bucket_native=0;
  uint64_t scene_membership_events=0,scene_membership_lists=0,scene_membership_nodes=0;
  uint64_t scene_membership_checks=0,scene_membership_mismatches=0;
  uint64_t scene_source_draws=0,scene_source_misses=0;
  uint64_t scene_source_publications=0;
  uint64_t scene_world_publications=0,scene_world_reused=0,scene_world_reads=0,scene_world_checks=0,scene_world_mismatches=0;
  uint64_t scene_queue_instances=0,scene_queue_groups=0,scene_queue_fallbacks=0;
  uint64_t scene_group_material_captures=0,scene_group_material_reused=0;
  uint64_t scene_asset_examined=0,scene_asset_created=0,scene_asset_rejected=0;
  struct SceneGeometryLoad {
    NativeSceneGeometrySource source;
    uint64_t revision=0,vertex_generation=0,index_generation=0;
    std::array<NativeBufferWrites::ObservedVersion,2> versions{};
    std::shared_ptr<const NativeDeclaration> declaration;
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    NativeRecordedReads reads; // Descriptor bytes behind `source`.
  };
  std::map<uint32_t,SceneGeometryLoad> scene_geometry_loads;
  struct SceneMaterialLoad {
    uint64_t revision=0;
    uint32_t material=0;
    std::shared_ptr<const NativeMaterialParameters::Groups> schema;
    std::shared_ptr<const NativeSceneGroupMaterial> published;
    // Program bytes only: pass, shader, texture, sampler and state inputs.
    // Constant values change per frame and are refreshed through `constants`.
    NativeRecordedReads reads;
    NativeSceneMaterialConstantLayout constants;
  };
  std::map<uint32_t,SceneMaterialLoad> scene_material_loads;
  // Model pass (821C9C20): material programs keyed by pass record address and
  // retained geometry keyed by (batch descriptor, vertex shader). Validated at
  // each use; never trusted across a changed input.
  struct ModelPassLoad {
    std::shared_ptr<const NativeMaterialParameters::Groups> schema;
    std::shared_ptr<const NativeSceneGroupMaterial> published;
    NativeRecordedReads reads; // Program bytes only; constant values are re-read at each use.
    NativeSceneMaterialConstantLayout layout;
    uint64_t failed_tick=UINT64_MAX;
  };
  std::unordered_map<uint32_t,ModelPassLoad> model_pass_loads;
  struct ModelGeometryLoad {
    NativeSceneGeometrySource source;
    uint64_t vertex_generation=0,index_generation=0;
    std::array<NativeBufferWrites::ObservedVersion,2> versions{};
    std::shared_ptr<const NativeDeclaration> declaration;
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  };
  std::map<std::pair<uint32_t,uint32_t>,ModelGeometryLoad> model_geometry_loads;
  uint64_t model_pass_loaded=0,model_pass_reused=0,model_pass_constants=0,model_geometry_loaded=0;
  uint64_t scene_preload_group_revision=UINT64_MAX;
  uint64_t scene_geometry_loaded=0,scene_geometry_reused=0,scene_geometry_deferred=0;
  uint64_t scene_geometry_unchanged=0,scene_geometry_verified=0,scene_material_unchanged=0;
  uint64_t scene_material_constants=0;
  std::set<std::string> scene_geometry_reasons;
  uint64_t scene_material_loaded=0,scene_material_reused=0,scene_material_deferred=0;
  uint64_t scene_material_constructed=0,scene_material_rejected=0;
  uint64_t scene_material_binding_bypasses=0;
  uint64_t scene_geometry_draw_bypasses=0,scene_geometry_draw_verified=0;
  std::set<std::string> scene_material_reasons;
  uint64_t scene_visibility_candidates=0,scene_visibility_retained=0,scene_visibility_selected=0;
  uint64_t scene_visibility_checks=0,scene_visibility_mismatches=0;
  std::unordered_map<uint32_t, EmbeddedShader> embedded_shaders;
  // Advanced whenever embedded_shaders gains, replaces or loses an entry, for
  // the same purpose as shader_registry_generation.
  uint64_t embedded_shader_generation=0;
  // The DrawPrimitiveUP hook's per-pair classification (native_immediate_classify.h)
  // and the registry entries it was read from, which stay valid while both
  // generations do: state.shaders only erases or replaces under a generation bump.
  struct ImmediatePairShaders { RegisteredShader* vertex=nullptr; RegisteredShader* pixel=nullptr; };
  NativeImmediatePairMemo<ImmediatePairShaders> immediate_pairs;
  std::unordered_map<uint32_t, NativeTexture> textures;
  std::unordered_map<uint32_t, TextureCreation> texture_creations;
  std::unordered_map<uint32_t, RegisteredTarget> render_targets;
  std::unordered_map<uint32_t, NativeDepthTarget> depth_targets;
  std::unordered_map<uint32_t, SurfaceCreation> surface_creations;
  std::unordered_map<uint32_t, NativeScene> scenes;
  uint32_t active_scene = 0;
  std::set<uint32_t> untiled_devices;
  std::unique_ptr<NativeGpuTimer> scene_gpu_timer;
  uint32_t scene_gpu_timer_owner=0;
  bool scene_gpu_timer_resolved=false;
  uint32_t active_output = 0, output_captures = 0;
  uint64_t loading_trace_frames=0;
  std::array<uint64_t,4> loading_trace_routes{};
  std::chrono::steady_clock::time_point loading_trace_reported{};
  std::chrono::steady_clock::time_point bloom_parameters_reported{};
  uint64_t output_draws=0;
  uint64_t output_unhandled=0;
  // Sentinel distinguishes "not observed" from a real zero register word.
  uint64_t vertex_center_word=UINT64_MAX;
  uint64_t shared_constant_activations=0,shared_constant_unsupplied=0,shared_constant_split_storage=0;
  uint64_t shared_constant_both_supplied=0;
  uint32_t last_activation_instance=0,last_activation_vertex=0,last_activation_pixel=0;
  uint64_t repeat_activations=0;
  // Whether the scene backend has a frame open. Opened lazily by the first
  // thing that records into it and closed at the guest's swap barrier, which
  // is the only point in the frame where the renderer already knows the frame
  // is over. A backend that records has to be told where a frame ends: D3D11
  // did not, which is why nothing needed this until now.
  bool scene_frame_open=false;
  uint64_t scene_frames=0;
  // Recorded operations in the open frame. A backend frame has to be bounded
  // by something: the guest's swap alone is not, because the renderer records
  // far more between two swaps than one command list should carry.
  uint64_t scene_frame_operations=0;
  uint64_t scene_frame_splits=0;
  // The scene's finished frame, in a surface the window's backend can open.
  //
  // This is how a scene drawn on one backend reaches a window presented by
  // another. The D3D11 path hands the compositor an ID3D11ShaderResourceView
  // instead, which is exactly what a D3D12 scene cannot produce.
  std::shared_ptr<edf::native::NativeBackendSharedSurface> scene_shared;
  std::array<std::shared_ptr<NativeBackendSharedSurface>,NativeBackendFrameQueue::kSlots> scene_shared_slots;
  std::array<uint64_t,NativeBackendFrameQueue::kSlots> scene_shared_slot_generations{};
  std::optional<size_t> scene_shared_slot;
  uint64_t scene_shared_next_generation=0;
  NativeBackendFrameQueue scene_frame_queue;
  uint32_t scene_shared_width=0,scene_shared_height=0;
  // Set when a copy into it has been recorded and not yet signalled. The
  // signal is a queue signal on the backends that have a queue, so it has to
  // happen after the frame is submitted, not while it is still open.
  bool scene_shared_pending=false;
  uint64_t scene_shared_sequence=0,scene_shared_generation=0;
  std::optional<NativeDisplayGamma> scene_shared_gamma;
  NativeFrameKind scene_shared_kind=NativeFrameKind::PartialScene;
  bool scene_shared_refused=false;
  // What the last recorded draw left the recorder holding.
  //
  // The same reasoning the direct path already uses: 77.4% of this game's
  // draws repeat the one before them in everything but the constants an
  // activation patched between them, so re-sending the pipeline, the targets,
  // the viewport and the material is the same calls with the same arguments.
  // The direct path skips those and this has to as well, or recording is
  // slower than the thing it replaces for no reason anyone would accept.
  struct RecordedBindings {
    bool valid=false;
    // Bumped by anything that binds the context directly. On the adopted D3D11
    // backend the recorder and the direct paths share one context, so a direct
    // bind invalidates what the recorder believes is still set.
    uint64_t bind_generation=0;
    uint64_t frame=0;
    edf::native::NativeBackendPipeline* pipeline=nullptr;
    // What that pipeline was built from. Compared here so a repeat draw does
    // not go through the backend's cache at all: that lookup builds a string
    // key per call, which is a heap allocation on a path that runs a million
    // times a minute.
    uint64_t vertex_id=0,pixel_id=0,layout_id=0;
    edf::native::RenderStateWords state{};
    edf::native::NativeBackendTopology topology=edf::native::NativeBackendTopology::TriangleList;
    uint32_t render_targets=0,sample_count=0,dsv_format=0;
    std::array<uint32_t,8> rtv_format{};
    std::array<edf::native::NativeBackendRenderTarget*,8> colors{};
    uint32_t color_count=0;
    edf::native::NativeBackendRenderTarget* depth=nullptr;
    D3D11_VIEWPORT viewport{};
    D3D11_RECT scissor{};
    bool scissor_enabled=false;
    const edf::native::ShaderBindings* pixel=nullptr;
    uint64_t pixel_resources=0;
    bool blend_factor_needed=false;
    std::array<float,4> blend_factor{};
    // The bytes last staged for each constant buffer, so an unchanged buffer
    // is not copied into the upload ring again.
    edf::native::NativeConstantCache vertex_constants,pixel_constants;
  } recorded;
  uint64_t recorded_draws=0,recorded_pipeline_skips=0,recorded_material_skips=0;
  uint64_t recorded_constant_skips=0;
  // Declared after the backend so it is destroyed before it: the preview
  // thread uses the backend on every tick and must be stopped first.
  std::unique_ptr<edf::native::NativeD3D12Preview> backend_preview;
  // Run-length accounting for the XUI path, the same question the indexed
  // audit answered: how many consecutive draws differ only in things a batch
  // would carry per-item, and how many change state that a batch cannot.
  // Bumped by every path that binds a target or render state, so the indexed
  // path can tell whether anything has bound since it last did. Without this a
  // skip would compare against its own cache and miss that another path had
  // replaced the state underneath it.
  uint64_t bind_generation=0,indexed_bind_generation=0;
  edf::native::RenderStateWords indexed_bind_key{};
  uint32_t indexed_bind_target=0,indexed_bind_scene=0,indexed_bind_output=0;
  uint32_t indexed_bind_vertex=0,indexed_bind_pixel=0;
  bool indexed_bind_reversed=false;
  uint64_t indexed_materials_reused=0;
  bool indexed_bind_valid=false;
  uint64_t indexed_binds_skipped=0,indexed_binds_bound=0;
  uint64_t xui_batch_draws=0,xui_batch_runs=0,xui_batch_run=0,xui_batch_longest=0;
  uint64_t xui_batch_collapsible=0,xui_last_state=0,xui_last_constants=0;
  uint64_t xui_constants_differ=0;
  // Per-part breakdown of the XUI audit: the parts of the draw before, and
  // how often each part differed from it.
  std::array<uint64_t,9> xui_audit_parts{},xui_audit_breaks{};
  uint64_t xui_audit_identical=0,xui_audit_constants_only=0;
  std::array<uint32_t,12> last_batch_key{};
  uint64_t batch_draws=0,batch_runs=0,batch_run=0,batch_run_total=0,batch_longest=0,batch_collapsible=0;
  uint64_t instance_shape=0,last_instance_shape=0,batch_shape_breaks=0;

  std::set<std::array<uint32_t,2>> shared_constant_pairs;
  std::set<std::array<uint32_t,3>> shared_constant_storage_reported;
  std::set<std::array<uint32_t,3>> shared_constant_reported;
  uint64_t movie_uploads=0, movie_upload_errors=0;
  uint64_t movie_draws=0, movie_draw_errors=0;
  // Movie drawing can run on a helper between main-thread UI swaps. Keep both
  // clocks paced until a completed 3D scene takes over or movie_pacing sees
  // NativeMoviePacing::kIdleSwaps swaps in a row without a new movie draw,
  // rather than clearing on the first UI-only swap that did not draw the movie.
  std::atomic<bool> movie_pacing_active{false};
  NativeMoviePacing movie_pacing;
  std::unique_ptr<ShaderBindings> movie_vertex,movie_pixel,movie_pixel_sd;
  std::array<std::optional<NativeMovieBindings>,2> movie_bindings;
  std::unique_ptr<QuadStream> movie_vertices;
  std::unique_ptr<ShaderBindings> xui_vertex,xui_pixel;
  std::unique_ptr<ShaderBindings> xui_reversed_vertex;
  std::optional<NativeXuiVertexBindings> xui_vertex_bindings,xui_reversed_vertex_bindings;
  std::array<std::optional<NativeXuiPixelBindings>,3> xui_pixel_bindings;
  std::unique_ptr<ShaderBindings> xui_solid_pixel,xui_mask_pixel;
  std::unique_ptr<PositionTriangleStream> xui_vertices;
  uint64_t xui_draws=0,xui_errors=0;
  std::unique_ptr<ShaderBindings> font_vertex,font_pixel;
  std::optional<NativeFontBindings> font_bindings;
  std::unique_ptr<QuadStream> font_vertices;
  uint64_t font_draws=0,font_errors=0;
  uint64_t utility_draws=0,utility_errors=0;
  uint64_t utility_3d_draws=0,utility_3d_errors=0;
  std::array<bool,6> scene_immediate_variants_reported{};
  std::array<bool,4> utility_variants_reported{};
  std::set<std::array<uint32_t,4>> unsupported_output_pairs;
  uint64_t immediate_requests=0,immediate_empty=0,immediate_submitted=0,immediate_unsubmitted=0;
  std::set<std::array<uint32_t,5>> immediate_unsubmitted_paths;
  std::chrono::steady_clock::time_point immediate_coverage_reported{};
  uint64_t scene_begins = 0, scene_ends = 0;
  uint64_t scene_resolves = 0,scene_resolve_errors = 0;
  uint64_t scene_indexed_start = 0;
  // A full native frame recorded on the current scene (reset at scene begin);
  // it counts as the scene's indexed draws (NativeSceneDrew).
  bool scene_full_frame = false;
  uint32_t scene_captures = 0;
  uint64_t indexed_output_frames = 0;
  uint64_t indexed_trace_frame = 0;
  uint64_t post_input_capture_frame = 0;
  uint32_t post_input_capture_pass = 0;
  uint32_t indexed_trace_draws = 0;
  std::vector<DrawVisibility> visibility;
  std::set<std::array<uint32_t,4>> clip_probes;
  uint32_t color_probe_draws=0;
  bool color_probe_done=false;
  uint64_t depth_clears = 0, depth_clear_skips = 0, depth_errors = 0;
  // Backend samplers, keyed by the guest words they were decoded from. The
  // backend owns the sampler objects and caches them by combination too; this
  // map only saves decoding the same words again.
  std::map<SamplerStateWords, edf::native::NativeBackendSampler*> samplers;
  std::map<RenderStateWords,NativeRenderState> render_states;
  // The two most recent keys each UI draw path looked up in the maps above
  // (RenderStateLocked, SamplerLocked). Neither map ever erases, so an entry
  // stays valid for the bridge's life.
  struct RenderStateMemo { RenderStateWords key{}; NativeRenderState* value=nullptr; };
  struct SamplerMemo { SamplerStateWords key{}; edf::native::NativeBackendSampler* value=nullptr; };
  std::array<RenderStateMemo,2> render_state_memo{};
  std::array<SamplerMemo,2> sampler_memo{};
  std::map<std::pair<uint32_t,uint32_t>,GuestStream> streams;
  std::map<uint32_t,uint32_t> index_bindings;
  std::map<uint32_t,uint32_t> declaration_bindings;
  NativeShaderState shader_bindings;
  NativeRenderStateSnapshots render_state_snapshots;
  NativeDeclarations declarations;
  NativeMaterialParameters material_parameters;
  NativeGeneratedIndexCache generated_indices;
  NativeModelBuffers model_buffers{&BufferWrites()};
  uint64_t buffer_update_notifications=0;
  uint64_t buffer_write_batches=0;
  uint64_t buffer_write_all_batches=0;
  uint64_t reported_mesh_mismatches=0;
  NativeMeshCache meshes;
  // Immediate guest buffers may be reused or mutated every draw. This cache
  // compares all bytes and stays separate from long-lived scene geometry.
  NativeMeshCache immediate_meshes{4*1024*1024,256,true};
  std::map<uint32_t,std::unique_ptr<NativeCompletionQueue>> completion_queues;
  std::map<uint32_t,std::unique_ptr<NativePresentProfiler>> present_profilers;
  std::set<uint32_t> completion_faults;
  uint64_t completion_submits=0;
  std::unordered_map<uint32_t,std::unique_ptr<NativeSignalQueue>> signal_queues;
  // Registry/context -> delivery, or SDK global -> delivery. Never delivery ->
  // registry/context/global. CPU delivery must not depend on renderer progress.
  std::mutex signal_delivery_mutex;
  std::unordered_map<uint32_t,NativeSignalDelivery> signal_deliveries;
  uint64_t completion_waits=0,completion_waits_native_pending=0;
  uint64_t completion_validated_waits=0;
  uint64_t indexed_draws = 0, indexed_uploads = 0, indexed_errors = 0, indexed_submitted = 0;
  std::array<uint64_t,4> indexed_ownership{}; // Bit0: registered VB; bit1: registered IB.
  uint64_t indexed_ownership_attempts=0,indexed_physical_pairs=0;
  uint64_t indexed_empty_requests=0,indexed_nonempty_submitted=0,indexed_unsubmitted_requests=0;
  std::set<std::array<uint32_t,5>> indexed_unsubmitted_paths;
  // Identity is the contract, not the caller: see native_contract_ledger.h.
  edf::native::NativeContractLedger contracts;
  std::chrono::steady_clock::time_point indexed_coverage_reported{};
  uint64_t indexed_outside_scene = 0;
  uint64_t sampler_bindings = 0;
  std::vector<std::pair<uint32_t,uint32_t>> target_stack;
  uint32_t active_target = 0;
  uint64_t target_begins = 0, target_resolves = 0, target_unwritten = 0;
  uint64_t immediate_draws = 0;
  uint64_t native_quad_draws = 0, native_quad_errors = 0;
  uint64_t color_clears = 0, color_clear_skips = 0;
  uint32_t active_vertex = 0, linked_vertex = 0, linked_pixel = 0;
  // Shader pairs whose varyings have been checked against each other. The
  // check reflects both signatures, and the pair changes hundreds of times a
  // frame; a pair only needs it once per registration of its shaders.
  std::set<std::pair<uint32_t,uint32_t>> validated_links;
  uint64_t texture_loads = 0, texture_errors = 0;
  uint64_t texture_bindings = 0, texture_missing = 0, texture_binding_errors = 0;
  uint64_t activations = 0, misses = 0, parameter_uploads = 0, optimized_out = 0, parameter_errors = 0;
};
inline Bridge& State() { static Bridge state; return state; }
}  // namespace edf::native
