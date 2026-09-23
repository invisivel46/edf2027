#include "native_full_frame_static_world.h"
#include <algorithm>
#include <map>

namespace edf::native {
NativeFullFrameStaticSelection SelectNativeFullFrameStaticWorld(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route) {
  NativeFullFrameStaticSelection result;
  auto& stats=result.stats;
  const auto* sources=publication.sources.get();
  const auto* membership=publication.membership.get();
  const auto& view=camera.visibility;
  // The guest's +48 marker: an owner straddling several leaves is visited once
  // per frame, at its first list.
  std::unordered_set<uint32_t> seen;
  std::vector<uint32_t> lists;
  for(const auto& [owner,image]:publication.trees) {
    if(!image) continue;
    ++stats.worlds;
    const auto* order_slot=publication.group_order.Find(owner);
    const auto order=order_slot?*order_slot:nullptr;
    if(!order) ++stats.missing_orders;
    // Frame-local queues of this world, in selection (push) order.
    std::map<uint32_t,std::vector<uint32_t>> queues;
    const NativeSceneTreeImageReader tree(*image);
    lists.clear();
    WalkNativeSceneTree(tree,owner,[&](uint32_t node) {
      ++stats.nodes_classified;
      return ClassifyNativeSceneTreeNode(tree,node,view).result;
    },[&](uint32_t list) { lists.push_back(list); });
    stats.tree_reads+=tree.reads;
    // 820B4310 gathers world+372 (non-octree map objects) after the octree.
    lists.push_back(uint32_t(owner+372));
    for(const auto list:lists) {
      ++stats.lists;
      const NativeSceneMembership::Snapshot* members=nullptr;
      if(membership) if(const auto* found=membership->lists.Find(list)) members=found->get();
      if(!members) { ++stats.missing_lists; continue; }
      for(const auto& member:members->members) {
        ++stats.members;
        if(!seen.insert(member.owner).second) { ++stats.duplicates; continue; }
        const auto candidate=sources?sources->FindCandidateView(member.owner):NativeSceneSources::CandidateView{};
        if(!candidate.visibility || !candidate.visibility->lod_count) { ++stats.unpublished; continue; }
        const auto& object=*candidate.visibility;
        const auto selection=SelectNativeVisibility(view,object,NativeVisibilityCenter(view,object));
        if(!selection.in_range) { ++stats.culled_distance; continue; }
        if(!selection.visible()) { ++stats.culled_frustum; continue; }
        // The route words, live, for culling's survivors only (every filter
        // here only drops objects, so the order changes no selection).
        ++stats.route_reads;
        const auto words=route(member.owner);
        if(!words) { ++stats.unrouted; continue; }
        if(ClassifyNativeStaticWalk(words->hidden,words->mode,words->direct)!=NativeStaticWalkRoute::Direct) {
          ++stats.not_direct; continue;
        }
        ++stats.visible;
        const auto parts=candidate.Lod(selection.lod);
        if(!parts) { ++stats.missing_lod; continue; }
        if(std::ranges::any_of(*parts,[](const auto& part) { return !part.group; })) { ++stats.undrawable; continue; }
        for(const auto& part:*parts) queues[part.group].push_back(part.instance);
        stats.parts+=parts->size(); ++stats.selected;
        result.objects.push_back({member.owner,selection.lod});
      }
    }
    NativeFullFrameStaticOwner drawn{owner,{}};
    if(order) for(const auto group:*order) {
      const auto found=queues.find(group);
      if(found==queues.end()) continue;
      // Retail links each selection at the group's head: last selected, first drawn.
      drawn.groups.push_back({group,std::vector<uint32_t>(found->second.rbegin(),found->second.rend())});
      queues.erase(found);
    }
    for(const auto& [group,instances]:queues) { ++stats.unordered_groups; stats.unordered_parts+=instances.size(); }
    if(!drawn.groups.empty()) result.owners.push_back(std::move(drawn));
  }
  return result;
}
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameStaticConstants(const NativeSceneGroupMaterial& group,
    const NativeFullFrameStaticCamera& camera) {
  auto constants=group.constants;
  for(auto& constant:constants) {
    if(camera.pass.Apply(constant) || !constant.global ||
       (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
    if(!camera.animation) throw std::runtime_error("native full-frame static material needs the world animation");
    camera.animation->Apply(constant);
  }
  return constants;
}
NativeFullFrameStaticMaterial ResolveNativeFullFrameStaticMaterial(const NativeSceneMaterialProgram& program,
    const NativeIndexedMesh::RetainedDraw& geometry,const NativeFullFrameStaticPass& pass,
    const NativeSceneMaterialPassState& base,std::span<const NativeSceneMaterialInputs::Constant> constants,
    const std::function<std::shared_ptr<const NativeSceneMaterial>(std::shared_ptr<const NativeSceneMaterial>)>& intern) {
  if(!program.backend || geometry.backend()!=program.backend.get())
    throw std::runtime_error("native full-frame static geometry and material backends differ");
  if(!pass.targets.count) throw std::runtime_error("native full-frame static pass has no color target");
  // Decodable before any pipeline is created.
  DecodeNativeRenderState(base.After(program).render.words);
  NativeBackendPipelineDesc desc;
  desc.vertex_id=(uint64_t(program.inputs.vertex)<<1)|uint64_t(pass.targets.reverse_depth);
  desc.pixel_id=program.inputs.pixel;
  desc.input_layout=geometry.input_layout().elements(); desc.input_layout_id=geometry.input_layout().fingerprint();
  desc.render_targets=pass.targets.count; desc.rtv_format=pass.targets.rtv_format;
  desc.dsv_format=pass.targets.dsv_format; desc.sample_count=pass.targets.samples;
  auto resolved=program.Resolve(desc,pass.targets.reverse_depth,constants,base.render,base.samplers,pass.filtering);
  if(intern) resolved.capture.material=intern(std::move(resolved.capture.material));
  NativeFullFrameStaticMaterial result;
  size_t worlds=0;
  for(const auto& constant:resolved.capture.material->constants()) for(const auto& matrix:constant.matrices)
    if(matrix.source==NativeSceneMatrixSource::World) {
      if(constant.stage!=NativeBackendStage::Vertex) throw std::runtime_error("native full-frame static world is not a vertex matrix");
      ++worlds; result.world_column_major=matrix.column_major;
    }
  if(worlds!=1) throw std::runtime_error("native full-frame static material needs exactly one world matrix");
  result.material=std::move(resolved.capture.material);
  result.camera=resolved.capture.camera;
  result.render=resolved.render.words;
  return result;
}
}
