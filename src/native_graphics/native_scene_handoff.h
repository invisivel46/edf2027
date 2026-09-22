#pragma once
#include "native_scene_geometry.h"
#include <optional>

namespace edf::native {
class NativeSceneMaterialHandoff {
 public:
  void Defer() {
    if(activation_ || world_) throw std::runtime_error("native material handoff already pending");
    activation_=world_=true;
  }
  bool activation_pending() const { return activation_; }
  bool pending() const { return activation_ || world_; }
  template<class Activate,class World>
  void Finish(Activate&& activate,World&& world) {
    if(activation_) { activate(); activation_=false; }
    if(world_) { world(); world_=false; }
  }
 private:
  bool activation_=false,world_=false;
};
// Compatibility work owed by a native group. Nothing installs CPU bindings
// until an unsupported draw or the group boundary needs them. A group with
// no accepted native draw must not consume draw-state dirty flags.
class NativeSceneGeometryHandoff {
 public:
  void Defer(NativeSceneGeometrySource input) {
    if(input_) throw std::runtime_error("native geometry handoff already pending");
    input_=std::move(input); installed_=drawn_=false;
  }
  const std::optional<NativeSceneGeometrySource>& pending() const { return input_; }
  void DrawAccepted() { if(input_) drawn_=true; }
  template<class Install,class Consume>
  void Finish(Install&& install,Consume&& consume) {
    Finish(install,[] {},consume);
  }
  template<class Install,class Restore,class Consume>
  void Finish(Install&& install,Restore&& restore,Consume&& consume) {
    if(!input_) return;
    if(!installed_) { install(*input_); installed_=true; }
    // Restoration is owed even without a draw. Keep the installed obligation
    // alive if restoration throws, so a retry cannot skip material/world work.
    restore();
    if(drawn_) consume();
    input_.reset(); installed_=drawn_=false;
  }
 private:
  std::optional<NativeSceneGeometrySource> input_;
  bool installed_=false,drawn_=false;
};
// Shared by the live static-group boundary and the CPU differential fixture.
// Submit retained draws before restoring guest bindings. Restore material/world
// before consuming dirty state, even when installation was completed on a retry.
// Restore is also owed on fallback before the first accepted native draw.
template<class Synchronize,class Submit,class Install,class Restore,class Consume,class Invalidate>
void FinishNativeSceneGroupHandoff(NativeSceneGeometryHandoff& geometry,
    Synchronize&& synchronize,Submit&& submit,Install&& install,Restore&& restore,
    Consume&& consume,Invalidate&& invalidate) {
  if(!geometry.pending()) return;
  synchronize();
  submit();
  geometry.Finish(install,restore,consume);
  invalidate();
}
}
