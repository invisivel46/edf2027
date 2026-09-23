#pragma once
#include "native_full_frame_base_state.h"
#include "native_scene_adapter.h"
#include "native_scene_tree.h"
#include "native_scene_static_walk.h"
#include "native_static_world_cache.h"
#include "native_queued_scene.h"
#include "native_reuse.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace edf::native {
// The full-frame renderer's StaticWorld pass: the static opaque world's draw
// list as a function of published data and the pass camera alone. It replaces
// the guest render helper's path for these objects end to end:
//   821C61D8 octree walk      -> WalkNativeSceneTree over the published tree image
//   820B4038 per-list gather  -> SelectNativeVisibility on published visibility records
//   native_scene_queues       -> a frame-local selection (group -> instances)
//   821C3BB8 group walk       -> the published group_order
//   821D96D8 group draw       -> one resolved material per group, instanced draws
// There are no guest queues, device mirrors, eligibility against the device or
// handoffs. Every input is a published immutable generation - NativeScenePublication
// (sources, membership, trees, group order, geometry, material programs) - but
// the route words, read live for the objects culling keeps (NativeFullFrameLiveRoutes).

// The camera one full frame renders the static world with.
//  visibility: camera+96 view matrix, camera+288 frustum and context+8 depth
//              scale, the values 821C61D8/820B4038 read (ReadNativeSceneVisibilityView).
//  pass:       the published camera (scene+32/+96/+160) the materials' camera
//              globals are replaced with (NativeScenePassCamera::Apply).
//  animation:  the published world animation constants, when a material uses them.
struct NativeFullFrameStaticCamera {
  NativeSceneVisibilityView visibility;
  NativeScenePassCamera pass;
  std::optional<NativeScenePassAnimation> animation;
};
// sub_821C0C00's route words for one owner: vtable+16==820B2670 (direct), +52
// mode and +64 hidden. They have unhooked writers (inline stw/sth), so they
// are not published: the selection reads them live at render time, for the
// objects that survive tree and frustum culling only, as 820B4038 reads them
// in the guest's own render walk (the same race with the simulation).
// fixed: vtable+16==820BAF90 (clRock, NativeSceneFixedRecord), the other slot 4
// that only publishes a group: `addi r3,r3,396; b 821BEE68`, one record and no
// LOD choice. direct and fixed are exclusive; either routes mode 0 as Direct.
struct NativeFullFrameStaticRoute {
  bool direct=false;
  uint32_t mode=0;
  uint16_t hidden=0;
  bool fixed=false;
  bool operator==(const NativeFullFrameStaticRoute&) const=default;
};
// The slot-4 kinds the static world draws, by vtable+16.
enum class NativeFullFrameStaticSlot : uint8_t { Other, Lod, Fixed };
constexpr NativeFullFrameStaticSlot ClassifyNativeFullFrameStaticSlot(uint32_t render) {
  return render==kNativeStaticDirectRender?NativeFullFrameStaticSlot::Lod:
    render==NativeSceneFixedRecord::render?NativeFullFrameStaticSlot::Fixed:NativeFullFrameStaticSlot::Other;
}
// Reads one owner's route words; nullopt when its header cannot be read.
using NativeFullFrameStaticRouteRead=std::function<std::optional<NativeFullFrameStaticRoute>(uint32_t owner)>;
// The production route reader over a reader (typically a page window): one
// 68-byte header read per object (+0 vtable, +52 mode, +64 hidden), and the
// vtable+16 slot only when the object would take the render slot (hidden 0,
// mode 0), in sub_821C0C00's order. Vtables are image data, never stored to,
// so each vtable's slot answer is kept for the reader's lifetime. The reader
// must outlive this. Not synchronized.
template<class Reader>
class NativeFullFrameLiveRoutes {
 public:
  explicit NativeFullFrameLiveRoutes(const Reader& reader):reader_(reader) {}
  std::optional<NativeFullFrameStaticRoute> operator()(uint32_t owner) const {
    try {
      const auto* header=reader_.Bytes(owner,68);
      const auto vtable=GuestBlockWord(header);
      NativeFullFrameStaticRoute route{false,GuestBlockWord(header+52),uint16_t(GuestBlockWord(header+64)>>16)};
      ++reads;
      if(route.hidden || route.mode) return route;
      // Static objects share a handful of vtables: the last one answers most.
      if(!last_ || vtable!=last_vtable_) {
        auto found=slots_.find(vtable);
        if(found==slots_.end()) {
          found=slots_.emplace(vtable,ClassifyNativeFullFrameStaticSlot(reader_.Word(reader_.Add(vtable,16)))).first;
          ++slot_reads;
        }
        last_=true; last_vtable_=vtable; last_slot_=found->second;
      }
      route.direct=last_slot_==NativeFullFrameStaticSlot::Lod;
      route.fixed=last_slot_==NativeFullFrameStaticSlot::Fixed;
      return route;
    } catch(const std::exception&) { ++failures; return std::nullopt; }
  }
  mutable uint64_t reads=0,slot_reads=0,failures=0;
 private:
  const Reader& reader_;
  mutable std::unordered_map<uint32_t,NativeFullFrameStaticSlot> slots_;
  mutable uint32_t last_vtable_=0;
  mutable bool last_=false;
  mutable NativeFullFrameStaticSlot last_slot_{};
};
// Addresses of the published tree image only: every read is a captured byte,
// and anything else throws rather than reaching guest memory.
class NativeSceneTreeImageReader {
 public:
  // image.Locate's answer for every region, in address order: what index
  // holds for an image's regions (Build).
  using Index=std::vector<NativeSceneTreeImage::Region>;
  // index, when given, is Index(image) and must outlive this.
  explicit NativeSceneTreeImageReader(const NativeSceneTreeImage& image,const Index* index=nullptr)
    :image_(image),index_(index) {}
  uint32_t Add(uint32_t address,size_t size) const { return uint32_t(address+size); }
  // image.Find(address,size): the regions last located answer every address
  // in their [base,limit) exactly as Locate would. A node's reads alternate
  // between two regions (center, radius and extents; children and
  // occupancy), so two are kept; a read outside both searches the index (a
  // flat array) or the map.
  const uint8_t* Bytes(uint32_t address,size_t size) const {
    ++reads;
    if(!Inside(last_[0],address)) {
      std::swap(last_[0],last_[1]);
      if(!Inside(last_[0],address)) last_[0]=Locate(address);
    }
    if(const auto* bytes=NativeSceneTreeImage::Within(last_[0],address,size)) return bytes;
    throw std::runtime_error("native full-frame tree read outside its published image");
  }
  uint32_t Word(uint32_t address) const { return GuestBlockWord(Bytes(address,4)); }
  mutable uint64_t reads=0;
  // Index(image): each region with the next one's start as its limit.
  static void Build(const NativeSceneTreeImage& image,Index& index) {
    index.clear();
    if(!image.regions) return;
    index.reserve(image.regions->size());
    for(auto at=image.regions->begin();at!=image.regions->end();++at) {
      const auto next=std::next(at);
      index.push_back({at->first,next==image.regions->end()?(uint64_t(1)<<32):next->first,&at->second});
    }
  }
 private:
  static bool Inside(const NativeSceneTreeImage::Region& region,uint32_t address) {
    return region.bytes && address>=region.base && address<region.limit;
  }
  NativeSceneTreeImage::Region Locate(uint32_t address) const {
    if(!index_) return image_.Locate(address);
    // The last region starting at or below address, as Locate's upper_bound.
    const auto next=std::ranges::upper_bound(*index_,address,{},&NativeSceneTreeImage::Region::base);
    return next==index_->begin()?NativeSceneTreeImage::Region{}:*std::prev(next);
  }
  const NativeSceneTreeImage& image_;
  const Index* index_=nullptr;
  mutable std::array<NativeSceneTreeImage::Region,2> last_{};
};

