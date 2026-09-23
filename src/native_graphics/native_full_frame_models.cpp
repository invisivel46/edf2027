#include "native_full_frame_models.h"
#include <algorithm>
#include <cstring>

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
NativeFullFrameModelPlan PlanNativeFullFrameModels(const NativeRenderRegistrySnapshot& snapshot,const NativeFullFrameModelCamera& camera,
    const NativeFullFrameModelGather& gather) {
  using C=NativeFullFrameModelCull;
  NativeFullFrameModelPlan plan;
  auto& stats=plan.stats;
  // Gather order: a listed object at 2i+1, the unlisted at 2*unlisted (before
  // objects[unlisted]), ties (the unlisted) in snapshot order.
  std::vector<std::pair<uint64_t,const NativeRenderEntry*>> order;
  order.reserve(snapshot.entries.size());
  {
    std::unordered_map<uint32_t,uint32_t> rank;
    rank.reserve(gather.objects.size());
    for(uint32_t i=0;i<gather.objects.size();++i) rank.emplace(gather.objects[i],i);
    const uint64_t unlisted=2*uint64_t(std::min<size_t>(gather.unlisted,gather.objects.size()));
    for(const auto& shared:snapshot.entries) {
      if(!shared) continue;
      const auto found=rank.find(shared->object);
      if(found==rank.end()) ++stats.unlisted;
      order.emplace_back(found==rank.end()?unlisted:2*uint64_t(found->second)+1,shared.get());
    }
    std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b) { return a.first<b.first; });
  }
  // The filed entries whose slot 4 821A3BA0 calls, in filing order.
  std::vector<std::pair<uint16_t,const NativeRenderEntry*>> filed;
  for(const auto& [position,pointer]:order) {
    const auto& entry=*pointer;
    ++stats.entries;
    // Drawn by its own pass (clSky: the sky pass); the registry never
    // publishes one, and an entry that says so is still not drawn twice.
    if(entry.type && entry.type->other_pass) { ++stats.other_pass; continue; }
    const auto visibility=ClassifyNativeFullFrameModel(entry,camera.visibility);
    switch(visibility.cull) {
      case C::Visible: break;
      case C::Hidden: ++stats.hidden; continue;
      case C::Mode: ++stats.mode; continue;
      case C::Distance: ++stats.distance; continue;
      case C::Frustum: ++stats.frustum; continue;
      case C::Box: ++stats.box; continue;
    }
    // 821C0C00: mode 0 calls slot 4 now; 1/2 file it, and 821A3BA0 calls it
    // after the walks unless the key is below 256.
    if(entry.mode==0) plan.calls.push_back(&entry);
    else {
      const auto key=NativeFullFrameModelKey(entry.mode,visibility.centre[2],entry.sort_bias,camera.key_scale,camera.key_offset);
      if(key>=256) filed.emplace_back(key,&entry);
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
    // An attachment (820DB268 face, 820E1A80 weapon) draws once its model
    // decoded for its own pose vector and the pose is that size.
    const auto attached=[](const NativeRenderAttachment& attachment) {
      return attachment.model.instance && attachment.model.layout && !attachment.model.layout->single_world &&
        attachment.pose && attachment.pose->size()==attachment.model.layout->bones;
    };
    size_t attachments=0;
    for(const auto& attachment:entry.attachments) { if(attached(attachment)) ++attachments; else ++stats.no_attachment; }
    if(!posed && !instances && !attachments) continue;
    NativeFullFrameModelItem item{&entry,model.value_or(0),visibility.depth,visibility.centre[2],0,entry.mode!=0};
    if(item.transparent) {
      item.key=NativeFullFrameModelKey(entry.mode,item.view_z,entry.sort_bias,camera.key_scale,camera.key_offset);
      if(item.key<256) { ++stats.bucket_zero; continue; }
    }
    auto& list=item.transparent?plan.transparent:plan.opaque;
    if(posed) list.push_back(item);
    // 820DEA08: 8210AE48's model, the face (820DB268), then the weapons (820DE790).
    for(size_t index=0;index<entry.attachments.size();++index) {
      if(!attached(entry.attachments[index])) continue;
      auto attachment=item; attachment.attachment=int32_t(index);
      list.push_back(attachment); ++stats.attachments;
    }
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
  std::stable_sort(filed.begin(),filed.end(),[](const auto& a,const auto& b) { return a.first>b.first; });
  for(const auto& [key,entry]:filed) plan.calls.push_back(entry);
  stats.opaque=plan.opaque.size(); stats.transparent=plan.transparent.size(); stats.calls=plan.calls.size();
  return plan;
}
void ApplyNativeFullFrameModelPool(std::vector<NativeRenderObjectConstant>& state,const NativeRenderConstants& stores) {
  if(!stores) return;
  for(const auto& store:*stores) {
    const auto same=std::find_if(state.begin(),state.end(),[&](const auto& c) { return c.name==store.name; });
    if(same!=state.end()) same->registers=store.registers;
    else state.push_back(store);
  }
}
NativeFullFrameModelPoolCarry CarryNativeFullFrameModelPool(const NativeFullFrameModelPlan& plan,
    std::span<const NativeRenderObjectConstant> start) {
  NativeFullFrameModelPoolCarry carry;
  carry.end.assign(start.begin(),start.end());
  NativeRenderConstants current=carry.end.empty()?nullptr:std::make_shared<const std::vector<NativeRenderObjectConstant>>(carry.end);
  carry.before.reserve(plan.calls.size());
  for(const auto* entry:plan.calls) {
    carry.before.insert_or_assign(entry,current);
    bool stores=entry->constants!=nullptr;
    for(const auto& attachment:entry->attachments) stores=stores || attachment.constants!=nullptr;
    if(!stores) continue;
    const auto previous=carry.end;
    ApplyNativeFullFrameModelPool(carry.end,entry->constants);
    for(const auto& attachment:entry->attachments) ApplyNativeFullFrameModelPool(carry.end,attachment.constants);
    if(carry.end!=previous) current=std::make_shared<const std::vector<NativeRenderObjectConstant>>(carry.end);
  }
  return carry;
}
std::vector<NativeRenderObjectConstant> NativeFullFrameModelEffectiveConstants(const NativeRenderConstants& before,
    const NativeRenderConstants& own) {
  std::vector<NativeRenderObjectConstant> result;
  if(before) result=*before;
  ApplyNativeFullFrameModelPool(result,own);
  return result;
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
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameModelObjectConstants(
    std::span<const NativeSceneMaterialInputs::Constant> pass,std::span<const NativeRenderObjectConstant> objects) {
  std::vector<NativeSceneMaterialInputs::Constant> result;
  if(objects.empty()) return result;
  for(const auto& constant:pass) {
    if(!constant.global || constant.registers.size()<16) continue;
    const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto& o) { return o.name==constant.name; });
    if(object==objects.end()) continue;
    auto& replaced=result.emplace_back(constant);
    std::copy(object->registers.begin(),object->registers.end(),replaced.registers.begin());
  }
  return result;
}
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameModelObjectConstants(
    std::span<const NativeSceneMaterialInputs::Constant> pass,std::span<const uint32_t> slots,
    std::span<const NativeRenderObjectConstant> objects) {
  std::vector<NativeSceneMaterialInputs::Constant> result;
  if(objects.empty()) return result;
  for(const auto slot:slots) {
    if(slot>=pass.size()) throw std::runtime_error("native full-frame model object slot outside the pass constants");
    const auto& constant=pass[slot];
    if(!constant.global || constant.registers.size()<16) continue;
    const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto& o) { return o.name==constant.name; });
    if(object==objects.end()) continue;
    auto& replaced=result.emplace_back(constant);
    std::copy(object->registers.begin(),object->registers.end(),replaced.registers.begin());
  }
  return result;
}
bool BindNativeFullFrameModelPalette(NativeSceneMaterialInputs::Constant& constant,std::span<const float> palette) {
  if(!constant.global || constant.pixel || constant.registers.size()%16 || palette.size()*4>constant.registers.size()) return false;
  std::fill(constant.registers.begin(),constant.registers.end(),uint8_t(0));
  for(size_t i=0;i<palette.size();++i) StoreNativeGuestFloat(constant.registers.data()+i*4,palette[i]);
  return true;
}
bool BindNativeFullFrameModelPalette(std::vector<NativeSceneMaterialInputs::Constant>& constants,std::span<const float> palette) {
  for(auto& constant:constants)
    if(constant.name=="g_mWorldArray" && !BindNativeFullFrameModelPalette(constant,palette)) return false;
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
// Whether two sets of an item's constants are the same bytes (the palette
// floats compared as bits: a NaN is itself, -0 is not 0).
bool SameConstants(const NativeFullFrameModelConstants& a,const NativeFullFrameModelConstants& b) {
  return a.skinned==b.skinned && a.bones==b.bones && a.worlds==b.worlds && a.palette.size()==b.palette.size() &&
    (a.palette.empty() || !std::memcmp(a.palette.data(),b.palette.data(),a.palette.size()*sizeof(float)));
}
}
NativeFullFrameModelFrame NativeFullFrameModels::Build(const NativeRenderRegistrySnapshot& snapshot,
    const NativeFullFrameModelCamera& camera,const NativeFullFrameModelPass& pass,const NativeFullFrameModelSources& sources,
    const std::function<void(NativeFullFrameModelPhase)>& phase) {
  using Phase=NativeFullFrameModelPhase;
  using Cache=NativeFullFrameModelMaterialCache<NativeFullFrameModelResolve>;
  using ItemState=NativeFullFrameModelItemState;
  using RowState=NativeFullFrameModelRowState;
  const auto enter=[&](Phase next) { if(phase) phase(next); };
  NativeFullFrameModelFrame frame;
  auto& stats=frame.stats;
  ++frame_;
  if(!NativeReuseAllowed()) {
    // Reuse off (native_reuse.h): every draw state, carried object, material
    // row, cached resolve and side-table answer is dropped and made again
    // below, as on a first frame; poses_ (pose motion) is kept and asked
    // afresh (its own predicate). The ids keep counting.
    items_.clear(); rows_.clear(); sources_.Clear(); materials_.Clear();
    provided_programs_.clear(); provided_geometry_.clear(); provided_generation_=kNativeFullFrameModelUnversioned;
  }
  enter(Phase::Visibility);
  frame.plan=PlanNativeFullFrameModels(snapshot,camera,pass.gather);
  // The pool carry: what each slot-4 call finds in the pool, from the pool as
  // this view starts (NativeFullFrameModelPass::tick_frame, view,
  // guest_frames). Renders whose slot 4s were the guest's left their stores
  // in the guest pool: every name the carry knows is taken from it again.
  if(pass.guest_frames!=pool_guest_frames_) {
    if(pass.pool)
      for(auto& constant:pool_committed_)
        if(const auto value=pass.pool(constant.name)) { constant.registers=*value; ++stats.reseeds; }
    pool_guest_frames_=pass.guest_frames;
    pool_tick_start_=pool_view_end_=pool_committed_;
  }
  if(pass.view==0 && pass.tick_frame) pool_tick_start_=pool_committed_;
  const auto carry=CarryNativeFullFrameModelPool(frame.plan,pass.view>0?pool_view_end_:pass.tick_frame?pool_committed_:pool_tick_start_);
  pool_view_end_=carry.end;
  if(pass.tick_frame) pool_committed_=carry.end;
  for(const auto& constant:carry.end)
    if(std::find(pool_names_.begin(),pool_names_.end(),constant.name)==pool_names_.end()) {
      pool_names_.push_back(constant.name); ++pool_names_version_;
    }
  // Programs: each drawn item's draw state, found by (object, generation, LOD
  // model or instanced world). Its draws and their program and geometry are
  // gathered only when the state is new, its layout object changed or the
  // source generation moved: the side table then serves each (pass record,
  // batch, layout), and behind it the providers are asked once per pass
  // record, and per pass record and batch value, per generation. That is the
  // providers' contract (NativeFullFrameModelSources::generation): at one
  // generation a program is a function of its pass record and a geometry of
  // its batch and pass record. Missing answers are asked again.
  enter(Phase::Programs);
  // Reuse off: unversioned, so every draw asks the providers and no answer is kept.
  const auto generation=sources.generation && NativeReuseAllowed()?sources.generation():kNativeFullFrameModelUnversioned;
  const bool versioned=generation!=kNativeFullFrameModelUnversioned;
  if(generation!=provided_generation_ || !versioned) {
    provided_programs_.clear(); provided_geometry_.clear(); provided_generation_=generation;
  }
  const auto program_of=[&](uint32_t record) -> std::shared_ptr<const NativeSceneGroupMaterial> {
    if(!sources.program) return nullptr;
    if(versioned) if(const auto found=provided_programs_.find(record);found!=provided_programs_.end()) return found->second;
    auto program=sources.program(record);
    ++stats.programs;
    if(versioned && program && program->program) provided_programs_.emplace(record,program);
    return program;
  };
  const auto geometry_of=[&](const NativeModelBatchLayout& batch,uint32_t record) {
    std::vector<std::pair<NativeModelBatchLayout,std::shared_ptr<const NativeIndexedMesh::RetainedDraw>>>* provided=nullptr;
    if(versioned) {
      provided=&provided_geometry_[{record,batch.address}];
      for(const auto& [value,geometry]:*provided) if(value==batch) return geometry;
    }
    auto geometry=sources.geometry(batch,record);
    ++stats.geometries;
    if(provided && geometry) provided->emplace_back(batch,geometry);
    return geometry;
  };
  const auto source_hits=sources_.hits,source_fetches=sources_.fetches;
  const std::array<std::span<const NativeFullFrameModelItem>,2> lists{
    std::span<const NativeFullFrameModelItem>(frame.plan.opaque),std::span<const NativeFullFrameModelItem>(frame.plan.transparent)};
  // Each list's item states in list order; null for an item without complete
  // sources. An entry the registry published twice (it never does) gets a
  // state of this frame only for its second item, so neither sees the other's.
  std::array<std::vector<ItemState*>,2> states;
  std::vector<std::unique_ptr<ItemState>> duplicates;
  for(size_t list=0;list<lists.size();++list) {
    states[list].resize(lists[list].size());
    for(size_t index=0;index<lists[list].size();++index) {
      const auto& item=lists[list][index];
      ++stats.items;
      // The item's layout object: the posed LOD model's, its attachment's, or
      // its instanced set's.
      const auto& layout_object=NativeFullFrameModelItemLayoutObject(item);
      const auto& layout=*layout_object;
      auto* state=&items_[item.attachment>=0?
        ItemKey{item.entry->object,item.entry->generation,-2,item.entry->attachments[size_t(item.attachment)].pose_vector}:
        ItemKey{item.entry->object,item.entry->generation,item.instanced,item.instanced<0?item.model:item.world}];
      if(state->used==frame_) state=duplicates.emplace_back(std::make_unique<ItemState>()).get();
      state->used=frame_;
      try {
        if(state->layout!=layout_object) {
          // New, or relaid out (a new layout generation): its draws in guest
          // order, each with its batch's address and its index in the batch's
          // pass list (OrderNativeFullFrameModelDraws' keys).
          *state=ItemState{layout_object};
          state->used=frame_;
          for(const auto& draw:NativeModelDrawPlan(layout)) {
            const auto& passes=layout.meshes[draw.mesh].batches[draw.batch].passes;
            state->draws.push_back({draw,uint32_t(std::find(passes.begin(),passes.end(),draw.pass)-passes.begin()),
              layout.meshes[draw.mesh].batches[draw.batch].address});
          }
        }
        if(!state->sourced || state->generation!=generation || !versioned) {
          ++stats.sourced;
          state->sourced=false;
          bool complete=true;
          for(auto& draw:state->draws) {
            const auto& batch=layout.meshes[draw.draw.mesh].batches[draw.draw.batch];
            auto source=sources_.Get(draw.draw.pass,batch.address,layout_object,generation,[&] {
              NativeFullFrameModelSourcePair fetched{program_of(draw.draw.pass),nullptr};
              if(fetched.first && fetched.first->program && sources.geometry) fetched.second=geometry_of(batch,draw.draw.pass);
              return fetched;
            },[](const NativeFullFrameModelSourcePair& value) { return value.first && value.first->program && value.second; });
            if(!source.first || !source.first->program) { ++stats.missing_program; complete=false; break; }
            if(!source.second) { ++stats.missing_geometry; complete=false; break; }
            // Other sources are another row: the object is made again.
            if(source!=draw.source) { draw.object.reset(); draw.made_from.reset(); draw.source=std::move(source); }
          }
          state->sourced=complete; state->generation=generation;
        } else if(sources.audit) {
          for(const auto& draw:state->draws)
            sources.audit(layout.meshes[draw.draw.mesh].batches[draw.draw.batch],draw.draw.pass,draw.source);
        }
      } catch(const std::exception&) { ++stats.failed; state->sourced=false; }
      if(state->sourced) states[list][index]=state;
    }
  }
  stats.source_hits=sources_.hits-source_hits; stats.source_fetches=sources_.fetches-source_fetches;
  // Resolve: each material row once (its pass constants against the cache:
  // the camera derived from them, or a capture or resolve), then each draw's
  // scene object: carried from its state while nothing it is made from moved,
  // else made from its row's result and its item's constants.
  enter(Phase::Resolve);
  const auto base=NativeFullFrameModelBaseState(pass.targets);
  // Each resolve or capture (the backend's pipeline and sampler caches) with
  // its intern runs in one exclusive call: one short hold of the host's locks.
  // The cache rows are Build's own state and are used outside it.
  const auto exclusive=[&](const std::function<void()>& work) { if(sources.exclusive) sources.exclusive(work); else work(); };
  // The row of one draw, evaluated at its first draw this frame. A row that
  // could not be evaluated throws its error for each of its draws, as each
  // draw's own resolve would; one whose program cannot defer its CPU
  // activation or whose geometry is another backend's is not evaluated (the
  // draw declines first).
  const auto row_of=[&](bool skinned,const NativeModelDraw& draw,const NativeFullFrameModelSourcePair& source) -> RowState& {
    const auto& material=source.first;
    const auto& program=*material->program;
    auto& row=rows_[RowKey{draw.pass,&program,source.second.get(),skinned}];
    if(row.frame==frame_) {
      if(row.failed) throw std::runtime_error(row.error);
      return row;
    }
    if(!row.program) {
      // Fixed by the key's program and geometry, which the row holds.
      row.program=material->program; row.geometry=source.second;
      // Scissor enable's rectangle is not a pass input (as in the world pass).
      row.deferrable=program.CanDeferCpuActivation();
      row.same_backend=program.backend && source.second->backend()==program.backend.get();
    }
    row.frame=frame_; row.used=frame_; row.draws=0; row.failed=false;
    if(!row.deferrable || !row.same_backend) return row;
    ++stats.rows;
    try {
      // The pass constants: the published constants with the camera and
      // animation applied (PassConstants). While the published material is
      // the same object and the animation it reads is the same, only the
      // camera constants can differ, and the camera writes each of them whole:
      // it is applied in place.
      if(row.group!=material || row.constants.empty() || (row.animated && row.animation!=camera.animation)) {
        row.group.reset(); row.palette_constants.clear(); row.object_names=0;
        row.constants=PassConstants(*material,camera);
        row.animated=std::any_of(row.constants.begin(),row.constants.end(),[](const auto& constant) {
          return constant.global && (constant.name=="m_WaterTime" || constant.name=="g_SignalBrightness");
        });
        // A skinned draw binds its palette into these (every g_mWorldArray).
        if(skinned) for(const auto& constant:row.constants) if(constant.name=="g_mWorldArray") row.palette_constants.push_back(constant);
        row.group=material; row.animation=camera.animation; row.camera=camera.pass;
      } else {
        if(!(row.camera==camera.pass)) { for(auto& constant:row.constants) camera.pass.Apply(constant); row.camera=camera.pass; }
        ++stats.camera_rows;
      }
      // The globals a draw's pool constants bind: those of a name the carry knows.
      if(row.object_names!=pool_names_version_) {
        row.object_slots.clear();
        for(uint32_t i=0;i<row.constants.size();++i) {
          const auto& constant=row.constants[i];
          if(constant.global && constant.registers.size()>=16 &&
             std::find(pool_names_.begin(),pool_names_.end(),constant.name)!=pool_names_.end()) row.object_slots.push_back(i);
        }
        row.object_names=pool_names_version_;
      }
      Cache::Key cache_key{draw.pass,skinned,material->program,source.second,base,pass.targets,pass.filtering};
      auto* entry=materials_.Candidate(cache_key);
      NativeSceneView derived;
      if(skinned) {
        // The row's capture of its pass constants (any palette), cached across
        // frames while Current; each draw derives its own material from it.
        const auto* cached=entry && entry->material.palette?entry->material.palette.get():nullptr;
        if(cached) derived=cached->capture().camera;
        if(cached && Cache::Current(*entry,row.constants,cached->capture().material.get(),derived)) {
          row.palette=entry->material.palette; row.scissor=entry->material.scissor;
          row.pipeline=entry->material.pipeline; row.samplers=entry->material.samplers;
          row.blend_factor=entry->material.blend_factor;
          ++materials_.hits; ++stats.cache_hits;
        } else {
          NativeFullFrameModelResolve half;
          if(entry) half=entry->material;
          exclusive([&] {
            if(!entry) {
              const auto result=ResolveMaterial(program,*source.second,pass,base,row.constants,true);
              half.pipeline=result.capture.material->pipeline(); half.blend_factor=result.capture.material->blend_factor();
              half.samplers=ResolvedSamplers(program,result.samplers,pass.filtering); half.scissor=result.render.words[5]!=0;
            }
            half.palette=std::make_shared<NativeScenePaletteCapture>(program,*half.pipeline,pass.targets.reverse_depth,
              row.constants,half.samplers,half.blend_factor);
          });
          ++(entry?stats.captures:stats.resolves);
          ++materials_.misses;
          row.palette=half.palette; row.scissor=half.scissor; derived=half.palette->capture().camera;
          row.pipeline=half.pipeline; row.samplers=half.samplers; row.blend_factor=half.blend_factor;
          materials_.Store(std::move(cache_key),row.constants,std::move(half),row.palette->capture().material.get(),derived);
        }
        // The pipeline half stays for reuse-off draws' full captures.
        row.capture={};
      } else {
        if(entry) derived=entry->material.capture.camera;
        if(entry && Cache::Current(*entry,row.constants,entry->material.capture.material.get(),derived)) {
          row.capture=entry->material.capture; row.scissor=entry->material.scissor;
          row.pipeline=entry->material.pipeline; row.samplers=entry->material.samplers;
          row.blend_factor=entry->material.blend_factor;
          ++materials_.hits; ++stats.cache_hits;
        } else {
          NativeFullFrameModelResolve half;
          if(entry) half=entry->material;
          exclusive([&] {
            if(entry) {
              // Same pipeline half; a constant moved: capture again.
              half.capture=program.Capture(*half.pipeline,pass.targets.reverse_depth,row.constants,half.samplers,half.blend_factor,false);
            } else {
              auto result=ResolveMaterial(program,*source.second,pass,base,row.constants,false);
              half.pipeline=result.capture.material->pipeline();
              half.samplers=ResolvedSamplers(program,result.samplers,pass.filtering);
              half.blend_factor=result.capture.material->blend_factor(); half.scissor=result.render.words[5]!=0;
              half.capture=std::move(result.capture);
            }
            if(sources.intern) half.capture.material=sources.intern(std::move(half.capture.material));
          });
          ++(entry?stats.captures:stats.resolves);
          ++materials_.misses;
          row.capture=half.capture; row.scissor=half.scissor; derived=half.capture.camera;
          row.pipeline=half.pipeline; row.samplers=half.samplers; row.blend_factor=half.blend_factor;
          const auto* captured=half.capture.material.get();
          materials_.Store(std::move(cache_key),row.constants,std::move(half),captured,derived);
        }
        row.palette.reset();
      }
      // The view every draw of the row records with this frame.
      row.view=NativeSceneView{};
      row.view.view=derived.view; row.view.projection=derived.projection; row.view.view_projection=derived.view_projection;
      row.view.viewport=pass.viewport; row.view.scissor=pass.scissor; row.view.scissor_enabled=row.scissor;
    } catch(const std::exception& error) {
      // Evaluated again next frame from the published constants.
      row.failed=true; row.error=error.what();
      row.group.reset(); row.constants.clear(); row.palette_constants.clear(); row.object_slots.clear(); row.object_names=0;
      throw;
    }
    return row;
  };
  const auto make=[&](const NativeFullFrameModelDrawState& draw,const NativeSceneMaterialCapture& capture) {
    auto object=std::make_shared<NativeSceneInstance>();
    object->id=++next_id_; object->changed_tick=UINT64_MAX;
    object->object.geometry=draw.source.second; object->object.material=capture.material;
    object->object.world=capture.world; object->previous=capture.world;
    ++stats.derived;
    return object;
  };
  const auto emit=[&](std::span<const NativeFullFrameModelItem> items,std::span<ItemState* const> item_states,bool transparent) {
    // Every draw of every item first; an item with any failure draws nothing.
    // views[item][draw]: the row view the draw records with; empty when the
    // item is not drawn.
    std::vector<std::vector<const NativeSceneView*>> views(items.size());
    for(size_t index=0;index<items.size();++index) {
      auto* state=item_states[index];
      if(!state) continue;
      const auto& item=items[index];
      const auto& layout=*state->layout;
      try {
        // The item's constants (worlds, palette) when their inputs moved (the
        // pose, and in unlocked mode the blend: previous pose and fraction);
        // if they moved by value, every object made from them goes.
        const auto [pose_of,motion_of]=NativeFullFrameModelItemPose(item);
        const auto& pose=*pose_of;
        const uint32_t world=item.instanced<0?0:item.world;
        const auto blend=NativeRenderBlendOf(pose,*motion_of,pass.motion);
        if(blend.previous) ++stats.blended;
        if(!state->valued || state->pose!=pose || state->blend!=blend || state->world!=world || state->palette_limit!=pass.palette_limit) {
          // Pose source: the published pose, or blended from the previous tick's.
          auto values=item.instanced<0?NativeFullFrameModelConstantsFor(layout,poses_.Pose(pose,*motion_of,pass.motion),pass.palette_limit):
            NativeFullFrameModelInstancedConstants(layout,poses_.Matrix(pose,*motion_of,world,pass.motion));
          if(!state->valued || !SameConstants(values,state->values))
            for(auto& draw:state->draws) { draw.object.reset(); draw.made_from.reset(); }
          state->values=std::move(values); state->pose=pose; state->blend=blend; state->world=world; state->palette_limit=pass.palette_limit;
          state->valued=true;
        }
        const auto& values=state->values;
        // The pool constants in effect at the item's draw: its entry's slot 4
        // found the carried pool, its own stores (the entry's, a part's) over
        // it. Made at the first draw whose material reads one; each such draw
        // binds the ones its material reads.
        const auto& own=NativeFullFrameModelItemConstants(item);
        std::optional<std::vector<NativeRenderObjectConstant>> effective;
        const auto bound_of=[&](const RowState& row) {
          if(row.object_slots.empty()) return std::vector<NativeSceneMaterialInputs::Constant>{};
          if(!effective) {
            const auto before=carry.before.find(item.entry);
            effective=NativeFullFrameModelEffectiveConstants(before==carry.before.end()?nullptr:before->second,own);
          }
          return NativeFullFrameModelObjectConstants(row.constants,row.object_slots,*effective);
        };
        const auto carried=[&](const std::vector<NativeSceneMaterialInputs::Constant>& bound) {
          for(const auto& constant:bound)
            if(!own || std::none_of(own->begin(),own->end(),[&](const auto& o) { return o.name==constant.name; })) return true;
          return false;
        };
        auto& drawn=views[index];
        drawn.assign(state->draws.size(),nullptr);
        bool complete=true;
        for(size_t d=0;d<state->draws.size() && complete;++d) {
          auto& draw=state->draws[d];
          auto& row=row_of(layout.skinned,draw.draw,draw.source);
          if(!row.deferrable) { ++stats.scissor; complete=false; break; }
          if(!row.same_backend) { ++stats.missing_geometry; complete=false; break; }
          auto bound=bound_of(row);
          if(layout.skinned) {
            if(draw.object && draw.made_from.get()==row.palette.get() && draw.bound==bound) ++stats.reused;
            else {
              // The palette is per draw, everything else per row: With over
              // its palette-bound g_mWorldArray. Refused before any capture,
              // as BindNativeFullFrameModelPalette over the whole set.
              for(auto& constant:row.palette_constants)
                if(!BindNativeFullFrameModelPalette(constant,values.palette)) { complete=false; break; }
              if(!complete) { ++stats.palette; break; }
              // Pool constants rebind like the palette (after it: none is
              // g_mWorldArray, and the last of a name wins in With).
              auto objects=bound;
              if(!objects.empty()) {
                ++stats.object_constants;
                if(carried(bound)) ++stats.carried;
              }
              NativeSceneMaterialCapture capture;
              if(!NativeReuseAllowed()) {
                // Reuse off (native_reuse.h): the full capture With stands
                // for, of the row's pass constants with this draw's palette
                // and pool constants bound, against its pipeline half.
                if(!row.pipeline) throw std::runtime_error("native full-frame model row has no pipeline half");
                auto full=row.constants;
                size_t next=0;
                for(auto& constant:full)
                  if(constant.name=="g_mWorldArray" && next<row.palette_constants.size()) constant=row.palette_constants[next++];
                for(const auto& object:objects)
                  for(auto& constant:full)
                    if(constant.global && constant.pixel==object.pixel && constant.name==object.name) constant=object;
                exclusive([&] {
                  capture=row.program->Capture(*row.pipeline,pass.targets.reverse_depth,full,row.samplers,row.blend_factor,true);
                });
              } else {
                if(!objects.empty()) objects.insert(objects.begin(),row.palette_constants.begin(),row.palette_constants.end());
                capture=row.palette->With(objects.empty()?std::span<const NativeSceneMaterialInputs::Constant>(row.palette_constants):
                  std::span<const NativeSceneMaterialInputs::Constant>(objects));
              }
              // g_mWorld as 821A17D8 stores it; a palette shader need not declare it.
              if(NativeSceneCaptureBindsWorld(capture)) ApplyNativeScenePublishedWorld(capture,values.worlds[draw.draw.mesh]);
              draw.object=make(draw,capture); draw.made_from=row.palette; draw.bound=std::move(bound);
              ++stats.palettes;
            }
          } else {
            if(row.draws++) ++stats.memo_hits;
            if(draw.object && draw.made_from.get()==row.capture.material.get() && draw.bound==bound) ++stats.reused;
            else {
              auto capture=row.capture;
              // Pool constants: the row's constants with the draw's bound,
              // captured against the row's pipeline half (the row's material
              // is shared by every draw of it, so never patched).
              if(!bound.empty()) {
                if(!row.pipeline) throw std::runtime_error("native full-frame model row has no pipeline half");
                auto constants=row.constants;
                for(const auto& object:bound)
                  for(auto& constant:constants)
                    if(constant.global && constant.pixel==object.pixel && constant.name==object.name) constant=object;
                exclusive([&] {
                  capture=row.program->Capture(*row.pipeline,pass.targets.reverse_depth,constants,row.samplers,row.blend_factor,false);
                  if(sources.intern) capture.material=sources.intern(std::move(capture.material));
                });
                ++stats.object_constants;
                if(carried(bound)) ++stats.carried;
              }
              ApplyNativeScenePublishedWorld(capture,values.worlds[draw.draw.mesh]);
              draw.object=make(draw,capture); draw.made_from=row.capture.material; draw.bound=std::move(bound);
            }
          }
          drawn[d]=&row.view;
        }
        if(complete) ++stats.drawn;
        else drawn.clear();
      } catch(const std::exception&) { ++stats.failed; views[index].clear(); }
    }
    // OrderNativeFullFrameModelDraws over the drawn items (a stable order of a
    // subsequence is the subsequence of the stable order), from the states.
    struct Ref { uint32_t item,index,pass_index,batch,pass; };
    std::vector<Ref> refs;
    for(uint32_t item=0;item<items.size();++item) {
      if(views[item].empty()) continue;
      const auto& draws=item_states[item]->draws;
      for(uint32_t index=0;index<draws.size();++index)
        refs.push_back({item,index,draws[index].pass_index,draws[index].batch,draws[index].draw.pass});
    }
    if(!transparent)
      std::stable_sort(refs.begin(),refs.end(),[](const Ref& a,const Ref& b) {
        return std::tie(a.pass_index,a.batch,a.pass)<std::tie(b.pass_index,b.batch,b.pass);
      });
    std::vector<std::shared_ptr<const NativeSceneInstance>> objects;
    const NativeSceneView* view=nullptr;
    uint32_t current=0;
    // A transparent batch never spans items: each carries its item's key and
    // filing order, so it can merge with other producers' transparents.
    const auto flush=[&] {
      if(objects.empty()) return;
      frame.batches.push_back({*view,NativeSceneSnapshot{0,NativeSceneInstances(objects)},transparent,
        transparent?items[current].key:uint16_t(0),transparent?current:0u});
      objects.clear();
    };
    for(const auto& ref:refs) {
      const auto* next=views[ref.item][ref.index];
      if(!objects.empty() && ((view!=next && !SameView(*view,*next)) || (transparent && ref.item!=current))) flush();
      current=ref.item;
      view=next; objects.push_back(item_states[ref.item]->draws[ref.index].object);
      ++stats.draws;
    }
    flush();
  };
  emit(frame.plan.opaque,states[0],false);
  emit(frame.plan.transparent,states[1],true);
  sources_.EndFrame(); materials_.EndFrame(); poses_.EndFrame();
  // Draw states and rows of objects, layouts, programs or geometry no longer
  // drawn release what they hold.
  if(items_.size()>kItemLimit) items_.clear();
  if(rows_.size()>kRowLimit) rows_.clear();
  if(frame_%kStateAge==0) {
    std::erase_if(items_,[&](const auto& item) { return frame_-item.second.used>kStateAge; });
    std::erase_if(rows_,[&](const auto& row) { return frame_-row.second.used>kStateAge; });
  }
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
