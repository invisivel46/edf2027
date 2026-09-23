#include "native_full_frame_static_world.h"
#include <algorithm>
#include <cmath>
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
NativeFullFrameStaticCull::NativeFullFrameStaticCull(const NativeSceneVisibilityView& view) {
  const auto& m=view.matrix; const auto& f=view.frustum;
  // NativeVisibilityTransform: component c is m[c]*p0+m[4+c]*p1+m[8+c]*p2+m[12+c]*p3.
  for(size_t i=0;i<4;++i) { x_[i]=m[i*4]; y_[i]=m[i*4+1]; z_[i]=m[i*4+2]; }
  near_=f[24]; far_=f[25]; scale_=view.depth_scale;
  valid_=std::isfinite(near_) && std::isfinite(far_) && std::isfinite(scale_);
  for(size_t i=0;i<4;++i) valid_=valid_ && std::isfinite(x_[i]) && std::isfinite(y_[i]) && std::isfinite(z_[i]);
  for(size_t side=0;side<4;++side) {
    // NativeVisibilitySphere's side planes: fma(f[offset+axis],A,f[offset+2]*Z).
    const size_t offset=8+side*4,axis=side<2?0:1;
    a_[side]=f[offset+axis]; b_[side]=f[offset+2];
    valid_=valid_ && std::isfinite(a_[side]) && std::isfinite(b_[side]);
    const auto& along=axis?y_:x_;
    for(size_t i=0;i<4;++i) sides_[side][i]=a_[side]*along[i]+b_[side]*z_[i];
  }
}
bool NativeFullFrameStaticCull::Boundable(const NativeSceneVisibility& object) {
  for(size_t i=0;i<4;++i) if(!std::isfinite(object.box[i])) return false;
  return std::isfinite(object.radius) && object.radius>=0 && std::isfinite(object.distance);
}
bool NativeFullFrameStaticCull::Culled(const NativeFullFrameStaticBound& bound) const {
  if(!valid_) return false;
  // A linear form's interval over the box (in double: its roundings are
  // some 2^-50 of the magnitude, far below kSlack), and its magnitude
  // sum|n_i|*max|c_i|: the bound on |form| and on each term it rounds.
  const auto range=[&](const Form& n,double& lo,double& hi) {
    double magnitude=0; lo=hi=0;
    for(size_t i=0;i<4;++i) {
      const double p=n[i]*double(bound.lo[i]),q=n[i]*double(bound.hi[i]);
      lo+=(std::min)(p,q); hi+=(std::max)(p,q); magnitude+=(std::max)(std::abs(p),std::abs(q));
    }
    return magnitude;
  };
  const auto terms=[&](const Form& n) { double lo=0,hi=0; return range(n,lo,hi); };
  const double r=bound.radius;
  double zlo=0,zhi=0;
  const double zterms=range(z_,zlo,zhi);
  if(!(zterms<kLimit)) return false;
  const double ez=kSlack*zterms;
  // Out of every member's draw distance: depth=-fl(Z*s) > distance.
  if(std::abs(scale_)*zterms<kLimit) {
    const double nearest=(std::min)(-scale_*(zlo-ez),-scale_*(zhi+ez));
    if(nearest-kSlack*std::abs(scale_)*zterms>double(bound.distance)) return true;
  }
  // Nearer than the near plane or farther than the far one by every radius.
  if(zhi+ez<near_-r-kSlack*(std::abs(near_)+r)) return true;
  if(zlo-ez>far_+r+kSlack*(std::abs(far_)+r)) return true;
  // Beyond one side plane by every radius.
  const std::array<double,2> along{terms(x_),terms(y_)};
  for(size_t side=0;side<4;++side) {
    const double aterms=along[side/2];
    const double error=2*kSlack*(std::abs(a_[side])*aterms+std::abs(b_[side])*zterms);
    if(!(std::abs(a_[side])*aterms<kLimit && std::abs(b_[side])*zterms<kLimit)) continue;
    double lo=0,hi=0;
    range(sides_[side],lo,hi);
    if(lo-error>r) return true;
  }
  return false;
}
void NativeFullFrameStaticSelectCache::List::Index() {
  owners.clear(); clusters.clear(); clustered.clear(); loose.clear(); unpublished=0;
  owners.reserve(candidates.size());
  for(uint32_t i=0;i<candidates.size();++i) {
    const auto& candidate=candidates[i];
    owners.push_back(candidate.owner);
    if(!candidate.published) { ++unpublished; continue; }
    (NativeFullFrameStaticCull::Boundable(candidate.visibility)?clustered:loose).push_back(i);
  }
  std::ranges::sort(owners); owners.erase(std::unique(owners.begin(),owners.end()),owners.end());
  if(clustered.empty()) return;
  // Median splits on the widest of x, y, z and the draw distance, down to
  // clusters of kLeaf: nearby objects of one draw distance share a cluster.
  constexpr uint32_t kLeaf=8;
  clusters.reserve(clustered.size()/kLeaf*2+1);
  const auto build=[&](auto&& self,uint32_t begin,uint32_t end)->uint32_t {
    const auto index=uint32_t(clusters.size());
    clusters.push_back({});
    NativeFullFrameStaticBound bound;
    float closest=0;
    for(uint32_t at=begin;at<end;++at) {
      const auto& object=candidates[clustered[at]].visibility;
      if(at==begin) {
        for(size_t i=0;i<4;++i) bound.lo[i]=bound.hi[i]=object.box[i];
        bound.radius=object.radius; bound.distance=closest=object.distance;
        continue;
      }
      for(size_t i=0;i<4;++i) { bound.lo[i]=(std::min)(bound.lo[i],object.box[i]); bound.hi[i]=(std::max)(bound.hi[i],object.box[i]); }
      bound.radius=(std::max)(bound.radius,object.radius);
      bound.distance=(std::max)(bound.distance,object.distance); closest=(std::min)(closest,object.distance);
    }
    clusters[index].bound=bound; clusters[index].begin=begin; clusters[index].end=end;
    if(end-begin<=kLeaf) return index;
    const std::array<float,4> spans{bound.hi[0]-bound.lo[0],bound.hi[1]-bound.lo[1],bound.hi[2]-bound.lo[2],bound.distance-closest};
    const auto axis=size_t(std::ranges::max_element(spans)-spans.begin());
    const auto key=[&](uint32_t candidate) {
      const auto& object=candidates[candidate].visibility;
      return std::pair(axis<3?object.box[axis]:object.distance,candidate);
    };
    const auto middle=begin+(end-begin)/2;
    std::nth_element(clustered.begin()+begin,clustered.begin()+middle,clustered.begin()+end,
      [&](uint32_t a,uint32_t b) { return key(a)<key(b); });
    const auto left=self(self,begin,middle),right=self(self,middle,end);
    clusters[index].left=left; clusters[index].right=right;
    return index;
  };
  build(build,0,uint32_t(clustered.size()));
}
NativeFullFrameStaticSelection SelectNativeFullFrameStaticWorld(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route,NativeFullFrameStaticSelectCache* cache) {
  SelectCache local;
  auto& c=cache?*cache:local;
  // Reuse off (native_reuse.h): the image walk instead of the flattened tree,
  // and every published member tested one by one instead of the cluster cull.
  const bool reuse=NativeReuseAllowed();
  NativeFullFrameStaticSelection result;
  auto& stats=result.stats;
  const auto* membership=publication.membership.get();
  const auto& view=camera.visibility;
  const NativeFullFrameStaticCull cull(view);
  result.objects.reserve(c.objects);
  ++c.pass;
  // A list's candidates are views into c.sources. With the same candidates
  // the new generation shares every owner entry's parts and record, so the
  // views stay valid under it and it is retained instead (the next
  // comparison then spans one step). Otherwise only the members whose owner
  // entry moved are looked up again, in every cached list that holds one.
  if(c.sources!=publication.sources) {
    if(!c.sources || !publication.sources) {
      c.lists.clear(); ++c.stats.invalidations;
    } else if(!c.sources->SameCandidates(*publication.sources)) {
      auto& moved=c.moved;
      moved.clear();
      publication.sources->OwnerDifferences(*c.sources,[&](uint32_t owner) { moved.push_back(owner); });
      std::ranges::sort(moved); moved.erase(std::unique(moved.begin(),moved.end()),moved.end());
      for(auto& [address,list]:c.lists) {
        const bool hit=moved.size()<list.owners.size()?
          std::ranges::any_of(moved,[&](uint32_t owner) { return std::ranges::binary_search(list.owners,owner); }):
          std::ranges::any_of(list.owners,[&](uint32_t owner) { return std::ranges::binary_search(moved,owner); });
        if(!hit) continue;
        for(auto& candidate:list.candidates) if(std::ranges::binary_search(moved,candidate.owner)) {
          candidate=NativeFullFrameStaticCandidate(candidate.owner,publication.sources.get()); ++c.stats.patched;
        }
        list.Index(); ++c.stats.patches;
      }
    }
    c.sources=publication.sources;
  }
  // The guest's +48 marker: an owner straddling several leaves is visited once
  // per frame, at its first list.
  c.seen.Begin();
  auto& lists=c.gathered;
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
        queues.slots.Build(*order);
      }
    }
    if(queues.regions!=image->regions) {
      queues.regions=image->regions; queues.index.clear(); ++c.stats.tree_builds;
      if(!queues.tree.Build(NativeSceneTreeImageReader(*image),owner)) NativeSceneTreeImageReader::Build(*image,queues.index);
    }
    lists.clear();
    if(queues.tree.valid && reuse) {
      // The walk of WalkNativeSceneTree over the flattened image.
      const auto& tree=queues.tree;
      ++stats.flat_walks;
      const auto walk=[&](auto&& self,int32_t index,bool accepted)->void {
        ++stats.tree_reads;
        if(index<0) return;
        const auto& node=tree.nodes[size_t(index)];
        if(!accepted) {
          ++stats.nodes_classified;
          const auto classified=NativeFullFrameStaticTree::Classify(node,view);
          if(!classified) return;
          accepted=classified==1;
        }
        if(node.leaf) { lists.push_back(node.list); return; }
        for(const auto child:node.children) self(self,child,accepted);
      };
      for(const auto root:tree.roots) walk(walk,root,false);
    } else {
      // The flat index exists only for an image that could not be flattened.
      const NativeSceneTreeImageReader tree(*image,queues.index.empty()?nullptr:&queues.index);
      WalkNativeSceneTree(tree,owner,[&](uint32_t node) {
        ++stats.nodes_classified;
        return ClassifyNativeSceneTreeNode(tree,node,view).result;
      },[&](uint32_t list) { lists.push_back(list); });
      stats.tree_reads+=tree.reads;
    }
    // 820B4310 gathers world+372 (non-octree map objects) after the octree.
    lists.push_back(uint32_t(owner+372));
    for(const auto address:lists) {
      ++stats.lists;
      const std::shared_ptr<const NativeSceneMembership::Snapshot>* members=nullptr;
      if(membership) members=membership->lists.Find(address);
      if(!members || !*members) { ++stats.missing_lists; continue; }
      auto& entry=c.lists[address];
      entry.used=c.pass;
      if(entry.members!=*members) {
        entry.members=*members; entry.candidates.clear(); entry.candidates.reserve(entry.members->members.size());
        for(const auto& member:entry.members->members)
          entry.candidates.push_back(NativeFullFrameStaticCandidate(member.owner,c.sources.get()));
        entry.Index();
        ++c.stats.list_builds;
      } else ++c.stats.list_hits;
      stats.members+=entry.candidates.size(); stats.unpublished+=entry.unpublished;
      if(c.census && entry.unpublished)
        for(const auto& candidate:entry.candidates)
          if(!candidate.published) result.skipped.push_back({candidate.owner,NativeFullFrameStaticSelection::Skip::Unpublished});
      // The members culling may keep, in list order: every published member
      // outside the clusters the conservative test drops whole.
      auto& pending=c.pending;
      pending.clear();
      if(!entry.clusters.empty()) {
        if(cull.valid() && reuse) {
          auto& stack=c.stack;
          stack.assign(1,0u);
          while(!stack.empty()) {
            const auto& cluster=entry.clusters[stack.back()];
            stack.pop_back();
            ++stats.clusters;
            if(cull.Culled(cluster.bound)) { stats.culled_bulk+=cluster.end-cluster.begin; continue; }
            if(cluster.left) { stack.push_back(cluster.right); stack.push_back(cluster.left); continue; }
            pending.insert(pending.end(),entry.clustered.begin()+cluster.begin,entry.clustered.begin()+cluster.end);
          }
        } else pending=entry.clustered;
      }
      pending.insert(pending.end(),entry.loose.begin(),entry.loose.end());
      std::ranges::sort(pending);
      for(const auto at:pending) {
        const auto& candidate=entry.candidates[at];
        const auto& object=candidate.visibility;
        const auto selection=SelectNativeVisibility(view,object,NativeVisibilityCenter(view,object));
        if(!selection.in_range) { ++stats.culled_distance; continue; }
        if(!selection.visible()) { ++stats.culled_frustum; continue; }
        if(!c.seen.Insert(candidate.owner)) { ++stats.duplicates; continue; }
        // The route words, live, for culling's survivors only (every filter
        // here only drops objects, so the order changes no selection).
        ++stats.route_reads;
        const auto words=route(candidate.owner);
        using Skip=NativeFullFrameStaticSelection::Skip;
        const auto skip=[&](Skip reason) { if(c.census) result.skipped.push_back({candidate.owner,reason}); };
        if(!words) { ++stats.unrouted; skip(Skip::Unrouted); continue; }
        if(const auto walk=ClassifyNativeStaticWalk(words->hidden,words->mode,words->direct || words->fixed);walk!=NativeStaticWalkRoute::Direct) {
          ++stats.not_direct;
          // Hidden: 821C0C00 returns before any slot 4, as here.
          if(walk!=NativeStaticWalkRoute::Hidden)
            skip(walk==NativeStaticWalkRoute::Virtual?Skip::Virtual:walk==NativeStaticWalkRoute::Bucket?Skip::Bucket:Skip::UnknownMode);
          continue;
        }
        // 820B2670 picks the LOD record; 820BAF90 publishes its one +396 record,
        // which its source files as LOD 0 whatever the depth.
        if(words->fixed!=candidate.view.fixed) { ++stats.route_mismatch; skip(Skip::RouteMismatch); continue; }
        ++stats.visible;
        const auto lod=words->fixed?0u:selection.lod;
        const auto parts=candidate.view.Lod(lod);
        if(!parts) { ++stats.missing_lod; skip(Skip::MissingLod); continue; }
        if(!candidate.drawable[lod]) { ++stats.undrawable; skip(Skip::Undrawable); continue; }
        for(const auto& part:*parts) {
          const auto slot=queues.slots.Find(part.group);
          if(slot==SelectCache::Slots::kNone) { ++queues.unordered[part.group]; continue; }
          auto& queue=queues.queues[slot];
          if(queue.empty()) queues.touched.push_back(slot);
          queue.push_back(part.instance);
        }
        stats.parts+=parts->size(); ++stats.selected; stats.fixed+=words->fixed;
        result.objects.push_back({candidate.owner,lod});
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
  c.objects=result.objects.size();
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
  // Reuse off: selected with a frame-local cache, selection_cache left as it was.
  auto next=SelectNativeFullFrameStaticWorld(publication,camera,route,NativeReuseAllowed()?&selection_cache:nullptr);
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