// One world owner's frame selection, in the order 821C3BB8 draws it: groups in
// the published group order, each group's instances in queue order (the guest
// links each selection at the group's head, so the last selected draws first).
struct NativeFullFrameStaticGroup {
  uint32_t group=0;
  std::vector<uint32_t> instances;
};
struct NativeFullFrameStaticOwner {
  uint32_t owner=0;
  std::vector<NativeFullFrameStaticGroup> groups;
};
struct NativeFullFrameStaticSelection {
  struct Object { uint32_t owner=0,lod=0; bool operator==(const Object&) const=default; };
  // members counts every member of every gathered list; culled_bulk the ones
  // a conservative cluster test proved culled without testing them one by
  // one (NativeFullFrameStaticCull), culled_distance/culled_frustum the ones
  // the exact per-object test culled, duplicates the visible ones an earlier
  // list already visited (the only ones the +48 marker can change: see
  // SelectNativeFullFrameStaticWorld). tree_reads: node records the walk
  // visited (occupied or not). clusters: cluster tests made.
  struct Stats {
    uint64_t worlds=0,missing_orders=0,nodes_classified=0,tree_reads=0,lists=0,missing_lists=0,members=0,duplicates=0;
    uint64_t route_reads=0,unrouted=0,not_direct=0,unpublished=0,culled_distance=0,culled_frustum=0,visible=0,missing_lod=0,undrawable=0;
    uint64_t selected=0,parts=0,unordered_groups=0,unordered_parts=0;
    uint64_t fixed=0,route_mismatch=0;  // Fixed-record objects selected; route kind unlike the source's.
    uint64_t culled_bulk=0,clusters=0,flat_walks=0;
  };
  std::vector<NativeFullFrameStaticOwner> owners;
  // Every object selected natively, in walk order, with the LOD it chose.
  std::vector<Object> objects;
  Stats stats;
};
// A conservative "every one of these objects is culled" test, exact against
// SelectNativeVisibility's float arithmetic: Culled(bound) is true only when
// every object whose record lies inside bound is one SelectNativeVisibility
// culls (not visible()) under the view the test was made for. It decides
// nothing else - visibility, LOD and everything a survivor needs come from
// SelectNativeVisibility itself - so it can only spare the per-object test of
// objects that test would cull.
//
// SelectNativeVisibility culls an object when depth=-fl(Z*s) > distance (s
// the depth scale), or when NativeVisibilitySphere returns 0, which it does
// as soon as one of Z < fl(f24-r), Z > fl(f25+r) or D_k > r holds, D_k =
// fma(a_k,A_k,fl(b_k*Z)) the side-plane value (k<4; A_k is X for the first
// two sides, Y for the others; with r>=0 each implies the enclosing compare
// the test makes first). X, Y, Z are the center NativeVisibilityTransform
// computes from box[0..3]: four float roundings of a linear form of the
// center, so |X-X*| <= 4*2^-24*sum|m_i*c_i|; a plane value adds two roundings
// and the depth one. The bound interval-evaluates each linear form over the
// centers' box (exactly, in double) and widens it by kSlack (1e-5, forty
// times the worst rounding) of the magnitudes involved; a test whose
// magnitudes could overflow a float is skipped. A bound holds its members'
// centers (all four components) as a box, their largest radius and their
// largest distance; a member with a non-finite center, radius or distance, or
// a negative radius, is never bounded (Boundable). A view with a non-finite
// value the tests read makes every test false.
struct NativeFullFrameStaticBound {
  std::array<float,4> lo{},hi{};
  float radius=0,distance=0;
};
class NativeFullFrameStaticCull {
 public:
  explicit NativeFullFrameStaticCull(const NativeSceneVisibilityView& view);
  bool valid() const { return valid_; }
  bool Culled(const NativeFullFrameStaticBound& bound) const;
  static bool Boundable(const NativeSceneVisibility& object);
  static constexpr double kSlack=1e-5,kLimit=1e30;
 private:
  using Form=std::array<double,4>;
  // The view-space x, y and z of a center, and each side plane's a*A+b*Z.
  Form x_{},y_{},z_{};
  std::array<Form,4> sides_{};
  std::array<double,4> a_{},b_{};
  double near_=0,far_=0,scale_=0;
  bool valid_=false;
};
// A tree image flattened for the walk: per occupied node the words
// WalkNativeSceneTree and ClassifyNativeSceneTreeNode read (center +32,
// half extents +48, radius +64, whether +84 holds a first child, and the
// children), decoded once per image. Build reads what the walk reads, in its
// order, but for every node (the walk reads a node's bounds only when it
// classifies it): any read or check that fails leaves it invalid, and the
// selection then walks the image itself, which fails, or not, as before. An
// image is immutable, so the walk's iterator re-checks of the root extent
// always pass and are not kept. Classify returns ClassifyNativeSceneTreeNode's
// result from the same floats and the same arithmetic.
struct NativeFullFrameStaticTree {
  struct Node {
    std::array<float,4> center{},extent{};
    float radius=0;
    uint32_t list=0;  // node+120, for a leaf.
    bool leaf=false;
    // Each child's node, or -1 when unoccupied (the walk returns at its +116).
    std::array<int32_t,8> children{};
  };
  std::vector<Node> nodes;
  std::vector<int32_t> roots;  // In root order; -1 when unoccupied.
  bool valid=false;
  template<class Reader> bool Build(const Reader& reader,uint32_t manager) {
    nodes.clear(); roots.clear(); valid=false;
    try {
      const auto levels=reader.Word(reader.Add(manager,52));
      const auto level_end=reader.Word(reader.Add(manager,56));
      if(!levels || level_end<levels || level_end-levels<32) return false;
      const auto extent=reader.Add(levels,16);
      auto root=reader.Word(reader.Add(extent,4));
      const auto end=reader.Word(reader.Add(extent,8));
      if(root>end || (end-root)%144) return false;
      const auto flatten=[&](auto&& self,uint32_t node,uint32_t depth)->int32_t {
        if(depth>128 || nodes.size()>=(1u<<20)) throw std::runtime_error("native full-frame tree exceeds its limits");
        if(!reader.Word(reader.Add(node,116))) return -1;
        Node value;
        value.center=ReadNativeVisibilityFloats<4>(reader,reader.Add(node,32));
        value.radius=std::bit_cast<float>(reader.Word(reader.Add(node,64)));
        value.extent=ReadNativeVisibilityFloats<4>(reader,reader.Add(node,48));
        const auto children=reader.Add(node,84);
        value.leaf=!reader.Word(children);
        value.list=reader.Add(node,120);
        value.children.fill(-1);
        const auto index=int32_t(nodes.size());
        nodes.push_back(value);
        if(!value.leaf) for(uint32_t child=0;child<8;++child)
          nodes[index].children[child]=self(self,reader.Word(reader.Add(children,child*4)),depth+1);
        return index;
      };
      for(;root!=end;root=reader.Add(root,144)) roots.push_back(flatten(flatten,root,0));
      valid=true;
    } catch(const std::exception&) { nodes.clear(); roots.clear(); }
    return valid;
  }
  static uint32_t Classify(const Node& node,const NativeSceneVisibilityView& view) {
    const auto sphere=NativeVisibilitySphere(view,NativeVisibilityTransform(node.center,view.matrix),node.radius);
    return sphere==2?NativeVisibilityAabb(view,node.center,node.extent):sphere;
  }
};
// Cross-frame scratch of SelectNativeFullFrameStaticWorld; it never changes
// a selection, only what one costs:
//  - each world's tree image flattened (NativeFullFrameStaticTree), rebuilt
//    when the image's regions move (a restamp shares them); the walk
//    classifies from it with no image lookups;
//  - each membership list's members pre-looked-up (visibility record, LOD
//    parts), in list order, and a hierarchy of conservative cluster bounds
//    over its boundable published members (NativeFullFrameStaticCull): a
//    frame tests the clusters coarse to fine, drops each one the test proves
//    culled whole, and runs SelectNativeVisibility only on the members of
//    the others (and the unbounded ones), in list order. Clusters split on the
//    widest of x, y, z and the draw distance, so objects of one draw distance
//    share clusters and a cluster past it is culled whole. A list is rebuilt
//    when its published snapshot moves. The views point into sources, which
//    it retains, so they stay valid while the publication moves on; when the
//    candidates move (NativeSceneSources::SameCandidates) only the members
//    whose owner entry moved (NativeSceneSources::OwnerDifferences, over the
//    chunks written between the two generations) are looked up again and
//    their lists' clusters rebuilt - an unmoved owner's entry holds the same
//    parts and record objects in both generations, so its views stay valid
//    under the new one. Route words are never cached: they are read live,
//    each frame, for culling's survivors;
//  - the +48 visit marker as an epoch-stamped open-addressing set (no clear,
//    no allocation per frame);
//  - each world's group queues as flat arrays indexed by the group's first
//    position in the published order (rebuilt when the order moves).
// Not synchronized.
struct NativeFullFrameStaticSelectCache {
  struct Candidate {
    uint32_t owner=0;
    bool published=false;  // A visibility record with at least one LOD.
    std::array<bool,3> drawable{};  // Every part of the LOD has a group.
    // The record itself when published: the survivors a frame tests are read
    // from one array, not through a pointer per object.
    NativeSceneVisibility visibility;
    NativeSceneSources::CandidateView view;
  };
  // One cluster: its bound and its members' positions in List::clustered,
  // [begin,end); children (both or neither) are later clusters.
  struct Cluster {
    NativeFullFrameStaticBound bound;
    uint32_t begin=0,end=0,left=0,right=0;
  };
  struct List {
    std::shared_ptr<const NativeSceneMembership::Snapshot> members;
    std::vector<Candidate> candidates;
    // The members' owners, sorted and unique, for OwnerDifferences' patches.
    std::vector<uint32_t> owners;
    // The cluster hierarchy (clusters[0] the root, when any) over the
    // boundable published candidates (their indices, grouped by cluster), and
    // the published ones outside it, ascending.
    std::vector<Cluster> clusters;
    std::vector<uint32_t> clustered,loose;
    uint32_t unpublished=0;
    uint64_t used=0;
    // Recomputes owners, clusters, clustered, loose and unpublished from candidates.
    void Index();
  };
  // Group -> its first position in the published order, as an open-addressed
  // table (the lookup every selected part makes); kNone when absent.
  class Slots {
   public:
    static constexpr uint32_t kNone=UINT32_MAX;
    template<class Order> void Build(const Order& order) {
      size_t size=16;
      while(size<order.size()*2) size*=2;
      keys_.assign(size,0); values_.assign(size,kNone); zero_=kNone;
      for(uint32_t i=0;i<order.size();++i) {
        const uint32_t group=order[i];
        if(!group) { if(zero_==kNone) zero_=i; continue; }
        for(size_t at=Hash(group)&(size-1);;at=(at+1)&(size-1)) {
          if(!keys_[at]) { keys_[at]=group; values_[at]=i; break; }
          if(keys_[at]==group) break;
        }
      }
    }
    void clear() { keys_.clear(); values_.clear(); zero_=kNone; }
    uint32_t Find(uint32_t group) const {
      if(!group) return zero_;
      if(keys_.empty()) return kNone;
      const auto mask=keys_.size()-1;
      for(size_t at=Hash(group)&mask;;at=(at+1)&mask) {
        if(keys_[at]==group) return values_[at];
        if(!keys_[at]) return kNone;
      }
    }
   private:
    static size_t Hash(uint32_t key) { return size_t((uint64_t(key)*0x9E3779B97F4A7C15ull)>>32); }
    std::vector<uint32_t> keys_,values_;
    uint32_t zero_=kNone;
  };
  struct Queues {
    std::shared_ptr<const NativeSceneGroupOrder> order;
    Slots slots;                                  // Group -> first position in order.
    std::vector<std::vector<uint32_t>> queues;    // Per position, in push order.
    std::vector<uint32_t> touched;                // Positions with a queue this frame.
    std::map<uint32_t,size_t> unordered;          // Groups outside the order -> parts.
    // The tree image's regions, the flattened tree built from them and, when
    // it could not be flattened, their flat index for the image walk
    // (NativeSceneTreeImageReader).
    std::shared_ptr<const NativeSceneTreeImage::Regions> regions;
    NativeFullFrameStaticTree tree;
    NativeSceneTreeImageReader::Index index;
    uint64_t used=0;
  };
  // The +48 marker: Insert is true once per key per Begin.
  class Seen {
   public:
    void Begin() {
      if(++epoch_==0) { std::fill(stamps_.begin(),stamps_.end(),0u); epoch_=1; }
      count_=0;
    }
    bool Insert(uint32_t key) {
      if((count_+1)*2>keys_.size()) Grow();
      const auto mask=keys_.size()-1;
      for(size_t at=Hash(key)&mask;;at=(at+1)&mask) {
        if(stamps_[at]!=epoch_) { stamps_[at]=epoch_; keys_[at]=key; ++count_; return true; }
        if(keys_[at]==key) return false;
      }
    }
   private:
    static size_t Hash(uint32_t key) { return size_t((uint64_t(key)*0x9E3779B97F4A7C15ull)>>32); }
    void Grow() {
      std::vector<uint32_t> keys((std::max)(keys_.size()*2,size_t(1024))),stamps(keys.size());
      const auto mask=keys.size()-1;
      for(size_t i=0;i<keys_.size();++i) if(stamps_[i]==epoch_) {
        size_t at=Hash(keys_[i])&mask;
        while(stamps[at]==epoch_) at=(at+1)&mask;
        stamps[at]=epoch_; keys[at]=keys_[i];
      }
      keys_=std::move(keys); stamps_=std::move(stamps);
    }
    std::vector<uint32_t> keys_,stamps_;
    uint32_t epoch_=0;
    size_t count_=0;
  };
  // invalidations: the lists dropped whole (no sources to compare with);
  // patches/patched: lists and members looked up again after moved candidates.
  struct Stats { uint64_t list_hits=0,list_builds=0,invalidations=0,patches=0,patched=0,tree_builds=0; };
  std::shared_ptr<const NativeSceneSources> sources;  // The generation every List points into.
  std::unordered_map<uint32_t,List> lists;
  std::unordered_map<uint32_t,Queues> worlds;
  Seen seen;
  Stats stats;
  uint64_t pass=0;
  // Per-frame scratch: gathered lists, a list's members to test, the cluster
  // stack and the moved owners.
  std::vector<uint32_t> gathered,pending,stack,moved;
  size_t objects=0;  // The last selection's object count, reserved up front.
};
// 821C61D8 + 820B4038 + 821C3BB8's group order over published data. Per world
// owner with a tree image: walk the tree with frustum classification from the
// camera; gather each accepted leaf list (node+120), then world+372, from the
// published membership; visit each owner once (the guest's +48 frame marker);
// cull by the published visibility record and pick the LOD
// (SelectNativeVisibility, shared with the 820B4038 hook); route by the words
// route() reads for the survivors only (hidden, then mode 0 with direct
// 820B2670 or fixed 820BAF90 dispatch; buckets, virtual and unknown routes are
// other passes' objects; an unreadable header is unrouted); push each part of
// that LOD into its group - for a fixed route the owner's one record (its
// source's LOD 0; a source of the other kind is a route mismatch, dropped). A part without a group makes the whole object
// undrawable here, as it sends the object back to the guest in the hook.
// Selections of a group outside the owner's published order are counted and
// dropped: 821C3BB8 never reaches them.
// The marker is applied to the objects culling keeps only: whether an object
// is culled, and its LOD, depend on its record and the camera alone, so an
// object culled at its first list is culled at every later one, and the
// marker only ever stops a visible object's second selection - which it
// still does. Culling first lets whole clusters be dropped unvisited.
// cache, when given, is kept across frames (a call without one builds and
// drops its own); the selection is the same either way.
NativeFullFrameStaticSelection SelectNativeFullFrameStaticWorld(const NativeScenePublication& publication,
  const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route,
  NativeFullFrameStaticSelectCache* cache=nullptr);

