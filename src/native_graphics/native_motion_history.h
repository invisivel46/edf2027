#pragma once
#include "native_camera_history.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>

namespace edf::native {
// The pure half of the motion vectors (native_motion_vectors.h): the matrices
// the reprojection is made of, the previous rendered camera of each scene and
// the rules that say whether it is history. No backend, no guest memory.
//
// Conventions are the game's: row vectors (clip = position * view *
// projection), row-major matrices (element r*4+c), a D3D projection whose
// ndc z runs 0 at the near plane to 1 at the far plane. The scene is
// reversed-Z by the native vertex variant (z = w - z) over a 0..1 viewport,
// so a stored depth d is 1 - ndc z: 1 at the near plane, 0 at the far plane
// and for the clear (the sky). UV is viewport-relative, y down.
using NativeMotionMatrix=std::array<double,16>;

inline NativeMotionMatrix NativeMotionMultiply(const NativeMotionMatrix& a,const NativeMotionMatrix& b) {
  NativeMotionMatrix r{};
  for(size_t i=0;i<4;++i) for(size_t j=0;j<4;++j) {
    double sum=0;
    for(size_t k=0;k<4;++k) sum+=a[i*4+k]*b[k*4+j];
    r[i*4+j]=sum;
  }
  return r;
}
// General 4x4 inverse (cofactors); none when singular or not finite.
inline std::optional<NativeMotionMatrix> NativeMotionInverse(const NativeMotionMatrix& m) {
  NativeMotionMatrix inv{};
  inv[0]=m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
  inv[4]=-m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
  inv[8]=m[4]*m[9]*m[15]-m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
  inv[12]=-m[4]*m[9]*m[14]+m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
  inv[1]=-m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
  inv[5]=m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
  inv[9]=-m[0]*m[9]*m[15]+m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
  inv[13]=m[0]*m[9]*m[14]-m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
  inv[2]=m[1]*m[6]*m[15]-m[1]*m[7]*m[14]-m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7]-m[13]*m[3]*m[6];
  inv[6]=-m[0]*m[6]*m[15]+m[0]*m[7]*m[14]+m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7]+m[12]*m[3]*m[6];
  inv[10]=m[0]*m[5]*m[15]-m[0]*m[7]*m[13]-m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7]-m[12]*m[3]*m[5];
  inv[14]=-m[0]*m[5]*m[14]+m[0]*m[6]*m[13]+m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6]+m[12]*m[2]*m[5];
  inv[3]=-m[1]*m[6]*m[11]+m[1]*m[7]*m[10]+m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]+m[9]*m[3]*m[6];
  inv[7]=m[0]*m[6]*m[11]-m[0]*m[7]*m[10]-m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]-m[8]*m[3]*m[6];
  inv[11]=-m[0]*m[5]*m[11]+m[0]*m[7]*m[9]+m[4]*m[1]*m[11]-m[4]*m[3]*m[9]-m[8]*m[1]*m[7]+m[8]*m[3]*m[5];
  inv[15]=m[0]*m[5]*m[10]-m[0]*m[6]*m[9]-m[4]*m[1]*m[10]+m[4]*m[2]*m[9]+m[8]*m[1]*m[6]-m[8]*m[2]*m[5];
  const double det=m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
  if(!std::isfinite(det) || std::abs(det)<1e-300) return std::nullopt;
  for(auto& v:inv) { v/=det; if(!std::isfinite(v)) return std::nullopt; }
  return inv;
}
template<class T> NativeMotionMatrix NativeMotionMatrixOf(const std::array<T,16>& m) {
  NativeMotionMatrix r{};
  for(size_t i=0;i<16;++i) r[i]=double(m[i]);
  return r;
}
// A pass camera matrix as NativeScenePassCamera holds it: float bit patterns.
inline std::array<float,16> NativeMotionFloats(const std::array<uint32_t,16>& words) {
  std::array<float,16> r{};
  for(size_t i=0;i<16;++i) r[i]=std::bit_cast<float>(words[i]);
  return r;
}

