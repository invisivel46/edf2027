#include "native_graphics/native_ab_alternate.h"
#include "native_graphics/native_reuse.h"
#include "native_graphics/native_renderer_preset.h"
#include <iostream>
#include <thread>

int main() {
  using edf::native::AbSide;
  int failures = 0;
  auto check = [&](bool ok) { if (!ok) ++failures; };
  for (uint64_t frame = 0; frame < 100; ++frame) {
    check(AbSide(frame, 0, 0));
    check(AbSide(frame, 600, -1));
    check(AbSide(frame, 0, 1) == (frame % 2 == 1));
    check(AbSide(frame, -5, 1) == (frame % 2 == 1));
    check(AbSide(frame, 0, 3) == ((frame / 3) % 2 == 1));
  }
  check(!AbSide(599, 600, 1));
  check(!AbSide(600, 600, 1));
  check(AbSide(601, 600, 1));
  check(!AbSide(601, 600, 2));
  check(AbSide(602, 600, 2) && AbSide(603, 600, 2) && !AbSide(604, 600, 2));
  check(edf::native::NativeAbNativeSide());
  {
    edf::native::NativeAbSideLatch outer(false);
    check(!edf::native::NativeAbNativeSide());
    { edf::native::NativeAbSideLatch inner(true); check(edf::native::NativeAbNativeSide()); }
    check(!edf::native::NativeAbNativeSide());
  }
  check(edf::native::NativeAbNativeSide());
  // The latch is per thread: a helper call on another thread neither sees nor
  // disturbs this thread's side.
  {
    edf::native::NativeAbSideLatch guest(false);
    bool other_initial = false, other_latched = true, other_restored = false;
    std::thread other([&] {
      other_initial = edf::native::NativeAbNativeSide();
      { edf::native::NativeAbSideLatch latch(false); other_latched = edf::native::NativeAbNativeSide(); }
      other_restored = edf::native::NativeAbNativeSide();
    });
    other.join();
    check(other_initial && !other_latched && other_restored);
    check(!edf::native::NativeAbNativeSide());
    std::thread native([&] { edf::native::NativeAbSideLatch latch(true); other_latched = edf::native::NativeAbNativeSide(); });
    native.join();
    check(other_latched && !edf::native::NativeAbNativeSide());
  }
  check(edf::native::NativeAbNativeSide());
  {
    // edf_native_reuse_off_alternate=N follows AbSide: reuse off on the
    // reference side (native=0), so compare-renderer-ab-captures.py pairs the
    // frames by the same rule (--period N --start S) or the logged tags.
    using edf::native::NativeReuseOffSide; using edf::native::NativeReuseAllowed;
    for (uint64_t frame = 0; frame < 100; ++frame) {
      check(!NativeReuseOffSide(frame, 0, 0) && !NativeReuseOffSide(frame, 600, -1));
      for (int64_t period : {1, 2, 5})
        for (int64_t start : {int64_t(0), int64_t(7), int64_t(-3)})
          check(NativeReuseOffSide(frame, start, period) == !AbSide(frame, start, period));
    }
    check(NativeReuseOffSide(599, 600, 1) && NativeReuseOffSide(600, 600, 1) && !NativeReuseOffSide(601, 600, 1));
    // The predicate: the thread's latch or the process-wide switch.
    check(NativeReuseAllowed());
    {
      edf::native::NativeReuseOffLatch outer(true);
      check(!NativeReuseAllowed());
      { edf::native::NativeReuseOffLatch inner(false); check(NativeReuseAllowed()); }
      check(!NativeReuseAllowed());
      bool other = false;
      std::thread thread([&] { other = NativeReuseAllowed(); });
      thread.join();
      check(other);  // Another thread does not see this frame's latch.
    }
    check(NativeReuseAllowed());
    edf::native::native_reuse_off_all = true;
    bool other = true;
    std::thread thread([&] { other = NativeReuseAllowed(); });
    thread.join();
    check(!other && !NativeReuseAllowed());  // edf_native_reuse_off reaches every thread.
    { edf::native::NativeReuseOffLatch latch(false); check(!NativeReuseAllowed()); }
    edf::native::native_reuse_off_all = false;
    check(NativeReuseAllowed());
  }
  {
    // edf_native_renderer preset -> flag mapping.
    using edf::native::NativeRendererFlag; using edf::native::NativeRendererPreset;
    using edf::native::NativeRendererPresetMask; using edf::native::NativeRendererFlagBit;
    using edf::native::ParseNativeRendererPreset;
    check(ParseNativeRendererPreset("off") == NativeRendererPreset::off);
    check(ParseNativeRendererPreset("") == NativeRendererPreset::off);
    check(ParseNativeRendererPreset("world") == NativeRendererPreset::world);
    check(ParseNativeRendererPreset("full") == NativeRendererPreset::full);
    check(!ParseNativeRendererPreset("Full") && !ParseNativeRendererPreset("on") && !ParseNativeRendererPreset("world "));
    check(NativeRendererPresetMask(NativeRendererPreset::off) == 0);
    const uint32_t world = NativeRendererPresetMask(NativeRendererPreset::world), full = NativeRendererPresetMask(NativeRendererPreset::full);
    const NativeRendererFlag world_flags[] {
      NativeRendererFlag::host, NativeRendererFlag::shader_bridge, NativeRendererFlag::seam_draws,
      NativeRendererFlag::material_activation, NativeRendererFlag::scene_queued, NativeRendererFlag::scene_preload,
      NativeRendererFlag::scene_sources_owned, NativeRendererFlag::scene_membership_owned,
      NativeRendererFlag::scene_selection_owned, NativeRendererFlag::scene_camera_owned,
      NativeRendererFlag::scene_geometry_owned, NativeRendererFlag::scene_material_owned,
      NativeRendererFlag::scene_tree, NativeRendererFlag::scene_tree_published, NativeRendererFlag::scene_visibility,
      NativeRendererFlag::frame_dispatch, NativeRendererFlag::scene_group_order, NativeRendererFlag::static_world_pass };
    const NativeRendererFlag full_only[] { NativeRendererFlag::model_publication, NativeRendererFlag::model_pass, NativeRendererFlag::post_finish };
    uint32_t expected_world = 0, expected_full = 0;
    for (auto flag : world_flags) expected_world |= NativeRendererFlagBit(flag);
    expected_full = expected_world;
    for (auto flag : full_only) expected_full |= NativeRendererFlagBit(flag);
    check(world == expected_world && full == expected_full);
    for (auto flag : full_only) check(!(world & NativeRendererFlagBit(flag)));
    check(edf::native::kNativeRendererFlagNames.back() == "full_frame" && edf::native::kNativeRendererFlagNames[17] == "static_world_pass");
    check(ParseNativeRendererPreset("native") == NativeRendererPreset::native);
    check(NativeRendererPresetMask(NativeRendererPreset::native) == (full | NativeRendererFlagBit(NativeRendererFlag::full_frame)));
    check(!(full & NativeRendererFlagBit(NativeRendererFlag::full_frame)));
    // Individual cvars only add: the effective value is individual OR preset.
    edf::native::native_renderer_preset_mask = 0;
    check(!edf::native::NativeFlag(NativeRendererFlag::model_pass, false) && edf::native::NativeFlag(NativeRendererFlag::model_pass, true));
    edf::native::native_renderer_preset_mask = world;
    check(edf::native::NativeFlag(NativeRendererFlag::static_world_pass, false));
    check(!edf::native::NativeFlag(NativeRendererFlag::post_finish, false) && edf::native::NativeFlag(NativeRendererFlag::post_finish, true));
    edf::native::native_renderer_preset_mask = full;
    check(edf::native::NativeFlag(NativeRendererFlag::post_finish, false));
    edf::native::native_renderer_preset_mask = 0;
    // Preset x scene backend: native needs the D3D12 scene backend, and falls
    // back to off (the guest renderer) on anything else; other presets and
    // native on D3D12 pass through unchanged.
    using edf::native::ResolveNativeRendererPresetForBackend;
    for (const char* backend : {"d3d12", "d3d12-warp"}) {
      const auto native = ResolveNativeRendererPresetForBackend(NativeRendererPreset::native, backend);
      check(native.preset == NativeRendererPreset::native && !native.backend_fallback);
      check(NativeRendererPresetMask(native.preset) & NativeRendererFlagBit(NativeRendererFlag::full_frame));
    }
    for (const char* backend : {"d3d11", "d3d11-warp", "", "D3D12", "vulkan"}) {
      const auto native = ResolveNativeRendererPresetForBackend(NativeRendererPreset::native, backend);
      check(native.preset == NativeRendererPreset::off && native.backend_fallback);
      check(!(NativeRendererPresetMask(native.preset) & NativeRendererFlagBit(NativeRendererFlag::full_frame)));
      for (auto preset : {NativeRendererPreset::off, NativeRendererPreset::world, NativeRendererPreset::full}) {
        const auto other = ResolveNativeRendererPresetForBackend(preset, backend);
        check(other.preset == preset && !other.backend_fallback);
      }
    }
    for (auto preset : {NativeRendererPreset::off, NativeRendererPreset::world, NativeRendererPreset::full}) {
      const auto other = ResolveNativeRendererPresetForBackend(preset, "d3d12");
      check(other.preset == preset && !other.backend_fallback);
    }
    static_assert(ResolveNativeRendererPresetForBackend(NativeRendererPreset::native, "d3d11").preset == NativeRendererPreset::off);
  }
  if (failures) std::cerr << failures << " native A/B alternate checks failed\n";
  return failures ? 1 : 0;
}