// Target formats and depth direction of the pass, and its base state: the one
// definition every full-frame scene pass shares (native_full_frame_base_state.h).
using NativeFullFrameStaticTargets=NativeFullFramePassTargets;
struct NativeFullFrameStaticPass {
  NativeFullFrameStaticTargets targets;
  NativeBackendViewport viewport;
  NativeBackendScissor scissor;
  int filtering=-1;
  bool operator==(const NativeFullFrameStaticPass& other) const {
    return targets==other.targets && !std::memcmp(&viewport,&other.viewport,sizeof(viewport)) &&
      !std::memcmp(&scissor,&other.scissor,sizeof(scissor)) && filtering==other.filtering;
  }
};
// Each group's state is the shared base state plus its own material's state
// operations - NOT chained from the previous group; see NativeFullFrameBaseState.
inline NativeSceneMaterialPassState NativeFullFrameStaticBaseState(const NativeFullFrameStaticTargets& targets) {
  return NativeFullFrameBaseState(targets);
}
inline constexpr const auto& kNativeFullFrameStaticBaseOperations=kNativeFullFrameBaseOperations;

// One group's material under the pass base state: everything its instances
// share. camera holds only the matrices (NativeStaticCaptureCamera's).
struct NativeFullFrameStaticMaterial {
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneView camera;
  bool world_column_major=false;
  RenderStateWords render{};
};
// NativeSceneMaterialProgram::Resolve with the pass base state and targets,
// interned when intern is given, validated to bind exactly one vertex
// g_mWorld. Throws on anything the pass cannot draw.
NativeFullFrameStaticMaterial ResolveNativeFullFrameStaticMaterial(const NativeSceneMaterialProgram& program,
  const NativeIndexedMesh::RetainedDraw& geometry,const NativeFullFrameStaticPass& pass,
  const NativeSceneMaterialPassState& base,std::span<const NativeSceneMaterialInputs::Constant> constants,
  const std::function<std::shared_ptr<const NativeSceneMaterial>(std::shared_ptr<const NativeSceneMaterial>)>& intern={});
