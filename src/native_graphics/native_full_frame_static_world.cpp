#include "native_full_frame_static_world.h"
#include <algorithm>
#include <map>

namespace edf::native {
namespace {
using SelectCache=NativeFullFrameStaticSelectCache;
// One member's published candidate, as the walk tests it.
SelectCache::Candidate NativeFullFrameStaticCandidate(uint32_t owner,const NativeSceneSources* sources) {
  SelectCache::Candidate result;
  result.owner=owner;
  result.view=sources?sources->FindCandidateView(owner):NativeSceneSources::CandidateView{};
  result.published=result.view.visibility && result.view.visibility->lod_count;
  if(result.published) result.visibility=*result.view.visibility;
  if(result.published) for(uint32_t lod=0;lod<result.drawable.size();++lod)
    if(const auto parts=result.view.Lod(lod))
      result.drawable[lod]=std::ranges::none_of(*parts,[](const auto& part) { return !part.group; });
  return result;
}
}
NativeFullFrameStaticSelection SelectNativeFullFrameStaticWorld(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route,NativeFullFrameStaticSelectCache* cache) {
  SelectCache local;
  auto& c=cache?*cache:local;
  NativeFullFrameStaticSelection result;
  auto& stats=result.stats;
  const auto* membership=publication.membership.get();
  const auto& view=camera.visibility;
  ++c.pass;
  // A list's candidates are views into c.sources: kept while this
  // publication's candidates answer the same.
  if(c.sources!=publication.sources &&
     !(c.sources && publication.sources && c.sources->SameCandidates(*publication.sources))) {
    c.lists.clear(); c.sources=publication.sources; ++c.stats.invalidations;
  }
  // The guest's +48 marker: an owner straddling several leaves is visited once
  // per frame, at its first list.
  c.seen.Begin();
  std::vector<uint32_t> lists;
  for(const auto& [owner,image]:publication.trees) {
    if(!image) continue;
    ++stats.worlds;
    const auto* order_slot=publication.group_order.Find(owner);
    const auto order=order_slot?*order_slot:nullptr;
    if(!order) ++stats.missing_orders;
    // Frame-local queues of this world, in selection (push) order, indexed by
    // the group's first position in the order.
    auto& queues=c.worlds[owner];
    queues.used=c.pass;
    for(const auto slot:queues.touched) queues.queues[slot].clear();  // Left by a frame that threw.
    queues.touched.clear(); queues.unordered.clear();
    if(queues.order!=order) {
      queues.order=order; queues.slots.clear(); queues.queues.clear();
      if(order) {
        queues.queues.resize(order->size());
        for(uint32_t i=0;i<order->size();++i) queues.slots.try_emplace((*order)[i],i);
      }
    }
    if(queues.regions!=image->regions) { NativeSceneTreeImageReader::Build(*image,queues.index); queues.regions=image->regions; }
    const NativeSceneTreeImageReader tree(*image,&queues.index);
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
      const std::shared_ptr<const NativeSceneMembership::Snapshot>* members=nullptr;
      if(membership) members=membership->lists.Find(list);
      if(!members || !*members) { ++stats.missing_lists; continue; }
      auto& entry=c.lists[list];
      entry.used=c.pass;
      if(entry.members!=*members) {
        entry.members=*members; entry.candidates.clear(); entry.candidates.reserve(entry.members->members.size());
        for(const auto& member:entry.members->members)
          entry.candidates.push_back(NativeFullFrameStaticCandidate(member.owner,c.sources.get()));
        ++c.stats.list_builds;
      } else ++c.stats.list_hits;
      for(const auto& candidate:entry.candidates) {
        ++stats.members;
        if(!c.seen.Insert(candidate.owner)) { ++stats.duplicates; continue; }
        if(!candidate.published) { ++stats.unpublished; continue; }
        const auto& object=candidate.visibility;
        const auto selection=SelectNativeVisibility(view,object,NativeVisibilityCenter(view,object));
        if(!selection.in_range) { ++stats.culled_distance; continue; }
        if(!selection.visible()) { ++stats.culled_frustum; continue; }
        // The route words, live, for culling's survivors only (every filter
        // here only drops objects, so the order changes no selection).
        ++stats.route_reads;
        const auto words=route(candidate.owner);
        if(!words) { ++stats.unrouted; continue; }
        if(ClassifyNativeStaticWalk(words->hidden,words->mode,words->direct)!=NativeStaticWalkRoute::Direct) {
          ++stats.not_direct; continue;
        }
        ++stats.visible;
        const auto parts=candidate.view.Lod(selection.lod);
        if(!parts) { ++stats.missing_lod; continue; }
        if(!candidate.drawable[selection.lod]) { ++stats.undrawable; continue; }
        for(const auto& part:*parts) {
          const auto slot=queues.slots.find(part.group);
          if(slot==queues.slots.end()) { ++queues.unordered[part.group]; continue; }
          auto& queue=queues.queues[slot->second];
          if(queue.empty()) queues.touched.push_back(slot->second);
          queue.push_back(part.instance);
        }
        stats.parts+=parts->size(); ++stats.selected;
        result.objects.push_back({candidate.owner,selection.lod});
      }
    }
    NativeFullFrameStaticOwner drawn{owner,{}};
    // Order positions ascending: the published order. Retail links each
    // selection at the group's head: last selected, first drawn.
    std::ranges::sort(queues.touched);
    drawn.groups.reserve(queues.touched.size());
    for(const auto slot:queues.touched) {
      auto& queue=queues.queues[slot];
      drawn.groups.push_back({(*order)[slot],std::vector<uint32_t>(queue.rbegin(),queue.rend())});
      queue.clear();
    }
    queues.touched.clear();
    for(const auto& [group,parts]:queues.unordered) { ++stats.unordered_groups; stats.unordered_parts+=parts; }
    queues.unordered.clear();
    if(!drawn.groups.empty()) result.owners.push_back(std::move(drawn));
  }
  // Lists and worlds no frame has visited for a while release what they hold.
  constexpr uint64_t kAge=256;
  if(!(c.pass%kAge)) {
    std::erase_if(c.lists,[&](const auto& item) { return c.pass-item.second.used>kAge; });
    std::erase_if(c.worlds,[&](const auto& item) { return c.pass-item.second.used>kAge; });
  }
  return result;
}
const NativeFullFrameStaticSelection& NativeFullFrameStaticWorld::Select(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route) {
  auto next=SelectNativeFullFrameStaticWorld(publication,camera,route,&selection_cache);
  // Whether BuildSelected would draw the same groups and instances as it drew last.
  const auto& last=frame_.selection.owners;
  same_selection_=built_.valid && std::ranges::equal(next.owners,last,[](const auto& a,const auto& b) {
    return a.owner==b.owner && std::ranges::equal(a.groups,b.groups,[](const auto& x,const auto& y) {
      return x.group==y.group && x.instances==y.instances;
    });
  });
  frame_.selection=std::move(next);
  return frame_.selection;
}
void ApplyNativeFullFrameStaticConstants(std::vector<NativeSceneMaterialInputs::Constant>& constants,
    const NativeFullFrameStaticCamera& camera) {
  for(auto& constant:constants) {
    if(camera.pass.Apply(constant) || !constant.global ||
       (constant.name!="m_WaterTime" && constant.name!="g_SignalBrightness")) continue;
    if(!camera.animation) throw std::runtime_error("native full-frame static material needs the world animation");
    camera.animation->Apply(constant);
  }
}
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameStaticConstants(const NativeSceneGroupMaterial& group,
    const NativeFullFrameStaticCamera& camera) {
  auto constants=group.constants;
  ApplyNativeFullFrameStaticConstants(constants,camera);
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