// One rendered camera: the view and projection its passes drew with,
// unjittered.
struct NativeMotionCamera {
  std::array<float,16> view{},projection{};
  bool operator==(const NativeMotionCamera&) const=default;
  NativeMotionMatrix ViewProjection() const {
    return NativeMotionMultiply(NativeMotionMatrixOf(view),NativeMotionMatrixOf(projection));
  }
  // The view without its translation (row 3 = 0,0,0,1) times the projection:
  // what a direction (the sky) projects through.
  NativeMotionMatrix RotationViewProjection() const {
    auto rotation=NativeMotionMatrixOf(view);
    rotation[12]=rotation[13]=rotation[14]=0; rotation[15]=1;
    return NativeMotionMultiply(rotation,NativeMotionMatrixOf(projection));
  }
  // A D3D perspective projection (row vectors): p22 = f/(f-n), p32 = -n f/(f-n)
  // with p23 = 1 (left-handed), or p22 = -f/(f-n), p32 = -n f/(f-n) with
  // p23 = -1 (right-handed, the game's 821C82C0). p22 is taken times the sign
  // of p23, which turns the second into the first.
  double DepthScale() const { return projection[11]<0?-double(projection[10]):double(projection[10]); }
  float Near() const {
    const double p22=DepthScale(),p32=projection[14];
    return p22!=0?float(-p32/p22):0.f;
  }
  float Far() const {
    const double p22=DepthScale(),p32=projection[14];
    return p22!=1?float(p32/(1-p22)):INFINITY;
  }
  float FovY() const { return projection[5]!=0?float(2*std::atan(1.0/double(projection[5]))):0.f; }
  // The camera-to-world pose (the view's inverse) with the vertical fov, for
  // NativeCameraHistory's cut limits. Not ValidPose when the view is not rigid.
  NativeCameraPose Pose() const {
    NativeCameraPose pose;
    pose.fov=FovY();
    if(const auto inverse=NativeMotionInverse(NativeMotionMatrixOf(view)))
      for(size_t i=0;i<16;++i) pose.world[i]=float((*inverse)[i]);
    else pose.world.fill(NAN);
    return pose;
  }
};

// Current clip -> previous clip, for a scene point (geometry) and for a
// direction (sky, depth 0): inverse(current VP) * previous VP, and the same
// over the rotation-only matrices. Computed in double, used in float.
struct NativeMotionReprojection {
  NativeMotionMatrix geometry{},sky{};
};
inline std::optional<NativeMotionReprojection> NativeMotionReprojectionOf(const NativeMotionCamera& current,
    const NativeMotionCamera& previous) {
  const auto inverse=NativeMotionInverse(current.ViewProjection());
  const auto sky=NativeMotionInverse(current.RotationViewProjection());
  if(!inverse || !sky) return std::nullopt;
  return NativeMotionReprojection{NativeMotionMultiply(*inverse,previous.ViewProjection()),
    NativeMotionMultiply(*sky,previous.RotationViewProjection())};
}
// The motion the camera pass writes for a pixel at viewport UV (u, v) with
// stored depth d: previous UV - current UV. None when the point is at or
// behind the previous camera (w <= 0); the pass writes 0 there.
inline std::optional<std::array<double,2>> NativeMotionReprojectUv(const NativeMotionReprojection& reprojection,
    double u,double v,double depth) {
  const std::array<double,4> clip{u*2-1,1-v*2,1-depth,1};
  const auto& m=depth>0?reprojection.geometry:reprojection.sky;
  std::array<double,4> p{};
  for(size_t j=0;j<4;++j) for(size_t k=0;k<4;++k) p[j]+=clip[k]*m[k*4+j];
  if(!(p[3]>1e-6)) return std::nullopt;
  return std::array<double,2>{(p[0]/p[3])*0.5+0.5-u,0.5-(p[1]/p[3])*0.5-v};
}
// A world point's viewport UV under a camera (row vectors), for references.
inline std::optional<std::array<double,2>> NativeMotionProjectUv(const NativeMotionCamera& camera,
    const std::array<double,3>& point) {
  const auto m=camera.ViewProjection();
  const std::array<double,4> p4{point[0],point[1],point[2],1};
  std::array<double,4> c{};
  for(size_t j=0;j<4;++j) for(size_t k=0;k<4;++k) c[j]+=p4[k]*m[k*4+j];
  if(!(c[3]>1e-6)) return std::nullopt;
  return std::array<double,2>{(c[0]/c[3])*0.5+0.5,0.5-(c[1]/c[3])*0.5};
}