// The production resolver for NativeFullFrameStaticWorld::Build; pass must
// outlive it. intern is typically the adapter's InternMaterial.
inline auto NativeFullFrameStaticResolver(const NativeFullFrameStaticPass& pass,
    std::function<std::shared_ptr<const NativeSceneMaterial>(std::shared_ptr<const NativeSceneMaterial>)> intern={}) {
  return [&pass,intern=std::move(intern)](const NativeSceneGroupMaterial& group,
      const std::shared_ptr<const NativeIndexedMesh::RetainedDraw>& geometry,const NativeSceneMaterialPassState& base,
      std::span<const NativeSceneMaterialInputs::Constant> constants) {
    if(!group.program || !geometry) throw std::runtime_error("native full-frame static group has no program or geometry");
    return ResolveNativeFullFrameStaticMaterial(*group.program,*geometry,pass,base,constants,intern);
  };
}
// The group's published constants with the pass camera and animation applied
// (the pass-owned globals); throws when a material needs a missing animation.
std::vector<NativeSceneMaterialInputs::Constant> NativeFullFrameStaticConstants(const NativeSceneGroupMaterial& group,
  const NativeFullFrameStaticCamera& camera);
// The same in place, on a copy of the group's published constants.
void ApplyNativeFullFrameStaticConstants(std::vector<NativeSceneMaterialInputs::Constant>& constants,
  const NativeFullFrameStaticCamera& camera);
// Whether NativeScenePassCamera::Apply rewrites this constant: the only
// constants whose pass value depends on the pass camera.
inline bool NativeFullFrameStaticCameraConstant(const NativeSceneMaterialInputs::Constant& constant) {
  return constant.global && (constant.name=="g_mProjection" || constant.name=="g_mView" ||
    constant.name=="g_mViewTranspose" || constant.name=="g_mViewProjection");
}
// Whether NativeScenePassAnimation::Apply rewrites this constant: the only
// constants whose pass value depends on the animation (never a camera one).
inline bool NativeFullFrameStaticAnimationConstant(const NativeSceneMaterialInputs::Constant& constant) {
  return constant.global && (constant.name=="m_WaterTime" || constant.name=="g_SignalBrightness");
}

