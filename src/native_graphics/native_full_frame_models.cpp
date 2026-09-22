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
    if(!model) { ++stats.no_model; continue; }
    const auto& layout=*entry.models[*model].layout;
    if(!entry.pose || entry.pose->size()!=layout.bones) { ++stats.no_pose; continue; }
    NativeFullFrameModelItem item{&entry,*model,visibility.depth,visibility.centre[2],0,entry.mode!=0};
    if(!item.transparent) { plan.opaque.push_back(item); continue; }
    item.key=NativeFullFrameModelKey(entry.mode,item.view_z,entry.sort_bias,camera.key_scale,camera.key_offset);
    if(item.key<256) { ++stats.bucket_zero; continue; }
    plan.transparent.push_back(item);
  }
  std::stable_sort(plan.transparent.begin(),plan.transparent.end(),
    [](const auto& a,const auto& b) { return a.key>b.key; });
  stats.opaque=plan.opaque.size(); stats.transparent=plan.transparent.size();
  return plan;
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
    const auto& layout=*items[item].entry->models[items[item].model].layout;
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
      return item.entry->models[item.model].layout->meshes[ref.draw.mesh].batches[ref.draw.batch].address;
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
}
NativeFullFrameModelFrame NativeFullFrameModels::Build(const NativeRenderRegistrySnapshot& snapshot,
    const NativeFullFrameModelCamera& camera,const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources) {
  NativeFullFrameModelFrame frame;
  frame.plan=PlanNativeFullFrameModels(snapshot,camera);
  auto& stats=frame.stats;
  const auto base=NativeFullFrameModelBaseState(pass.targets);
  struct Memo { NativeSceneMaterialCapture capture; bool scissor=false; };
  std::map<std::tuple<uint32_t,const NativeSceneMaterialProgram*,const NativeIndexedMesh::RetainedDraw*>,Memo> memo;
  struct Resolved { std::shared_ptr<const NativeSceneInstance> object; NativeSceneView view; };
  // Resolves one draw, or throws / returns nothing (counted) when it cannot.
  const auto resolve=[&](const NativeModelLayout& layout,const NativeFullFrameModelConstants& values,
      const NativeModelDraw& draw) -> std::optional<Resolved> {
    const auto& batch=layout.meshes[draw.mesh].batches[draw.batch];
    const auto material=sources.program?sources.program(draw.pass):nullptr;
    if(!material || !material->program) { ++stats.missing_program; return std::nullopt; }
    const auto& program=*material->program;
    // Scissor enable's rectangle is not a pass input (as in the world pass).
    if(!program.CanDeferCpuActivation()) { ++stats.scissor; return std::nullopt; }
    const auto geometry=sources.geometry?sources.geometry(batch,draw.pass):nullptr;
    if(!geometry || !program.backend || geometry->backend()!=program.backend.get()) { ++stats.missing_geometry; return std::nullopt; }
    const auto key=std::tuple(draw.pass,&program,geometry.get());
    Memo resolved;
    if(const auto found=layout.skinned?memo.end():memo.find(key);found!=memo.end()) { resolved=found->second; ++stats.memo_hits; }
    else {
      auto constants=PassConstants(*material,camera);
      if(layout.skinned && !BindNativeFullFrameModelPalette(constants,values.palette)) { ++stats.palette; return std::nullopt; }
      auto result=ResolveMaterial(program,*geometry,pass,base,constants,layout.skinned);
      ++stats.resolves;
      if(!layout.skinned && sources.intern) result.capture.material=sources.intern(std::move(result.capture.material));
      resolved={std::move(result.capture),result.render.words[5]!=0};
      if(!layout.skinned) memo.emplace(key,resolved);
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
  const auto emit=[&](std::span<const NativeFullFrameModelItem> items,bool transparent) {
    // Every draw of every item first; an item with any failure draws nothing.
    std::vector<std::vector<Resolved>> resolved(items.size());
    for(size_t index=0;index<items.size();++index) {
      const auto& item=items[index];
      const auto& layout=*item.entry->models[item.model].layout;
      ++stats.items;
      try {
        const auto values=NativeFullFrameModelConstantsFor(layout,*item.entry->pose,pass.palette_limit);
        std::vector<Resolved> draws;
        bool complete=true;
        for(const auto& draw:NativeModelDrawPlan(layout)) {
          auto result=resolve(layout,values,draw);
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
  emit(frame.plan.opaque,false);
  emit(frame.plan.transparent,true);
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
