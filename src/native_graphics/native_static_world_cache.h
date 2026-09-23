#pragma once
#include "native_recorded_reads.h"
#include "native_scene_adapter.h"
#include "native_static_group_eligibility.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace edf::native {
// The guest constant a capture reads each camera matrix from (CaptureNativeSceneMaterial).
inline const char* NativeSceneCameraConstantName(NativeSceneMatrixSource source) {
  switch(source) {
    case NativeSceneMatrixSource::View: return "g_mView";
    case NativeSceneMatrixSource::ViewTranspose: return "g_mViewTranspose";
    case NativeSceneMatrixSource::Projection: return "g_mProjection";
    case NativeSceneMatrixSource::ViewProjection: return "g_mViewProjection";
    default: return nullptr;
  }
}
// The camera NativeSceneMaterialProgram::Capture derives for material from
// constants, without building bindings. Capture applies each constant to the
// variable of its name in its stage (the last one of a name wins, the first 64
// bytes of a 4x4), reads every used matrix back through ReadFloat4x4 (row i of
// a row_major matrix is register i; a column_major one is transposed), zeroes
// those 64 bytes of the image and assigns the camera. The matrices it recorded
// are material's, so a used camera matrix is one of them. Nothing unless every
// recorded camera matrix has its constant, every assignment of one camera
// matrix is bitwise and == equal (so Capture neither throws nor keeps a value
// that depends on its visit order) and the camera is complete. derived, when
// given, marks each constant a recorded camera matrix read: a variable whose
// bytes are zeroed from the image, so it reaches the capture only as this camera.
inline std::optional<NativeSceneView> NativeStaticCaptureCamera(const NativeSceneMaterial& material,
    std::span<const NativeSceneMaterialInputs::Constant> constants,std::vector<uint8_t>* derived=nullptr) {
  std::optional<NativeSceneMatrix> view,projection,view_projection;
  std::vector<uint8_t> used(constants.size());
  for(const auto& image:material.constants()) for(const auto& matrix:image.matrices) {
    if(matrix.source==NativeSceneMatrixSource::World) continue;
    const auto* name=NativeSceneCameraConstantName(matrix.source);
    if(!name) return {};
    const bool pixel=image.stage==NativeBackendStage::Pixel;
    size_t found=constants.size();
    for(size_t i=0;i<constants.size();++i) if(constants[i].pixel==pixel && constants[i].name==name) found=i;
    if(found==constants.size() || constants[found].registers.size()<64) return {};
    used[found]=1;
    const auto* bytes=constants[found].registers.data();
    NativeSceneMatrix value;
    for(size_t row=0;row<4;++row) for(size_t column=0;column<4;++column) {
      const auto* word=bytes+(matrix.column_major?column*16+row*4:row*16+column*4);
      value[row*4+column]=std::bit_cast<float>(uint32_t(word[0])<<24|uint32_t(word[1])<<16|uint32_t(word[2])<<8|word[3]);
    }
    if(matrix.source==NativeSceneMatrixSource::ViewTranspose) value=NativeSceneTranspose(value);
    auto& destination=matrix.source==NativeSceneMatrixSource::Projection?projection:
      matrix.source==NativeSceneMatrixSource::ViewProjection?view_projection:view;
    if(destination && (*destination!=value || std::memcmp(destination->data(),value.data(),sizeof(value)))) return {};
    destination=value;
  }
  if(!view_projection && !(view && projection)) return {};
  NativeSceneView result;
  if(view) result.view=*view;
  if(projection) result.projection=*projection;
  result.view_projection=view_projection;
  if(derived) *derived=std::move(used);
  return result;
}
inline bool NativeSceneCameraIdentical(const NativeSceneView& a,const NativeSceneView& b) {
  const auto same=[](const NativeSceneMatrix& x,const NativeSceneMatrix& y) { return !std::memcmp(x.data(),y.data(),sizeof(x)); };
  return same(a.view,b.view) && same(a.projection,b.projection) &&
    a.view_projection.has_value()==b.view_projection.has_value() &&
    (!a.view_projection || same(*a.view_projection,*b.view_projection));
}
// Every input of one static group's per-pass work in the native world pass
// other than the pass constants, compared whole. The pass reuses a group's
// eligibility, chained pass state, resolved and interned material, pipeline
// and world binding only while all of these match and Current accepts the
// constants. Completeness, per cached computation:
// - AssessNativeStaticGroup(reader,device,stack,setup,witness): device and
//   setup are here; every guest byte it read through the recorder is in the
//   entry's NativeRecordedReads and compared before reuse (a deterministic
//   reader of those bytes visits the same addresses and returns the same
//   result). The stack, fence and retirement slots are per-frame and not
//   recorded: the entry's NativeStaticEligibilityWitness re-checks the
//   predicate the outcome depends on (stack aliasing, zero/non-zero fence and
//   retired-handle aliasing) against this pass's stack and words.
// - CanDeferCpuActivation, pass.After(program) and its decode: program and
//   pass (the incoming render words and sampler pass, the cursor it chains).
// - program.Resolve: vertex/pixel ids and shaders (program), reverse depth
//   (view's viewport), input layout (geometry), target formats and samples
//   (view's targets), render and sampler pass (pass), filtering, the
//   backend's pipeline and sampler caches (backend: never cleared, so a
//   description returns the same object) and the constants (Current).
// - InternMaterial: the cached material stays alive, so it stays the first
//   equivalent candidate the adapter would return for an equal material.
// - ConfigureNativeQueuedWorldLocked: the resolved material, the program and
//   the registered vertex shader bindings, which change only with shaders
//   (the registry generation, advanced on every register and release).
// Not the published NativeSceneGroupMaterial object: the preload republishes
// it whenever a refreshed constant moves (g_mWorld, the last drawn object's
// world, moves every tick), and the resolve reads it only as its program (held
// here) and its constants (compared by Current); its revision is checked per
// instance against the live membership. program, geometry and backend are
// held so their addresses are never reused. A backend or device replacement
// is a new backend pointer.
template<class View>
struct NativeStaticWorldGroupKey {
  uint32_t group=0,device=0,vertex=0,pixel=0;
  std::shared_ptr<const NativeSceneMaterialProgram> program;
  NativeSceneGeometrySource setup;
  std::shared_ptr<const void> geometry,backend;
  NativeSceneMaterialPassState pass;
  View view{};
  int filtering=-1;
  uint64_t shaders=0;
  bool operator==(const NativeStaticWorldGroupKey& other) const {
    return group==other.group && device==other.device && vertex==other.vertex && pixel==other.pixel &&
      program==other.program && setup==other.setup && geometry==other.geometry &&
      backend==other.backend && pass==other.pass && view==other.view && filtering==other.filtering && shaders==other.shaders;
  }
};
// Why a group's lookup missed, for the pass's miss diagnostic. Key components
// through Shaders; Reads/Witness are eligibility; Constants/Camera/World are
// Current. Published and Stack are observations only (neither is compared):
// the published group material object, or the pass stack, differed from the
// stored resolve's.
enum class NativeStaticWorldMiss : uint32_t {
  Absent,Device,Vertex,Pixel,Program,Setup,Geometry,Backend,Pass,View,Filtering,Shaders,
  Reads,Witness,Constants,Camera,World,Published,Stack,Count
};
inline constexpr std::array<const char*,size_t(NativeStaticWorldMiss::Count)> kNativeStaticWorldMissNames{
  "absent","device","vertex","pixel","program","setup","geometry","backend","pass","view","filtering","shaders",
  "reads","witness","constants","camera","world","published","stack"};
