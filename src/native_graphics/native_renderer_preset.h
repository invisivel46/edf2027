#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>

namespace edf::native {
// One switch for the native renderer configuration (edf_native_renderer).
//
// Contract:
// - "off" adds nothing, "world" adds every flag the static world pass needs
//   (NativeScenePassMissingFlag plus the bridge, seam, activation and the pass
//   itself), "full" adds model publication, the rigid model pass and the
//   native post finish on top. Bucket dispatch and the map-effect list walk
//   are not in "full": their in-game audit and census have not been run yet.
//   Audits are never part of a preset.
// - The effective value of a flag is (individual cvar) OR (preset includes
//   it). An individual cvar can only add a flag; it cannot remove one the
//   preset turns on. To run a preset minus one flag, choose the smaller preset
//   and enable the rest individually.
// - The preset is resolved once at startup (ResolveNativeRendererPreset, before
//   SDK setup); changing edf_native_renderer later needs a restart. Individual
//   cvars keep being read live.
// Enumerators are the cvar names without the edf_native_ prefix, so
// EDF_NATIVE_FLAG(name) reads REXCVAR_GET(edf_native_##name).
enum class NativeRendererFlag : uint32_t {
  host,shader_bridge,seam_draws,material_activation,scene_queued,scene_preload,
  scene_sources_owned,scene_membership_owned,scene_selection_owned,scene_camera_owned,
  scene_geometry_owned,scene_material_owned,scene_tree,scene_tree_published,
  scene_visibility,frame_dispatch,scene_group_order,static_world_pass,
  model_publication,model_pass,post_finish,count
};
inline constexpr uint32_t kNativeRendererFlagCount=uint32_t(NativeRendererFlag::count);
inline constexpr std::array<std::string_view,kNativeRendererFlagCount> kNativeRendererFlagNames{
  "host","shader_bridge","seam_draws","material_activation","scene_queued","scene_preload",
  "scene_sources_owned","scene_membership_owned","scene_selection_owned","scene_camera_owned",
  "scene_geometry_owned","scene_material_owned","scene_tree","scene_tree_published",
  "scene_visibility","frame_dispatch","scene_group_order","static_world_pass",
  "model_publication","model_pass","post_finish"};
enum class NativeRendererPreset : uint32_t { off,world,full };

constexpr uint32_t NativeRendererFlagBit(NativeRendererFlag flag) { return 1u<<uint32_t(flag); }
// Every flag up to and including static_world_pass, then the model and post passes.
constexpr uint32_t NativeRendererPresetMask(NativeRendererPreset preset) {
  constexpr uint32_t world=(NativeRendererFlagBit(NativeRendererFlag::static_world_pass)<<1)-1;
  constexpr uint32_t full=world|NativeRendererFlagBit(NativeRendererFlag::model_publication)|
    NativeRendererFlagBit(NativeRendererFlag::model_pass)|NativeRendererFlagBit(NativeRendererFlag::post_finish);
  return preset==NativeRendererPreset::full?full:preset==NativeRendererPreset::world?world:0u;
}
// Exact lowercase names; anything else is refused rather than read as "off".
constexpr std::optional<NativeRendererPreset> ParseNativeRendererPreset(std::string_view name) {
  if(name=="off" || name.empty()) return NativeRendererPreset::off;
  if(name=="world") return NativeRendererPreset::world;
  if(name=="full") return NativeRendererPreset::full;
  return std::nullopt;
}

// Preset bits resolved at startup; zero until then.
inline std::atomic<uint32_t> native_renderer_preset_mask{0};
inline bool NativeFlag(NativeRendererFlag flag,bool individual) {
  return individual || (native_renderer_preset_mask.load(std::memory_order_relaxed)&NativeRendererFlagBit(flag));
}
// Parses edf_native_renderer, stores its mask and logs the effective
// configuration in one line; throws on an unknown preset name.
void ResolveNativeRendererPreset();
}

#define EDF_NATIVE_FLAG(name) \
  ::edf::native::NativeFlag(::edf::native::NativeRendererFlag::name,REXCVAR_GET(edf_native_##name))