// One instanced draw: one group's geometry and material, every selected
// instance with its published world. instances are scene objects in draw
// order; the renderer instances adjacent objects that share geometry and
// material, which all of these do.
struct NativeFullFrameStaticDraw {
  uint32_t owner=0,group=0;
  std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
  std::shared_ptr<const NativeSceneMaterial> material;
  NativeSceneView view;
  // Shared storage: a snapshot, or the next frame's unchanged draw, copies it in O(1).
  NativeSceneInstances instances;
  // The instances' worlds, in order, when every instance draws this draw's
  // geometry and material, is visible, unbounded and not interpolated in
  // Snapshot (changed_tick is not its tick 0): what
  // NativeSceneRenderer::RenderUniform records Snapshot from without reading
  // the instances. Null otherwise. Kept with the instances while they do not
  // move, so an unchanged group's recording reads one array.
  std::shared_ptr<const std::vector<NativeSceneMatrix>> worlds;
  NativeSceneSnapshot Snapshot() const { return {0,instances}; }
  // Draw's worlds when every instance has that shape (see worlds).
  static std::shared_ptr<const std::vector<NativeSceneMatrix>> UniformWorlds(const NativeFullFrameStaticDraw& draw) {
    auto worlds=std::make_shared<std::vector<NativeSceneMatrix>>();
    worlds->reserve(draw.instances.size());
    for(const auto& instance:draw.instances) {
      const auto& object=instance->object;
      if(object.geometry!=draw.geometry || object.material!=draw.material || !object.visible || object.bounds ||
         instance->changed_tick==0) return nullptr;
      worlds->push_back(object.world);
    }
    return worlds;
  }
};
struct NativeFullFrameStaticFrame {
  struct Stats {
    uint64_t groups=0,draws=0,instances=0,resolves=0,cache_hits=0,missing_group=0,missing_material=0,
      missing_geometry=0,scissor=0,declined=0,world_declines=0,retained_objects=0,fresh_objects=0,
      camera_only=0,reused_draws=0,reused_frame=0,reused_moved=0,moved_owners=0,moved_instances=0,reused_instances=0,keyed=0;
    // reused_moved: reused_draws across a new generation; moved_*: that
    // generation's moved keys; reused_instances: selected instances whose
    // outcome was carried rather than resolved (whole draws included).
  };
  NativeFullFrameStaticSelection selection;
  std::vector<NativeFullFrameStaticDraw> draws;
  Stats stats;
};
// Cross-frame state: the per-group material cache and instance reuse. The
// material cache is keyed by the program pointer, geometry, pass base state
// and targets; never by guest state. A hit also requires this frame's pass
// constants (the published ones with camera and animation applied) to match
// but for the camera matrices the capture derives and the zeroed world (see
// NativeStaticWorldGroupCache::Current). Not synchronized.
//
// What a frame costs follows what moved since the last one (every result is
// the one a build from scratch returns, but for the ids of frame-local objects):
//  - Select always selects (the route words are read live, so no selection
//    outlives its frame), through selection_cache: the flattened tree, and
//    per list the cluster bounds that drop whole the objects the exact test
//    would cull, so the per-object work is the objects in or near the view.
//  - Per group, the last build's lookups (sources group, published material
//    and geometry) answer while the three maps share storage with the last
//    build's, and then with the same pass its cache key is the same: the
//    entry it found (memo.keyed, while the cache erased nothing) is reused
//    without building or comparing the key.
//  - BuildSelected returns the previous frame whole when the selection's
//    groups and instances equal the ones it was built from and its own inputs
//    (sources, group materials and geometry, by_source, pass camera,
//    animation, pass) are the same and no group was resolved since
//    (cache.stores).
//  - Per group, the pass constants persist (Memo): while the published group
//    material is the same object only the camera constants (a moved pass
//    camera) and the animation constants (a moved animation) are rewritten,
//    in place. Once Current accepted them for an entry, and every camera
//    constant is one it compares by shape alone (it reaches the capture only
//    as the derived camera), later frames skip Current's compare and derive
//    the camera alone - until that entry is stored again (its own stamp, so
//    another group's resolve keeps it) or an animation constant's bytes move
//    (Current compares those whole). The animation advances every gameplay
//    frame; most materials read none of it and keep their acceptance.
//  - Per group, the instances are the previous frame's while the selected
//    instances, geometry, material and world layout are the same and either
//    sources and by_source are too, or the group was built from the last
//    build's generation and nothing any of its instances reads moved from
//    that one to this: its parts (Find), its owners (world registers and
//    generation) and their by_source objects. The moved keys are the two
//    generations' difference (NativeSceneSources::Differences,
//    NativeSharedMap::Difference), computed once per build from the chunks
//    written between them; gameplay publishes a new generation whenever any
//    object moves, so the comparison of whole generations alone never
//    reuses. Only a group some moved owner files a part in (Moved::groups)
//    scans its instances against the moved keys; the others are unmoved at
//    once. When the group's selection moved (an object entered or left
//    the view), each instance the last build also selected, with nothing it
//    read moved, keeps its outcome (declined, or the same object) and only
//    the others are resolved. A frame-local object keeps its id across frames.
//  - Per group, the instances' worlds are kept with them when the draw is
//    uniform (NativeFullFrameStaticDraw::worlds): recording an unchanged
//    group reads that one array, not the instance objects.
// resolve is taken to be the same function on every call.
class NativeFullFrameStaticWorld {
 public:
  using Cache=NativeStaticWorldGroupCache<NativeFullFrameStaticTargets,NativeFullFrameStaticMaterial>;
  // resolve(group material, geometry, base state, pass constants) returns the
  // group's material or throws; ResolveNativeFullFrameStaticMaterial is the
  // production resolver. Runs once per group per cache miss.
  template<class Resolve>
  const NativeFullFrameStaticFrame& Build(const NativeScenePublication& publication,const NativeFullFrameStaticCamera& camera,
      const NativeFullFrameStaticRouteRead& route,const NativeFullFrameStaticPass& pass,Resolve&& resolve) {
    Select(publication,camera,route);
    return BuildSelected(publication,camera,pass,std::forward<Resolve>(resolve));
  }
  // The two halves of Build, for timing them apart: BuildSelected draws the
  // selection the last Select made, with the same publication and camera.
  const NativeFullFrameStaticSelection& Select(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticRouteRead& route);
  template<class Resolve>
  const NativeFullFrameStaticFrame& BuildSelected(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticPass& pass,Resolve&& resolve);
  const NativeFullFrameStaticFrame& frame() const { return frame_; }
  Cache cache;
  NativeSceneInstanceReuse reuse;
  NativeFullFrameStaticSelectCache selection_cache;
 private:
  using Constants=std::vector<NativeSceneMaterialInputs::Constant>;
  using BySource=decltype(NativeScenePublication::by_source);
  // One selected instance's instance build: the source owner it was
  // resolved through (0: no source) and its object (none when declined).
  struct Outcome {
    enum Kind : uint8_t { Declined,Retained,Fresh };
    uint32_t owner=0;
    Kind kind=Declined;
    std::shared_ptr<const NativeSceneInstance> object;
  };
  struct Memo {
    // constants: published's constants with pass_camera and animation applied;
    // cameras: the indices NativeFullFrameStaticCameraConstant names.
    std::shared_ptr<const NativeSceneGroupMaterial> published;
    std::optional<NativeScenePassAnimation> animation;
    NativeScenePassCamera pass_camera,derived;
    Constants constants;
    std::vector<uint32_t> cameras,animated;
    // Current accepted constants for entry (at its stored stamp); derived is
    // the pass camera entry's camera was last derived at.
    const Cache::Entry* entry=nullptr;
    uint64_t stores=0;
    bool accepted=false,camera_only=false;
    NativeStaticCameraPlan plan;  // Made at acceptance (Current).
    // The last instance build and what it depended on; outcomes[i] is
    // selected[i]'s.
    std::vector<uint32_t> selected;
    std::vector<Outcome> outcomes;
    std::shared_ptr<const NativeSceneSources> sources;
    BySource by_source;
    std::shared_ptr<const NativeIndexedMesh::RetainedDraw> geometry;
    std::shared_ptr<const NativeSceneMaterial> material;
    uint32_t first=0;
    bool column_major=false,built=false;
    NativeSceneInstances instances;
    std::shared_ptr<const std::vector<NativeSceneMatrix>> worlds;  // NativeFullFrameStaticDraw::worlds.
    uint64_t retained=0,fresh=0,declined=0,used=0;
    // The last build's lookups of this group (at build looked): its sources
    // group, published material and geometry, each null when absent or of
    // another revision. Pointers into chunks of that build's maps, valid and
    // the same answers while this build's maps share them (BuildSelected's lookups).
    uint64_t looked=0;
    const NativeSceneSources::Group* membership=nullptr;
    const std::shared_ptr<const NativeSceneGroupMaterial>* found_material=nullptr;
    const std::shared_ptr<const NativeSceneGroupGeometry>* found_geometry=nullptr;
    // The cache entry the last build's key found or stored (at build keyed,
    // with the cache's erasures then): the entry Candidate returns for the
    // same key while no entry was erased since.
    Cache::Entry* keyed=nullptr;
    uint64_t keyed_pass=0,keyed_erasures=0;
  };
  // NativeFullFrameStaticConstants(published,camera), kept in memo.
  static const Constants& PassConstants(Memo& memo,const std::shared_ptr<const NativeSceneGroupMaterial>& published,
      const NativeFullFrameStaticCamera& camera) {
    if(memo.published!=published) {
      memo.published=nullptr; memo.accepted=false;
      memo.constants=published->constants;  // Assignment keeps each constant's storage.
      ApplyNativeFullFrameStaticConstants(memo.constants,camera);
      memo.cameras.clear(); memo.animated.clear();
      for(size_t i=0;i<memo.constants.size();++i) {
        if(NativeFullFrameStaticCameraConstant(memo.constants[i])) memo.cameras.push_back(uint32_t(i));
        else if(NativeFullFrameStaticAnimationConstant(memo.constants[i])) memo.animated.push_back(uint32_t(i));
      }
      memo.published=published; memo.animation=camera.animation; memo.pass_camera=camera.pass;
      return memo.constants;
    }
    // Apply rewrites each constant from itself and the camera or animation
    // alone, so rewriting only the ones that depend on what moved gives the
    // bytes applying all of them to the published constants gives.
    if(!(memo.pass_camera==camera.pass)) {
      // Only the camera constants depend on the pass camera.
      memo.published=nullptr;
      for(const auto i:memo.cameras) camera.pass.Apply(memo.constants[i]);
      memo.published=published; memo.pass_camera=camera.pass;
    }
    if(memo.animation!=camera.animation) {
      // Only the animation constants depend on the animation. Current compares
      // them whole: its acceptance stands only while their bytes do.
      memo.published=nullptr;
      for(const auto i:memo.animated) {
        auto& constant=memo.constants[i];
        if(!camera.animation) throw std::runtime_error("native full-frame static material needs the world animation");
        std::array<uint8_t,16> old{};
        const bool sized=constant.registers.size()==old.size();
        if(sized) std::copy(constant.registers.begin(),constant.registers.end(),old.begin());
        camera.animation->Apply(constant);  // Throws unless 16 bytes.
        if(!sized || !std::equal(old.begin(),old.end(),constant.registers.begin())) memo.accepted=false;
      }
      memo.published=published; memo.animation=camera.animation;
    }
    return memo.constants;
  }
  // Cache::Current(entry,memo.constants,...) with its compare skipped when
  // only constants it compares by shape moved since it accepted them.
  bool Current(Memo& memo,Cache::Entry& entry,const NativeFullFrameStaticCamera& camera,NativeFullFrameStaticFrame::Stats& stats) {
    auto& material=entry.material;
    if(memo.accepted && memo.camera_only && memo.entry==&entry && memo.stores==entry.stored) {
      // Current's result for these constants: the entry's camera is already
      // the one they derive when it is underived or derived at this camera.
      if(!entry.derived || memo.derived==camera.pass) { ++stats.camera_only; return true; }
      // The plan made at acceptance: this material and these constants' shape.
      if(material.material) if(const auto view=memo.plan.Derive(memo.constants)) {
        material.camera.view=view->view; material.camera.projection=view->projection;
        material.camera.view_projection=view->view_projection; memo.derived=camera.pass;
        ++stats.camera_only; return true;
      }
    }
    memo.accepted=false;
    if(!Cache::Current(entry,memo.constants,material.material.get(),material.camera)) return false;
    memo.accepted=true; memo.entry=&entry; memo.stores=entry.stored; memo.derived=camera.pass;
    memo.camera_only=std::ranges::all_of(memo.cameras,[&](uint32_t i) { return entry.derived && entry.camera[i]; });
    // The camera-only path's derivation (NativeStaticCaptureCamera's result for
    // this material and any constants of this shape): valid while the
    // acceptance is, as the published constants object and the entry's store
    // stamp fix both.
    memo.plan=entry.derived && material.material?NativeStaticCameraPlan::Make(*material.material,memo.constants):NativeStaticCameraPlan{};
    return true;
  }
  struct BuildInputs {
    std::shared_ptr<const NativeSceneSources> sources;
    NativeSceneSources::GroupMap groups;  // sources' groups (shared, O(1)).
    decltype(NativeScenePublication::group_materials) materials;
    decltype(NativeScenePublication::group_geometry) geometry;
    BySource by_source;
    NativeScenePassCamera pass_camera;
    std::optional<NativeScenePassAnimation> animation;
    NativeFullFrameStaticPass pass;
    uint64_t stores=0;
    bool valid=false;
  };
  // The keys whose answers moved from built_'s sources and by_source to this
  // build's (sorted, unique); all when the two cannot be compared. groups:
  // every group one of built_'s parts of a moved owner, or of a moved
  // instance's owner, files its part in (sorted, unique).
  struct Moved {
    bool computed=false,all=false;
    std::vector<uint32_t> owners,instances,groups;
  };
  void ComputeMoved(const NativeScenePublication& publication) {
    auto& moved=moved_;
    moved.computed=true; moved.all=false; moved.owners.clear(); moved.instances.clear(); moved.groups.clear();
    const auto& before=built_;
    if(before.sources!=publication.sources) {
      if(!before.sources || !publication.sources) { moved.all=true; return; }
      publication.sources->Differences(*before.sources,[&](uint32_t owner) { moved.owners.push_back(owner); },
        [&](uint32_t instance) { moved.instances.push_back(instance); });
    }
    // Resolve's by_source key is {owner, generation, lod, part}.
    publication.by_source.Difference(before.by_source,[&](const auto& key) { moved.owners.push_back(uint32_t(key[0])); });
    for(auto* keys:{&moved.owners,&moved.instances}) {
      std::ranges::sort(*keys); keys->erase(std::unique(keys->begin(),keys->end()),keys->end());
    }
    if(!before.sources) { moved.all=true; return; }
    const auto file=[&](uint32_t owner) {
      const auto view=before.sources->FindCandidateView(owner);
      for(const auto& parts:view.lods) for(const auto& part:parts) moved.groups.push_back(part.group);
    };
    for(const auto owner:moved.owners) file(owner);
    for(const auto instance:moved.instances) if(const auto* source=before.sources->Find(instance)) file(source->owner);
    std::ranges::sort(moved.groups); moved.groups.erase(std::unique(moved.groups.begin(),moved.groups.end()),moved.groups.end());
  }
  // Whether anything memo's last instance build (of group, from built_'s
  // generation) read moved since built_: one of its instances, or the source
  // owner one resolved through. Each such instance is a part of that owner
  // filed in group in built_'s sources (the selection queued it from the
  // owner's parts in that generation, and Find(instance) is that owner), and a
  // moved instance's own entry moves only with its owner's; so a group no
  // moved owner files a part in has nothing moved, and only the groups one
  // does (moved_.groups) scan their instances.
  bool Moves(uint32_t group,const Memo& memo) const {
    if(moved_.all) return true;
    if(!std::ranges::binary_search(moved_.groups,group)) return false;
    for(size_t i=0;i<memo.selected.size();++i)
      if(std::ranges::binary_search(moved_.instances,memo.selected[i]) ||
         (memo.outcomes[i].owner && std::ranges::binary_search(moved_.owners,memo.outcomes[i].owner))) return true;
    return false;
  }
  BuildInputs built_;
  Moved moved_;
  // Scratch of one group's instance build: the last build's (instance,
  // position) sorted, and the new outcomes (swapped into the memo).
  std::vector<std::pair<uint32_t,uint32_t>> carried_;
  std::vector<Outcome> outcomes_;
  bool same_selection_=false;  // Select's groups and instances are the ones built_ drew.
  NativeFullFrameStaticFrame frame_;
  std::unordered_map<uint32_t,Memo> memos_;
  uint64_t pass_=0;
  uint64_t next_id_=(uint64_t(3)<<61);
  static constexpr uint64_t kMemoAge=256;
};

