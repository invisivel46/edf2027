#pragma once
#include "native_scene.h"
#include "native_scene_geometry.h"
#include "d3d11_render_state.h"

namespace edf::native {
// Why a published static instance was not resolved. Declines with a reason are
// reported once each; the others are silent, as they were at the guest draw.
enum class NativeStaticWorldDecline {
  None, PendingWrites, Revision, GeometryPublication, GeometryCount, DeferredGeometry, GeometryBinding,
  BufferGeneration, VersionsUnavailable, VersionChanged, ProgramBinding, Targets, WorldParameter,
  Source, WorldRegisters, WorldMismatch, Lifetime
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
    default: return nullptr;
  }
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