constexpr uint32_t NativeStaticWorldMissBit(NativeStaticWorldMiss miss) { return 1u<<uint32_t(miss); }
template<class View>
uint32_t NativeStaticWorldKeyDifferences(const NativeStaticWorldGroupKey<View>& a,const NativeStaticWorldGroupKey<View>& b) {
  using M=NativeStaticWorldMiss;
  uint32_t mask=0;
  const auto differ=[&](bool different,M miss) { if(different) mask|=NativeStaticWorldMissBit(miss); };
  differ(a.group!=b.group,M::Absent); differ(a.device!=b.device,M::Device); differ(a.vertex!=b.vertex,M::Vertex);
  differ(a.pixel!=b.pixel,M::Pixel); differ(a.program!=b.program,M::Program); differ(!(a.setup==b.setup),M::Setup);
  differ(a.geometry!=b.geometry,M::Geometry); differ(a.backend!=b.backend,M::Backend); differ(!(a.pass==b.pass),M::Pass);
  differ(!(a.view==b.view),M::View); differ(a.filtering!=b.filtering,M::Filtering); differ(a.shaders!=b.shaders,M::Shaders);
  return mask;
}
// A NaN world makes CaptureNativeSceneMaterial's `*value!=result.world` throw.
inline bool NativeStaticWorldHasNaN(std::span<const uint8_t> registers) { return NativeSceneWorldHasNaN(registers); }
// Diagnostic observations of a stored resolve; never compared for reuse.
struct NativeStaticWorldObserved {
  uint32_t stack=0;
  std::shared_ptr<const void> published;
};
// Cross-frame per-group cache of the static world pass, one entry per group
// address. Not synchronized: the pass uses it under the bridge lock.
template<class View,class Material>
class NativeStaticWorldGroupCache {
 public:
  using Key=NativeStaticWorldGroupKey<View>;
  using Constants=std::vector<NativeSceneMaterialInputs::Constant>;
  using Observed=NativeStaticWorldObserved;
  struct Entry {
    Key key;
    // The pass constants the material was resolved with (camera and
    // animation applied); per constant whether it only reaches the capture as
    // its camera (derived again from the new constants on reuse), and whether
    // it is the object world whose 64 bytes the capture zeroes (see Store).
    Constants constants;
    std::vector<uint8_t> camera,world;
    bool derived=false;
    NativeRecordedReads reads;
    NativeStaticEligibilityWitness eligibility;
    NativeSceneMaterialPassState next;
    Material material{};
    Observed observed;
    uint64_t used=0;
    // The cache's stores count after this entry's Store: unique per store, so
    // an equal stamp on the same entry means it was not stored again since.
    uint64_t stored=0;
  };
  // The group's entry when every key component matches, else null.
  Entry* Candidate(const Key& inputs) {
    const auto found=entries_.find(inputs.group);
    if(found==entries_.end() || !(found->second.key==inputs)) return nullptr;
    found->second.used=pass_;
    return &found->second;
  }
  // The group's entry regardless of its key (diagnostics).
  const Entry* Find(uint32_t group) const {
    const auto found=entries_.find(group);
    return found==entries_.end()?nullptr:&found->second;
  }
  // Whether entry's material is the one these pass constants resolve to:
  // every constant equal except those that only feed the derived camera,
  // which is then derived from constants into camera (material is the
  // cached capture's), and the object world's zeroed 64 bytes. A camera that
  // cannot be derived is a miss. miss, when given, receives why it missed.
  static bool Current(const Entry& entry,std::span<const NativeSceneMaterialInputs::Constant> constants,
      const NativeSceneMaterial* material,NativeSceneView& camera,NativeStaticWorldMiss* miss=nullptr,size_t* differed=nullptr) {
    const auto fail=[&](NativeStaticWorldMiss why,size_t at) {
      if(miss) *miss=why;
      if(differed) *differed=at;
      return false;
    };
    using M=NativeStaticWorldMiss;
    if(constants.size()!=entry.constants.size()) return fail(M::Constants,constants.size());
    for(size_t i=0;i<constants.size();++i) {
      const auto& a=constants[i]; const auto& b=entry.constants[i];
      const bool shape=a.pixel==b.pixel && a.global==b.global && a.name==b.name && a.registers.size()==b.registers.size();
      if(entry.derived && entry.camera[i]) {
        if(!shape) return fail(M::Camera,i);
      } else if(!entry.world.empty() && entry.world[i]) {
        if(!shape || !std::equal(a.registers.begin()+64,a.registers.end(),b.registers.begin()+64)) return fail(M::Constants,i);
        if(NativeStaticWorldHasNaN(a.registers)) return fail(M::World,i);
      } else if(!(a==b)) return fail(M::Constants,i);
    }
    if(!entry.derived) return true;
    if(!material) return fail(M::Camera,constants.size());
    const auto derived=NativeStaticCaptureCamera(*material,constants);
    if(!derived) return fail(M::Camera,constants.size());
    camera.view=derived->view; camera.projection=derived->projection; camera.view_projection=derived->view_projection;
    return true;
  }
  // Records a resolve. The camera is derived on reuse only when deriving it
  // from these constants reproduces the resolved capture's (captured, camera)
  // bit for bit; otherwise, or with no capture, every camera constant must
  // match. The object world is excluded when captured binds exactly one world
  // matrix, in the vertex stage (the only kind the pass records): then every
  // vertex g_mWorld constant's first 64 bytes land on that variable, which
  // CaptureNativeSceneMaterial zeroes from the image and reads back only into
  // capture.world - which the resolve replaces with each instance's published
  // world - and its only other use is a self-comparison that fails on NaN
  // (Current rejects a NaN world). Bytes past 64 are still compared.
  Entry& Store(Key key,Constants constants,NativeRecordedReads reads,NativeStaticEligibilityWitness eligibility,
      NativeSceneMaterialPassState next,Material material,const NativeSceneMaterial* captured,const NativeSceneView& camera,
      Observed observed={}) {
    if(entries_.size()>=kLimit && !entries_.contains(key.group)) entries_.clear();
    Entry entry{std::move(key),std::move(constants)};
    const auto derived=captured?NativeStaticCaptureCamera(*captured,entry.constants,&entry.camera):std::nullopt;
    entry.derived=derived && NativeSceneCameraIdentical(*derived,camera);
    if(!entry.derived) entry.camera.clear();
    if(captured) {
      size_t worlds=0; bool vertex=false;
      for(const auto& image:captured->constants()) for(const auto& matrix:image.matrices)
        if(matrix.source==NativeSceneMatrixSource::World) { ++worlds; vertex=image.stage==NativeBackendStage::Vertex; }
      if(worlds==1 && vertex) {
        entry.world.resize(entry.constants.size());
        for(size_t i=0;i<entry.constants.size();++i) {
          const auto& constant=entry.constants[i];
          entry.world[i]=!constant.pixel && constant.name=="g_mWorld" && constant.registers.size()>=64 &&
            !NativeStaticWorldHasNaN(constant.registers);
        }
      }
    }
    entry.reads=std::move(reads); entry.eligibility=std::move(eligibility); entry.next=std::move(next);
    entry.material=std::move(material); entry.observed=std::move(observed); entry.used=pass_;
    const auto group=entry.key.group;
    entry.stored=++stores;
    return entries_.insert_or_assign(group,std::move(entry)).first->second;
  }
  void Invalidate(uint32_t group) { entries_.erase(group); }
  void Clear() { entries_.clear(); }
  // Once per pass: groups unused for kAge passes (unloaded, or no longer
  // drawn natively) release what their entries hold.
  void EndPass() {
    if(++pass_%kAge) return;
    std::erase_if(entries_,[&](const auto& item) { return pass_-item.second.used>kAge; });
  }
  size_t size() const { return entries_.size(); }
  // Counts one miss with every reason in mask; returns the miss count.
  uint64_t Missed(uint32_t mask) {
    for(size_t i=0;i<missed.size();++i) if(mask>>i&1) ++missed[i];
    return ++miss_events;
  }
  std::string MissSummary() const {
    std::string text;
    for(size_t i=0;i<missed.size();++i) {
      if(!text.empty()) text+=' ';
      text+=kNativeStaticWorldMissNames[i]; text+='='; text+=std::to_string(missed[i]);
    }
    return text;
  }
  uint64_t hits=0,misses=0,stores=0,miss_events=0;
  std::array<uint64_t,size_t(NativeStaticWorldMiss::Count)> missed{};
 private:
  static constexpr size_t kLimit=16384;
  static constexpr uint64_t kAge=256;
  std::unordered_map<uint32_t,Entry> entries_;
  uint64_t pass_=0;
};
}