template<class Resolve>
const NativeFullFrameStaticFrame& NativeFullFrameStaticWorld::BuildSelected(const NativeScenePublication& publication,
    const NativeFullFrameStaticCamera& camera,const NativeFullFrameStaticPass& pass,Resolve&& resolve) {
  auto& in=built_;
  if(!NativeReuseAllowed()) {
    // Reuse off (native_reuse.h): no whole-frame reuse, no per-group memo
    // (lookups, keyed entry, pass constants, acceptance, camera plan,
    // instances and their outcomes), no cached resolve and no retained-object
    // reuse; each is made again below and kept for the next frame. The
    // selection was made with a frame-local cache (Select).
    in=BuildInputs{}; memos_.clear(); cache.Clear(); reuse.Clear(); same_selection_=false;
  }
  if(same_selection_ && in.valid && in.sources==publication.sources && in.materials.Shares(publication.group_materials) &&
     in.geometry.Shares(publication.group_geometry) && in.by_source.Shares(publication.by_source) &&
     in.pass_camera==camera.pass && in.animation==camera.animation && in.pass==pass && in.stores==cache.stores) {
    frame_.stats.reused_frame=1;
    return frame_;
  }
  // built_ still holds the last build's inputs until this one ends: the
  // generation per-group reuse across generations compares against.
  const bool baseline=in.valid;
  moved_.computed=false;
  in.valid=false;
  frame_.draws.clear(); frame_.stats={};
  ++pass_;
  auto& stats=frame_.stats;
  const auto base=NativeFullFrameStaticBaseState(pass.targets);
  const auto* sources=publication.sources.get();
  const auto report=[&](const char* reason) {
    ++stats.declined;
    (void)reason;
  };
  // The last build's per-group lookups answer for this one while its maps
  // share storage with the last build's (equal copies were never written
  // apart). The same material and geometry objects and the same pass then
  // make the same cache key (every component is the program's, the
  // geometry's or the pass's).
  const bool lookups=baseline && sources && in.sources && in.groups.Shares(sources->Groups()) &&
    in.materials.Shares(publication.group_materials) && in.geometry.Shares(publication.group_geometry);
  const bool same_pass=baseline && in.pass==pass;
  for(const auto& owner:frame_.selection.owners) for(const auto& selected:owner.groups) {
    ++stats.groups;
    auto& memo=memos_[selected.group];
    memo.used=pass_;
    const bool looked=lookups && memo.looked==pass_-1;
    if(!looked) {
      const auto* membership=sources?sources->FindGroup(selected.group):nullptr;
      memo.membership=membership; memo.found_material=nullptr; memo.found_geometry=nullptr;
      if(membership) {
        // Both are kept sorted and unique by group (NativeSceneAdapter publishes them so).
        const auto* material=publication.group_materials.Find(selected.group,NativeSceneGroupKey{});
        if(material && (*material)->revision==membership->revision) memo.found_material=material;
        const auto* geometry=publication.group_geometry.Find(selected.group,NativeSceneGroupKey{});
        if(geometry && (*geometry)->revision==membership->revision) memo.found_geometry=geometry;
      }
    }
    memo.looked=pass_;
    const auto* material=memo.found_material;
    const auto* geometry=memo.found_geometry;
    if(!memo.membership) { ++stats.missing_group; continue; }
    if(!material || !(*material)->program) { ++stats.missing_material; continue; }
    if(!geometry) { ++stats.missing_geometry; continue; }
    const auto& group=**material;
    const auto& program=*group.program;
    // Scissor enable's rectangle is not a pass input (as in the world pass).
    if(!program.CanDeferCpuActivation()) { ++stats.scissor; continue; }
    const auto first=program.inputs.WorldRegisterFirst();
    if(!first) { report("world register"); continue; }
    const auto make_key=[&] {
      return Cache::Key{.group=selected.group,.vertex=program.inputs.vertex,.pixel=program.inputs.pixel,
        .program=group.program,.setup=(*geometry)->setup.value_or(NativeSceneGeometrySource{}),
        .geometry=(*geometry)->geometry,.backend=program.backend,.pass=base,.view=pass.targets,.filtering=pass.filtering};
    };
    const NativeFullFrameStaticMaterial* resolved=nullptr;
    // The last build keyed this group to memo.keyed with the same objects
    // and pass: Candidate would return that entry again (or null had it been
    // erased, which erasures rules out).
    auto* const last_entry=looked && same_pass && memo.keyed_pass==pass_-1 && memo.keyed_erasures==cache.erasures?memo.keyed:nullptr;
    memo.keyed=nullptr;
    try {
      const auto& constants=PassConstants(memo,*material,camera);
      Cache::Entry* entry=nullptr;
      if(last_entry) { entry=last_entry; cache.Touch(*entry); ++stats.keyed; }
      else entry=cache.Candidate(make_key());
      if(entry && Current(memo,*entry,camera,stats)) {
        resolved=&entry->material; ++stats.cache_hits; ++cache.hits;
      } else {
        ++cache.misses; ++stats.resolves;
        NativeFullFrameStaticMaterial fresh=resolve(group,(*geometry)->geometry,base,std::span<const NativeSceneMaterialInputs::Constant>(constants));
        const auto captured=fresh.material;
        const auto view=fresh.camera;
        // No guest eligibility here: empty reads and witness. The published
        // group material is recorded as an observation only.
        entry=&cache.Store(make_key(),constants,NativeRecordedReads{},NativeStaticEligibilityWitness{},
          base.After(program),std::move(fresh),captured.get(),view,Cache::Observed{0,*material});
        resolved=&entry->material;
      }
      memo.keyed=entry; memo.keyed_pass=pass_; memo.keyed_erasures=cache.erasures;
    } catch(const std::exception& error) { report(error.what()); continue; }
    NativeFullFrameStaticDraw draw;
    draw.owner=owner.owner; draw.group=selected.group; draw.geometry=(*geometry)->geometry; draw.material=resolved->material;
    draw.view=resolved->camera;
    draw.view.viewport=pass.viewport; draw.view.scissor=pass.scissor; draw.view.scissor_enabled=resolved->render[5]!=0;
    // What memo's last instance build read, for the instances this one also
    // selects: the same group inputs, and either this very generation or the
    // last build's, from which this one differs only by moved_.
    const bool layout=memo.built && memo.geometry==draw.geometry && memo.material==draw.material &&
      memo.column_major==resolved->world_column_major && memo.first==*first;
    const bool same_generation=layout && memo.sources==publication.sources && memo.by_source.Shares(publication.by_source);
    const bool last_generation=layout && !same_generation && baseline && memo.sources==in.sources &&
      memo.by_source.Shares(in.by_source);
    if(last_generation && !moved_.computed) {
      ComputeMoved(publication);
      stats.moved_owners=moved_.owners.size(); stats.moved_instances=moved_.instances.size();
    }
    if((same_generation || last_generation) && memo.selected==selected.instances && (same_generation || !Moves(selected.group,memo))) {
      if(last_generation) { memo.sources=publication.sources; memo.by_source=publication.by_source; ++stats.reused_moved; }
      draw.instances=memo.instances; draw.worlds=memo.worlds; ++stats.reused_draws;
      stats.reused_instances+=memo.selected.size();
    } else {
      // Per instance: one the last build also selected, with nothing it read
      // moved, has the outcome it had (declined, or the same object); only
      // the rest are resolved. The group's selection moves whenever one of its
      // objects enters or leaves the view, which the camera does every frame.
      const bool carry=same_generation || last_generation;
      if(carry) {
        carried_.clear(); carried_.reserve(memo.selected.size());
        for(uint32_t i=0;i<memo.selected.size();++i) carried_.emplace_back(memo.selected[i],i);
        std::ranges::sort(carried_);
      }
      auto& outcomes=outcomes_;
      outcomes.clear(); outcomes.resize(selected.instances.size());
      // memo.selected and memo.outcomes stay the last build's until the swap.
      memo.built=false; memo.retained=memo.fresh=memo.declined=0;
      const auto count=[&](const Outcome& outcome) {
        (outcome.kind==Outcome::Declined?memo.declined:outcome.kind==Outcome::Retained?memo.retained:memo.fresh)+=1;
      };
      for(size_t at=0;at<selected.instances.size();++at) {
        const auto instance=selected.instances[at];
        auto& outcome=outcomes[at];
        if(carry) {
          const auto found=std::ranges::lower_bound(carried_,std::pair<uint32_t,uint32_t>{instance,0});
          if(found!=carried_.end() && found->first==instance) {
            const auto& last=memo.outcomes[found->second];
            if(same_generation || (!moved_.all && !std::ranges::binary_search(moved_.instances,instance) &&
               !(last.owner && std::ranges::binary_search(moved_.owners,last.owner)))) {
              outcome=last; count(outcome); ++stats.reused_instances;
              if(outcome.object) draw.instances.push_back(outcome.object);
              continue;
            }
          }
        }
        // The instance's parameters must be its world alone, in the program's
        // g_mWorld registers (recorded at the source's lifetime event).
        const auto* source=sources->Find(instance);
        if(source) outcome.owner=source->owner;
        const auto world=source && source->world_data && source->world_first && *source->world_first==*first?
          sources->WorldRegisters(*source,source->world_data):nullptr;
        if(!world) { outcome.kind=Outcome::Declined; count(outcome); continue; }
        const auto matrix=DecodeNativeQueuedWorld(*world,resolved->world_column_major);
        auto object=publication.Resolve(*source,draw.geometry,draw.material,matrix,&reuse);
        outcome.kind=Outcome::Retained;
        if(!object) {
          // No retained lifetime yet (never observed): a frame-local object.
          auto fresh=std::make_shared<NativeSceneInstance>();
          fresh->id=++next_id_; fresh->changed_tick=UINT64_MAX;
          fresh->object.geometry=draw.geometry; fresh->object.material=draw.material;
          fresh->object.world=matrix; fresh->previous=matrix;
          object=std::move(fresh); outcome.kind=Outcome::Fresh;
        }
        count(outcome);
        outcome.object=object;
        draw.instances.push_back(std::move(object));
      }
      std::swap(memo.outcomes,outcomes);
      memo.selected=selected.instances; memo.sources=publication.sources; memo.by_source=publication.by_source;
      memo.geometry=draw.geometry; memo.material=draw.material; memo.column_major=resolved->world_column_major;
      memo.first=*first; memo.instances=draw.instances; memo.built=true;
      memo.worlds=draw.worlds=NativeFullFrameStaticDraw::UniformWorlds(draw);
    }
    stats.world_declines+=memo.declined; stats.retained_objects+=memo.retained; stats.fresh_objects+=memo.fresh;
    if(draw.instances.empty()) continue;
    stats.instances+=draw.instances.size(); ++stats.draws;
    frame_.draws.push_back(std::move(draw));
  }
  cache.EndPass(); reuse.EndPass();
  if(!(pass_%kMemoAge)) std::erase_if(memos_,[&](const auto& item) { return pass_-item.second.used>kMemoAge; });
  in={publication.sources,sources?sources->Groups():NativeSceneSources::GroupMap{},publication.group_materials,
    publication.group_geometry,publication.by_source,camera.pass,camera.animation,pass,cache.stores,true};
  return frame_;
}
}
