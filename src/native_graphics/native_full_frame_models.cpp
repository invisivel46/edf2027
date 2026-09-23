#include "native_full_frame_models.h"
#include <algorithm>

namespace edf::native {
NativeFullFrameModelVisibility ClassifyNativeFullFrameModel(const NativeRenderEntry& entry,const NativeSceneVisibilityView& view) {
  using C=NativeFullFrameModelCull;
  NativeFullFrameModelVisibility result;
  if(entry.hidden) { result.cull=C::Hidden; return result; }
  if(entry.mode!=0 && entry.mode!=1 && entry.mode!=2) { result.cull=C::Mode; return result; }
  result.centre=NativeVisibilityTransform(entry.centre,view.matrix);
  result.depth=-float(result.centre[2]*view.depth_scale);
  // Negated as the gather does: a NaN depth is not culled by distance.
  if(result.depth>entry.cull_distance) { result.cull=C::Distance; return result; }
  result.sphere=NativeVisibilitySphere(view,result.centre,entry.radius);
  if(!result.sphere) { result.cull=C::Frustum; return result; }
  if(result.sphere!=2) { result.box=result.sphere; return result; }
  std::array<float,16> box;
  std::copy(entry.centre.begin(),entry.centre.end(),box.begin());
  std::copy(entry.axes.begin(),entry.axes.end(),box.begin()+4);
  result.box=NativeVisibilityBox(view,box);
  if(!result.box) result.cull=C::Box;
  return result;
}
std::optional<uint32_t> SelectNativeFullFrameModelLod(const NativeRenderEntry& entry,float depth) {
  const auto& thresholds=entry.lod_thresholds;
  uint32_t chosen=0;
  switch(entry.type?entry.type->lod:NativeRenderLodKind::None) {
    case NativeRenderLodKind::None: break;
    case NativeRenderLodKind::Character:
      for(uint32_t i=0;i<thresholds.size();++i) if(depth>thresholds[i]) chosen=i+1;
      break;
    case NativeRenderLodKind::FieldParts:
      for(uint32_t i=1;i<thresholds.size();++i) if(depth>thresholds[i]) chosen=i;
      break;
  }
  if(chosen>=entry.models.size()) return std::nullopt;
  const auto& model=entry.models[chosen];
  if(!model.instance || !model.layout) return std::nullopt;
  return chosen;
}
NativeFullFrameModelPlan PlanNativeFullFrameModels(const NativeRenderRegistrySnapshot& snapshot,const NativeFullFrameModelCamera& camera) {
  using C=NativeFullFrameModelCull;
  NativeFullFrameModelPlan plan;
  auto& stats=plan.stats;
  for(const auto& shared:snapshot.entries) {
    if(!shared) continue;
    const auto& entry=*shared;
    ++stats.entries;
    const auto visibility=ClassifyNativeFullFrameModel(entry,camera.visibility);
    switch(visibility.cull) {
      case C::Visible: break;
      case C::Hidden: ++stats.hidden; continue;
      case C::Mode: ++stats.mode; continue;
      case C::Distance: ++stats.distance; continue;
      case C::Frustum: ++stats.frustum; continue;
      case C::Box: ++stats.box; continue;
    }
    const auto model=SelectNativeFullFrameModelLod(entry,visibility.depth);
    const bool posed=model && entry.pose && entry.pose->size()==entry.models[*model].layout->bones;
    if(!model) ++stats.no_model;
    else if(!posed) ++stats.no_pose;
    // A set is drawable once its model decoded without a pose (821C9DA8).
    const auto drawable=[](const NativeRenderInstanced& set) {
      return set.model.instance && set.model.layout && set.model.layout->single_world && set.worlds;
    };
    size_t instances=0;
    for(const auto& set:entry.instanced) { if(drawable(set)) instances+=set.worlds->size(); else ++stats.no_instanced; }
    if(!posed && !instances) continue;
    NativeFullFrameModelItem item{&entry,model.value_or(0),visibility.depth,visibility.centre[2],0,entry.mode!=0};
    if(item.transparent) {
      item.key=NativeFullFrameModelKey(entry.mode,item.view_z,entry.sort_bias,camera.key_scale,camera.key_offset);
      if(item.key<256) { ++stats.bucket_zero; continue; }
    }
    auto& list=item.transparent?plan.transparent:plan.opaque;
    if(posed) list.push_back(item);
    for(size_t set=0;set<entry.instanced.size();++set) {
      if(!drawable(entry.instanced[set])) continue;
      for(uint32_t world=0;world<entry.instanced[set].worlds->size();++world) {
        auto instance=item; instance.instanced=int32_t(set); instance.world=world;
        list.push_back(instance); ++stats.instances;
      }
    }
  }
  std::stable_sort(plan.transparent.begin(),plan.transparent.end(),
    [](const auto& a,const auto& b) { return a.key>b.key; });
  stats.opaque=plan.opaque.size(); stats.transparent=plan.transparent.size();
  return plan;
}
std::vector<const NativeRenderEntry*> NativeFullFrameBrokenObjects(const NativeRenderRegistrySnapshot& snapshot,
    const NativeFullFrameModelCamera& camera) {
  std::vector<const NativeRenderEntry*> result;
  for(const auto& shared:snapshot.entries) {
    if(!shared || !shared->type || shared->type->vtable!=NativeBrokenObject::vtable) continue;
    if(NativeFullFrameModelDispatched(ClassifyNativeFullFrameModel(*shared,camera.visibility),*shared,camera)) result.push_back(shared.get());
  }
  return result;
}
NativeFullFrameModelConstants NativeFullFrameModelConstantsFor(const NativeModelLayout& layout,
    std::span<const NativePoseMatrix> pose,uint32_t palette_limit) {
  NativeFullFrameModelConstants result;
  result.skinned=layout.skinned;
  static const auto identity=NativeModelWorldRegisters(kNativeSceneIdentity);
  // Rigid layouts upload every record's bone (uploads_bone), so the entry
  // value is never seen there.
  result.worlds=NativeModelWorldPlan(layout,pose,identity);
  if(layout.skinned) {
    if(pose.empty()) throw std::runtime_error("native full-frame skinned model has an empty pose");
    result.palette.resize(size_t(NativeBonePaletteCount(uint32_t(pose.size()),palette_limit))*kNativeBonePaletteFloats);
    result.bones=PackNativeBonePalette(pose,result.palette,palette_limit);
  }
  return result;
}
NativeFullFrameModelConstants NativeFullFrameModelInstancedConstants(const NativeModelLayout& layout,const NativePoseMatrix& world) {
  if(!layout.single_world) throw std::runtime_error("native full-frame instanced model has a pose layout");
  NativeFullFrameModelConstants result;
  result.worlds.assign(layout.meshes.size(),NativeModelWorldRegisters(world));
  return result;
}
bool BindNativeFullFrameModelPalette(std::vector<NativeSceneMaterialInputs::Constant>& constants,std::span<const float> palette) {
  for(auto& constant:constants) {
    if(constant.name!="g_mWorldArray") continue;
    if(!constant.global || constant.pixel || constant.registers.size()%16 || palette.size()*4>constant.registers.size()) return false;
    std::fill(constant.registers.begin(),constant.registers.end(),uint8_t(0));
    for(size_t i=0;i<palette.size();++i) StoreNativeGuestFloat(constant.registers.data()+i*4,palette[i]);
  }
  return true;
}
std::vector<NativeFullFrameModelDrawRef> OrderNativeFullFrameModelDraws(std::span<const NativeFullFrameModelItem> items,bool opaque) {
  std::vector<NativeFullFrameModelDrawRef> result;
  for(uint32_t item=0;item<items.size();++item) {
    const auto& layout=NativeFullFrameModelItemLayout(items[item]);
    const auto plan=NativeModelDrawPlan(layout);
    for(uint32_t index=0;index<plan.size();++index) {
      const auto& draw=plan[index];
      const auto& passes=layout.meshes[draw.mesh].batches[draw.batch].passes;
      const auto pass_index=uint32_t(std::find(passes.begin(),passes.end(),draw.pass)-passes.begin());
      result.push_back({item,index,pass_index,draw});
    }
  }
  if(opaque) {
    const auto batch=[&](const NativeFullFrameModelDrawRef& ref) {
      const auto& item=items[ref.item];
      return NativeFullFrameModelItemLayout(item).meshes[ref.draw.mesh].batches[ref.draw.batch].address;
    };
    std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b) {
      return std::tuple(a.pass_index,batch(a),a.draw.pass)<std::tuple(b.pass_index,batch(b),b.draw.pass);
    });
  }
  return result;
}
namespace {
std::vector<NativeSceneMaterialInputs::Constant> PassConstants(const NativeSceneGroupMaterial& material,
    const NativeFullFrameModelCamera& camera) {
  auto constants=material.constants;
  for(auto& constant:constants) {
    if(camera.pass.Apply(constant) || !constant.global ||
       (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
    if(!camera.animation) throw std::runtime_error("native full-frame model material needs the world animation");
    camera.animation->Apply(constant);
  }
  return constants;
}
NativeSceneResolvedMaterial ResolveMaterial(const NativeSceneMaterialProgram& program,const NativeIndexedMesh::RetainedDraw& geometry,
    const NativeFullFrameModelPass& pass,const NativeSceneMaterialPassState& base,
    std::span<const NativeSceneMaterialInputs::Constant> constants,bool palette) {
  if(!pass.targets.count) throw std::runtime_error("native full-frame model pass has no color target");
  // Decodable before any pipeline is created.
  DecodeNativeRenderState(base.After(program).render.words);
  NativeBackendPipelineDesc desc;
  desc.vertex_id=(uint64_t(program.inputs.vertex)<<1)|uint64_t(pass.targets.reverse_depth);
  desc.pixel_id=program.inputs.pixel;
  desc.input_layout=geometry.input_layout().elements(); desc.input_layout_id=geometry.input_layout().fingerprint();
  desc.render_targets=pass.targets.count; desc.rtv_format=pass.targets.rtv_format;
  desc.dsv_format=pass.targets.dsv_format; desc.sample_count=pass.targets.samples;
  return program.Resolve(desc,pass.targets.reverse_depth,constants,base.render,base.samplers,pass.filtering,palette);
}
bool SameView(const NativeSceneView& a,const NativeSceneView& b) {
  return a.view==b.view && a.projection==b.projection && a.view_projection==b.view_projection &&
    a.scissor_enabled==b.scissor_enabled;
}
const std::shared_ptr<const NativeModelLayout>& ItemLayoutObject(const NativeFullFrameModelItem& item) {
  return item.instanced<0?item.entry->models[item.model].layout:item.entry->instanced[size_t(item.instanced)].model.layout;
}
// The sampler objects program.Resolve bound, in program texture order, from
// its resolved sampler pass (the backend's sampler cache returns the same
// object for the same description).
std::vector<NativeBackendSampler*> ResolvedSamplers(const NativeSceneMaterialProgram& program,
    const std::array<NativeMaterialSamplerPass,16>& samplers,int filtering) {
  std::vector<NativeBackendSampler*> resources;
  resources.reserve(program.inputs.textures.size());
  for(const auto& texture:program.inputs.textures) {
    if(texture.slot>=samplers.size()) throw std::runtime_error("native material sampler slot is invalid");
    resources.push_back(&program.backend->CreateSampler(DecodeNativeGuestSampler(NativeFilteringKey(samplers[texture.slot].words,filtering))));
  }
  return resources;
}
}
NativeFullFrameModelFrame NativeFullFrameModels::Build(const NativeRenderRegistrySnapshot& snapshot,
    const NativeFullFrameModelCamera& camera,const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources,
    const std::function<void(NativeFullFrameModelPhase)>& phase) {
  using Phase=NativeFullFrameModelPhase;
  using Cache=NativeFullFrameModelMaterialCache<NativeFullFrameModelResolve>;
  const auto enter=[&](Phase next) { if(phase) phase(next); };
  NativeFullFrameModelFrame frame;
  auto& stats=frame.stats;
  enter(Phase::Visibility);
  frame.plan=PlanNativeFullFrameModels(snapshot,camera);
  // Programs: the program and geometry of every draw of every item, from the
  // side table (the providers only when the generation moved or a row is new).
  enter(Phase::Programs);
  const auto generation=sources.generation?sources.generation():kNativeFullFrameModelUnversioned;
  const auto source_hits=sources_.hits,source_fetches=sources_.fetches;
  struct Gathered {
    std::vector<NativeModelDraw> draws;
    std::vector<NativeFullFrameModelSourcePair> sources;
    bool complete=false;
  };
  const std::array<std::span<const NativeFullFrameModelItem>,2> lists{
    std::span<const NativeFullFrameModelItem>(frame.plan.opaque),std::span<const NativeFullFrameModelItem>(frame.plan.transparent)};
  std::array<std::vector<Gathered>,2> gathered;
  for(size_t list=0;list<lists.size();++list) {
    gathered[list].resize(lists[list].size());
    for(size_t index=0;index<lists[list].size();++index) {
      const auto& item=lists[list][index];
      // The item's layout object: the posed LOD model's, or its instanced set's.
      const auto& layout_object=ItemLayoutObject(item);
      const auto& layout=*layout_object;
      auto& result=gathered[list][index];
      ++stats.items;
      try {
        result.draws=NativeModelDrawPlan(layout);
        result.sources.reserve(result.draws.size());
        result.complete=true;
        for(const auto& draw:result.draws) {
          const auto& batch=layout.meshes[draw.mesh].batches[draw.batch];
          auto source=sources_.Get(draw.pass,batch.address,layout_object,generation,[&] {
            NativeFullFrameModelSourcePair fetched{sources.program?sources.program(draw.pass):nullptr,nullptr};
            if(fetched.first && fetched.first->program && sources.geometry) fetched.second=sources.geometry(batch,draw.pass);
            return fetched;
          },[](const NativeFullFrameModelSourcePair& value) { return value.first && value.first->program && value.second; });
          if(!source.first || !source.first->program) { ++stats.missing_program; result.complete=false; break; }
          if(!source.second) { ++stats.missing_geometry; result.complete=false; break; }
          result.sources.push_back(std::move(source));
        }
      } catch(const std::exception&) { ++stats.failed; result.complete=false; }
      if(!result.complete) { result.draws.clear(); result.sources.clear(); }
    }
  }
  stats.source_hits=sources_.hits-source_hits; stats.source_fetches=sources_.fetches-source_fetches;
  // Resolve: each draw's material from the cache (the pass constants compared,
  // the camera derived from them) or a resolve, then its scene object.
  enter(Phase::Resolve);
  const auto base=NativeFullFrameModelBaseState(pass.targets);
  struct Memo { NativeSceneMaterialCapture capture; bool scissor=false; };
  using MemoKey=std::tuple<uint32_t,const NativeSceneMaterialProgram*,const NativeIndexedMesh::RetainedDraw*>;
  std::map<MemoKey,Memo> memo;
  std::map<MemoKey,std::vector<NativeSceneMaterialInputs::Constant>> skinned_constants;
  struct Resolved { std::shared_ptr<const NativeSceneInstance> object; NativeSceneView view; };
  // Each resolve or capture (the backend's pipeline and sampler caches) with
  // its intern runs in one exclusive call: one short hold of the host's locks.
  // The cache rows are Build's own state and are used outside it.
  const auto exclusive=[&](const std::function<void()>& work) { if(sources.exclusive) sources.exclusive(work); else work(); };
  // Resolves one draw, or throws / returns nothing (counted) when it cannot.
  const auto resolve=[&](const NativeModelLayout& layout,const NativeFullFrameModelConstants& values,
      const NativeModelDraw& draw,const NativeFullFrameModelSourcePair& source) -> std::optional<Resolved> {
    const auto& material=*source.first;
    const auto& program=*material.program;
    // Scissor enable's rectangle is not a pass input (as in the world pass).
    if(!program.CanDeferCpuActivation()) { ++stats.scissor; return std::nullopt; }
    const auto& geometry=source.second;
    if(!program.backend || geometry->backend()!=program.backend.get()) { ++stats.missing_geometry; return std::nullopt; }
    const auto key=MemoKey(draw.pass,&program,geometry.get());
    Cache::Key cache_key{draw.pass,layout.skinned,material.program,geometry,base,pass.targets,pass.filtering};
    Memo resolved;
    if(layout.skinned) {
      // The palette is per draw: capture against the row's pipeline half.
      auto found=skinned_constants.find(key);
      if(found==skinned_constants.end()) found=skinned_constants.emplace(key,PassConstants(material,camera)).first;
      auto constants=found->second;
      if(!BindNativeFullFrameModelPalette(constants,values.palette)) { ++stats.palette; return std::nullopt; }
      if(const auto* entry=materials_.Candidate(cache_key)) {
        const auto& cached=entry->material;
        exclusive([&] {
          resolved.capture=program.Capture(*cached.pipeline,pass.targets.reverse_depth,constants,cached.samplers,cached.blend_factor,true);
        });
        resolved.scissor=cached.scissor;
        ++materials_.hits; ++stats.cache_hits; ++stats.captures;
      } else {
        std::optional<NativeSceneResolvedMaterial> result;
        NativeFullFrameModelResolve half;
        exclusive([&] {
          result=ResolveMaterial(program,*geometry,pass,base,constants,true);
          half.samplers=ResolvedSamplers(program,result->samplers,pass.filtering);
        });
        ++materials_.misses; ++stats.resolves;
        half.pipeline=result->capture.material->pipeline(); half.blend_factor=result->capture.material->blend_factor();
        half.scissor=result->render.words[5]!=0;
        materials_.Store(std::move(cache_key),{},std::move(half),nullptr,result->capture.camera);
        resolved={std::move(result->capture),result->render.words[5]!=0};
      }
    } else if(const auto found=memo.find(key);found!=memo.end()) { resolved=found->second; ++stats.memo_hits; }
    else {
      auto constants=PassConstants(material,camera);
      auto* entry=materials_.Candidate(cache_key);
      NativeSceneView derived;
      if(entry) derived=entry->material.capture.camera;
      if(entry && Cache::Current(*entry,constants,entry->material.capture.material.get(),derived)) {
        resolved={entry->material.capture,entry->material.scissor};
        resolved.capture.camera.view=derived.view; resolved.capture.camera.projection=derived.projection;
        resolved.capture.camera.view_projection=derived.view_projection;
        ++materials_.hits; ++stats.cache_hits;
      } else {
        NativeFullFrameModelResolve half;
        if(entry) half=entry->material;
        exclusive([&] {
          if(entry) {
            // Same pipeline half; a constant moved: capture again.
            half.capture=program.Capture(*half.pipeline,pass.targets.reverse_depth,constants,half.samplers,half.blend_factor,false);
          } else {
            auto result=ResolveMaterial(program,*geometry,pass,base,constants,false);
            half.pipeline=result.capture.material->pipeline();
            half.samplers=ResolvedSamplers(program,result.samplers,pass.filtering);
            half.blend_factor=result.capture.material->blend_factor(); half.scissor=result.render.words[5]!=0;
            half.capture=std::move(result.capture);
          }
          if(sources.intern) half.capture.material=sources.intern(std::move(half.capture.material));
        });
        ++(entry?stats.captures:stats.resolves);
        ++materials_.misses;
        resolved={half.capture,half.scissor};
        const auto* captured=half.capture.material.get();
        const auto captured_camera=half.capture.camera;
        materials_.Store(std::move(cache_key),std::move(constants),std::move(half),captured,captured_camera);
      }
      memo.emplace(key,resolved);
    }
    // g_mWorld as 821A17D8 stores it; a palette shader need not declare it.
    if(!layout.skinned || NativeSceneCaptureBindsWorld(resolved.capture))
      ApplyNativeScenePublishedWorld(resolved.capture,values.worlds[draw.mesh]);
    auto object=std::make_shared<NativeSceneInstance>();
    object->id=++next_id_; object->changed_tick=UINT64_MAX;
    object->object.geometry=geometry; object->object.material=resolved.capture.material;
    object->object.world=resolved.capture.world; object->previous=resolved.capture.world;
    auto view=resolved.capture.camera;
    view.viewport=pass.viewport; view.scissor=pass.scissor; view.scissor_enabled=resolved.scissor;
    return Resolved{std::move(object),std::move(view)};
  };
  const auto emit=[&](std::span<const NativeFullFrameModelItem> items,std::span<const Gathered> sourced,bool transparent) {
    // Every draw of every item first; an item with any failure draws nothing.
    std::vector<std::vector<Resolved>> resolved(items.size());
    for(size_t index=0;index<items.size();++index) {
      const auto& item=items[index];
      const auto& layout=NativeFullFrameModelItemLayout(item);
      const auto& sourced_item=sourced[index];
      if(!sourced_item.complete) continue;
      try {
        const auto values=item.instanced<0?NativeFullFrameModelConstantsFor(layout,*item.entry->pose,pass.palette_limit):
          NativeFullFrameModelInstancedConstants(layout,item.entry->instanced[size_t(item.instanced)].worlds->at(item.world));
        std::vector<Resolved> draws;
        bool complete=true;
        for(size_t d=0;d<sourced_item.draws.size();++d) {
          auto result=resolve(layout,values,sourced_item.draws[d],sourced_item.sources[d]);
          if(!result) { complete=false; break; }
          draws.push_back(std::move(*result));
        }
        if(complete) { resolved[index]=std::move(draws); ++stats.drawn; }
        else resolved[index].clear();
      } catch(const std::exception&) { ++stats.failed; resolved[index].clear(); }
    }
    std::vector<std::shared_ptr<const NativeSceneInstance>> objects;
    NativeSceneView view;
    uint32_t current=0;
    // A transparent batch never spans items: each carries its item's key and
    // filing order, so it can merge with other producers' transparents.
    const auto flush=[&] {
      if(objects.empty()) return;
      frame.batches.push_back({view,NativeSceneSnapshot{0,NativeSceneInstances(objects)},transparent,
        transparent?items[current].key:uint16_t(0),transparent?current:0u});
      objects.clear();
    };
    for(const auto& ref:OrderNativeFullFrameModelDraws(items,!transparent)) {
      auto& draws=resolved[ref.item];
      if(draws.empty()) continue;
      auto& draw=draws.at(ref.index);
      if(!objects.empty() && (!SameView(view,draw.view) || (transparent && ref.item!=current))) flush();
      current=ref.item;
      view=draw.view; objects.push_back(std::move(draw.object));
      ++stats.draws;
    }
    flush();
  };
  emit(frame.plan.opaque,gathered[0],false);
  emit(frame.plan.transparent,gathered[1],true);
  sources_.EndFrame(); materials_.EndFrame();
  enter(Phase::Done);
  return frame;
}
NativeSceneRenderStatistics NativeFullFrameModels::Record(NativeRenderBackend& backend,NativeSceneRenderer& renderer,
    const NativeFullFrameModelFrame& frame) {
  NativeSceneRenderStatistics total;
  for(const auto& batch:frame.batches) {
    const auto statistics=renderer.Render(backend,batch.snapshot,batch.view,1);
    total.visible+=statistics.visible; total.culled+=statistics.culled;
    total.draws+=statistics.draws; total.instanced_draws+=statistics.instanced_draws;
  }
  return total;
}
NativeFullFrameModelFrame RecordNativeModels(NativeFullFrameModels& models,const NativeRenderRegistrySnapshot& snapshot,
    const NativeFullFrameModelCamera& camera,const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources,
    NativeRenderBackend& backend,NativeSceneRenderer& renderer,NativeSceneRenderStatistics* statistics) {
  auto frame=models.Build(snapshot,camera,pass,sources);
  const auto recorded=NativeFullFrameModels::Record(backend,renderer,frame);
  if(statistics) *statistics=recorded;
  return frame;
}
}