// One scene's rendered camera in one frame.
struct NativeMotionFrameCamera {
  NativeMotionCamera camera;
  uint64_t frame=0;
  std::array<uint32_t,4> viewport{};  // x, y, width, height in pixels
  uint32_t width=0,height=0;          // render size
};
enum class NativeMotionReset : uint8_t {
  None,      // history valid
  First,     // no earlier render of this scene (or after Reset)
  Gap,       // the scene did not render in the immediately previous frame
  Viewport,  // another viewport
  Size,      // another render size (the motion texture was made again)
  Cut,       // NativeCameraHistory::CutWith: a jump, a zoom or a turn past its limits
  Invalid,   // a camera whose view is not rigid or whose matrices do not invert
};
inline const char* NativeMotionResetName(NativeMotionReset reset) {
  switch(reset) {
    case NativeMotionReset::None: return "none";
    case NativeMotionReset::First: return "first";
    case NativeMotionReset::Gap: return "gap";
    case NativeMotionReset::Viewport: return "viewport";
    case NativeMotionReset::Size: return "size";
    case NativeMotionReset::Cut: return "cut";
    case NativeMotionReset::Invalid: return "invalid";
  }
  return "?";
}
// The previous rendered camera per scene. Advance records a scene's camera of
// one frame and answers with the camera it rendered with in the frame before,
// when that is history. A second Advance of the same scene in the same frame
// (a second view of it) replaces that frame's camera and answers against the
// same previous frame. Not synchronized.
class NativeMotionHistory {
 public:
  struct Result {
    NativeMotionReset reset=NativeMotionReset::First;
    NativeMotionFrameCamera previous;  // Meaningful when reset is None.
    explicit operator bool() const { return reset==NativeMotionReset::None; }
  };
  Result Advance(uint32_t scene,const NativeMotionFrameCamera& current) {
    auto& entry=scenes_[scene];
    if(!entry.has_current || entry.current.frame!=current.frame) {
      entry.previous=entry.current; entry.has_previous=entry.has_current;
    }
    entry.current=current; entry.has_current=true;
    Prune(current.frame);
    Result result;
    if(!entry.has_previous) return result;
    result.previous=entry.previous;
    result.reset=Judge(entry.previous,current);
    return result;
  }
  // Why `previous` is not history for `current` (None when it is).
  static NativeMotionReset Judge(const NativeMotionFrameCamera& previous,const NativeMotionFrameCamera& current) {
    if(previous.frame+1!=current.frame) return NativeMotionReset::Gap;
    if(previous.viewport!=current.viewport) return NativeMotionReset::Viewport;
    if(previous.width!=current.width || previous.height!=current.height) return NativeMotionReset::Size;
    const auto a=previous.camera.Pose(),b=current.camera.Pose();
    if(!NativeCameraHistory::ValidPose(a) || !NativeCameraHistory::ValidPose(b) ||
       !NativeMotionReprojectionOf(current.camera,previous.camera)) return NativeMotionReset::Invalid;
    if(NativeCameraHistory::CutWith(a,b,NativeCameraHistory::RotationOf(a.world),NativeCameraHistory::RotationOf(b.world)))
      return NativeMotionReset::Cut;
    return NativeMotionReset::None;
  }
  void Reset() { scenes_.clear(); }
  void Reset(uint32_t scene) { scenes_.erase(scene); }
  size_t size() const { return scenes_.size(); }
 private:
  struct Entry {
    NativeMotionFrameCamera previous,current;
    bool has_previous=false,has_current=false;
  };
  // Scenes not rendered for a while go: a scene object freed and reused at
  // the same address is a new scene, and its first render must not match.
  void Prune(uint64_t frame) {
    if(scenes_.size()<=8) return;
    std::erase_if(scenes_,[&](const auto& entry) { return entry.second.current.frame+2<frame; });
  }
  std::map<uint32_t,Entry> scenes_;
};
}  // namespace edf::native
