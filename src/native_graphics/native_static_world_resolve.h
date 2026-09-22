#pragma once
#include "native_scene.h"
#include "native_scene_geometry.h"
#include "d3d11_render_state.h"
#include "native_buffer_writes.h"
#include "native_scene_sources.h"
#include <span>
#include <vector>

namespace edf::native {
// Why a published static instance was not resolved. Declines with a reason are
// reported once each; the others are silent, as they were at the guest draw.
enum class NativeStaticWorldDecline {
  None, PendingWrites, Revision, GeometryPublication, GeometryCount, DeferredGeometry, GeometryBinding,
  BufferGeneration, VersionsUnavailable, VersionChanged, ProgramBinding, Targets, WorldParameter,
  Source, WorldRegisters, WorldMismatch, Lifetime, ComparisonChanged
};
inline const char* NativeStaticWorldDeclineReason(NativeStaticWorldDecline decline) {
  using D=NativeStaticWorldDecline;
  switch(decline) {
    case D::PendingWrites: return "pending resource writes";
    case D::Revision: return "group membership or preload revision";
    case D::GeometryPublication: return "retained geometry publication";
    case D::GeometryCount: return "geometry count";
    case D::DeferredGeometry: return "deferred geometry identity";
    case D::GeometryBinding: return "geometry binding identity";
    case D::BufferGeneration: return "model buffer generation";
    case D::VersionsUnavailable: return "observed resource versions unavailable";
    case D::VersionChanged: return "observed resource revision changed";
    case D::Lifetime: return "instance lifetime not in scene publication";
    case D::ComparisonChanged: return "due geometry comparison found a change";
    default: return nullptr;
  }
}
// Whether a source's instance parameters are its world alone - one range of
// four registers at first reading the owner's world data, clear of the
// device's constant block - from what its generation recorded at the lifetime
// event (ReadNativeStaticSceneParts): no guest read. A source without
// world_first is decided by reading its parameters instead.
inline bool NativeStaticWorldOnlySource(const NativeSceneSources::Source& source,uint32_t first,uint32_t device) {
  return source.world_data && source.world_first && *source.world_first==first &&
    !(uint64_t(source.world_data)<uint64_t(device)+5888 && uint64_t(source.world_data)+64>uint64_t(device)+1792);
}
// One static group's resolve, split by what varies. group() runs once and
// returns its decline: everything that depends on the group, its geometry and
// the pass (revisions, buffer generations, the due comparison, the material,
// its world binding and view), never on an instance. instance(i) runs per
// instance and returns a result with a decline: the source, its world
// registers and its publication object. The first decline of either ends the
// walk and is returned; resolved then holds fewer results than instances and
// the caller returns the whole group. With no instances nothing runs.
template<class Result,class Group,class Instance>
NativeStaticWorldDecline ResolveNativeStaticGroupInstances(std::span<const uint32_t> instances,std::vector<Result>& resolved,
    Group&& group,Instance&& instance) {
  resolved.clear();
  if(instances.empty()) return NativeStaticWorldDecline::None;
  if(const auto decline=group(); decline!=NativeStaticWorldDecline::None) return decline;
  resolved.reserve(instances.size());
  for(const auto at:instances) {
    auto result=instance(at);
    if(result.decline!=NativeStaticWorldDecline::None) return result.decline;
    resolved.push_back(std::move(result));
  }
  return NativeStaticWorldDecline::None;
}
// A draw whose sampled buffer comparison has fallen due runs it instead of
// declining until the next preload tick. The retained geometry stays drawable
// only when every source compared equal to the exact retained candidate at the
// loaded lifetime and revision; the guarded copy itself refuses overlapping or
// unranged writers and lost tracking. Audit and compare-every-observation
// policies are left to the preload and indexed paths, which carry their
// instrumentation, so they stay unavailable here as before.
enum class NativeStaticComparison { Unavailable, Confirmed, Changed, UnreportedChange };
inline NativeStaticComparison CompareNativeStaticGeometry(NativeBufferWrites& writes,
    const std::array<NativeBufferWrites::SnapshotSource,2>& sources,
    const std::array<NativeBufferWrites::ObservedVersion,2>& loaded,NativeBufferWrites::SnapshotPolicy policy) {
  using C=NativeStaticComparison;
  if(policy.audit_revisions || !policy.verify_interval) return C::Unavailable;
  for(const auto& source:sources) if(!source.candidate) return C::Unavailable;
  const auto observed=writes.CopyObservedSet(sources,nullptr,policy);
  if(!observed) return C::Unavailable;
  bool unreported=false,changed=false;
  for(size_t i=0;i<2;++i) {
    const auto& value=(*observed)[i];
    unreported|=value.unreported_change;
    changed|=value.contents!=sources[i].candidate || value.version.lifetime!=loaded[i].lifetime ||
      value.version.revision!=loaded[i].revision;
  }
  return unreported?C::UnreportedChange:changed?C::Changed:C::Confirmed;
}
// A draw's bound stream 0, indices and declaration against the preloaded
// identity. Each binding is read only once every earlier one matched, so a
// missing later binding cannot change which decline the draw reports.
template<class Stream,class Index,class Declaration>
bool NativeBoundGeometryMatches(const NativeSceneGeometrySource& source,Stream&& stream,Index&& index,Declaration&& declaration) {
  const auto& bound=stream();
  return bound.resource==source.vertex && !bound.offset && bound.stride==source.stride &&
    index()==source.index && declaration()==source.declaration;
}
// The resolved camera placed in the pass viewport. Scissor enable is the
// resolved render word 5, not the viewport's own flag.
inline NativeSceneView NativeStaticInstanceView(NativeSceneView view,const NativeViewportState& viewport,bool scissor_enabled) {
  const auto& v=viewport.viewport; const auto& s=viewport.scissor;
  view.viewport={v.TopLeftX,v.TopLeftY,v.Width,v.Height,v.MinDepth,v.MaxDepth};
  view.scissor={s.left,s.top,s.right,s.bottom}; view.scissor_enabled=scissor_enabled;
  return view;
}
}
