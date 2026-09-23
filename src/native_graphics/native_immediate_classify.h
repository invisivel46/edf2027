#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace edf::native {
// What the DrawPrimitiveUP hook (821FD8F8) can tell about a draw from its
// shader pair alone: which of its native paths - movie, XUI brush, Utility 3D,
// Utility 2D - the pair's identities admit. Every other check those paths make
// (target, topology, declaration, textures, depth) depends on the draw and is
// still made per draw; the font path's identity lives in guest memory and is
// still read per draw too. The pair answer was recomputed per draw from up to
// eight hash lookups and a dozen entry-name comparisons, and only changes when
// a registry does, so it is kept per pair with the registry generations it
// was computed against.
struct NativeEmbeddedIdentity { uint32_t source=0; bool pixel=false; };
struct NativeSourceIdentity { uint64_t fingerprint=0; std::string_view entry; };
struct NativeImmediatePairClass {
  enum class Movie : uint8_t { None, Hd, Sd };
  enum class Xui : uint8_t { None, Texture, Solid, Mask };
  enum class Utility3D : uint8_t { None, Solid, Textured, Particle, ZParticle };
  enum class Utility2D : uint8_t { None, Solid, Textured };
  Movie movie=Movie::None;
  Xui xui=Xui::None;
  Utility3D utility3d=Utility3D::None;
  Utility2D utility2d=Utility2D::None;
  bool operator==(const NativeImmediatePairClass&) const=default;
};
inline constexpr uint64_t kNativeUtilitySourceFingerprint=0xc885203e230fe745ull;
inline constexpr uint64_t kNativeParticleSourceFingerprint=0x777f4cf51fb1b019ull;
// The same predicates the hook's paths apply, in the same terms. A null
// identity is a handle absent from that registry.
inline NativeImmediatePairClass ClassifyNativeImmediatePair(
    const NativeEmbeddedIdentity* vertex_embedded,const NativeEmbeddedIdentity* pixel_embedded,
    const NativeSourceIdentity* vertex_source,const NativeSourceIdentity* pixel_source) {
  NativeImmediatePairClass result;
  if(vertex_embedded && pixel_embedded && !vertex_embedded->pixel && pixel_embedded->pixel) {
    const auto vs=vertex_embedded->source,ps=pixel_embedded->source;
    // Movie: 82060B70 with the HD (82064428) or SD (820641F0) plane shader.
    if(vs==0x82060B70 && ps==0x82064428) result.movie=NativeImmediatePairClass::Movie::Hd;
    if(vs==0x82060B70 && ps==0x820641F0) result.movie=NativeImmediatePairClass::Movie::Sd;
    // XUI brush: 820608B0 with the texture, solid or mask brush shader.
    if(vs==0x820608B0 && ps==0x82060EC8) result.xui=NativeImmediatePairClass::Xui::Texture;
    if(vs==0x820608B0 && ps==0x82060DB0) result.xui=NativeImmediatePairClass::Xui::Solid;
    if(vs==0x820608B0 && ps==0x82061848) result.xui=NativeImmediatePairClass::Xui::Mask;
  }
  if(vertex_source && pixel_source) {
    const auto& vs=*vertex_source;
    const auto& ps=*pixel_source;
    const bool utility=vs.fingerprint==kNativeUtilitySourceFingerprint &&
                       ps.fingerprint==kNativeUtilitySourceFingerprint;
    if(utility && vs.entry=="VS_3D" && ps.entry=="PS_Main")
      result.utility3d=NativeImmediatePairClass::Utility3D::Solid;
    else if(utility && vs.entry=="VS_3DTex" && ps.entry=="PS_Tex")
      result.utility3d=NativeImmediatePairClass::Utility3D::Textured;
    else if(vs.fingerprint==kNativeParticleSourceFingerprint && ps.fingerprint==kNativeParticleSourceFingerprint &&
            vs.entry=="Vs_Particle" && (ps.entry=="Ps_Particle" || ps.entry=="Ps_ZParticle"))
      result.utility3d=ps.entry=="Ps_ZParticle"?NativeImmediatePairClass::Utility3D::ZParticle
                                               :NativeImmediatePairClass::Utility3D::Particle;
    if(utility && vs.entry=="VS_2DTex" && ps.entry=="PS_Tex")
      result.utility2d=NativeImmediatePairClass::Utility2D::Textured;
    else if(utility && vs.entry=="VS_2D" && ps.entry=="PS_Main")
      result.utility2d=NativeImmediatePairClass::Utility2D::Solid;
  }
  return result;
}
// A few recent pairs, each with the registry generations its answer was
// computed against; a changed generation recomputes. `Payload` is whatever
// the caller derives alongside (the registry entries the answer was read
// from), valid exactly as long as the answer is.
template<class Payload,size_t Slots=8>
class NativeImmediatePairMemo {
 public:
  struct Entry {
    uint32_t vertex=0,pixel=0;
    uint64_t shaders=0,embedded=0;
    bool valid=false;
    NativeImmediatePairClass kind;
    Payload payload{};
  };
  template<class Compute>
  const Entry& Get(uint32_t vertex,uint32_t pixel,uint64_t shaders,uint64_t embedded,Compute&& compute) {
    auto& entry=entries_[((vertex>>2)^(pixel>>2)^(pixel>>7))%Slots];
    if(entry.valid && entry.vertex==vertex && entry.pixel==pixel &&
       entry.shaders==shaders && entry.embedded==embedded) { ++hits_; return entry; }
    ++misses_;
    entry.valid=false;
    compute(entry.kind,entry.payload);
    entry.vertex=vertex; entry.pixel=pixel; entry.shaders=shaders; entry.embedded=embedded;
    entry.valid=true;
    return entry;
  }
  void Clear() { for(auto& entry:entries_) entry.valid=false; }
  uint64_t hits() const { return hits_; }
  uint64_t misses() const { return misses_; }
 private:
  std::array<Entry,Slots> entries_{};
  uint64_t hits_=0,misses_=0;
};
}  // namespace edf::native
