// EDF2027 - native keyboard and mouse input: the window side.
//
// Takes the place of the SDK's pad-emulation driver (rex::input::mnk, switched off in
// launcher_cvars.cpp). It reports a synthetic device whose pad state is always neutral, for
// two reasons: a synthetic device is merged with real pads on user 0, so the game sees a
// connected pad - and therefore runs its pad poll, where native_kbm.cpp merges the keys -
// whether or not a controller is plugged in; and a neutral state contributes nothing of
// its own, so no key or mouse motion ever travels as pad data.
//
// Keys, mouse buttons and relative mouse motion go to edf::kbm (native_kbm.h). Mouse
// capture follows the SDK driver's pattern: requested from the guest thread, applied on
// the UI thread, released on focus loss and whenever a dialog owns the input.
#pragma once

#include <atomic>
#include <cstdlib>
#include <mutex>

#include <rex/input/input_driver.h>
#include <rex/logging.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

#include "native_kbm.h"
#include "pause_menu.h"

class NativeKbmDriver final : public rex::input::InputDriver,
                              public rex::ui::WindowInputListener,
                              public rex::ui::WindowListener {
 public:
  NativeKbmDriver() : InputDriver(nullptr, 0) {}
  ~NativeKbmDriver() override { DetachFromWindow(); }

  rex::X_STATUS Setup() override { return 0; }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    if (!edf::kbm::Enabled()) return;
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "Keyboard and mouse (native)";
    info.guid = "edf-native-kbm";
    info.synthetic = true;
    out.push_back(info);
  }

  rex::X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (!edf::kbm::Enabled() || id != kDevice) return kNotConnected;
    // The per-poll call from the guest thread, so it also drives ownership and capture.
    const bool game_owns_input =
        is_active() && has_focus_.load(std::memory_order_relaxed) && !edf::menu::MenuOpen();
    edf::kbm::SetGameOwnsInput(game_owns_input);
    QueueMouseCaptureUpdate(game_owns_input && edf::kbm::MouseLookWanted());
    if (out) *out = {};
    return kSuccess;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t, rex::input::X_INPUT_CAPABILITIES* caps) override {
    if (!edf::kbm::Enabled() || id != kDevice) return kNotConnected;
    if (caps) { *caps = {}; caps->type = 1; caps->sub_type = 1; caps->gamepad.buttons = 0xFFFF; }
    return 0;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId, rex::input::X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId, uint32_t, rex::input::X_INPUT_KEYSTROKE*) override {
    return kEmpty;
  }

  // UI thread: the F1 menu opened. The capture is normally withdrawn by the next guest
  // poll, but the menu pauses the game and the polls stop with it, so drop it here: free
  // the cursor and forget whatever was held. The first poll after the menu closes asks
  // for the capture again.
  static void ReleaseForMenu() {
    NativeKbmDriver* driver = instance_;
    if (!driver || !driver->attached_window_) return;
    driver->mouse_capture_requested_.store(false, std::memory_order_relaxed);
    driver->ReleaseMouseCaptureFromUIThread(driver->attached_window_);
    edf::kbm::SetGameOwnsInput(false);
  }

  void OnWindowAvailable(rex::ui::Window* window) override {
    if (!window) return;
    {
      std::lock_guard lock(mutex_);
      attached_window_ = window;
    }
    instance_ = this;
    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
  }

  // WindowInputListener. Releases are always taken so a key cannot stick down across a
  // dialog or a focus change.
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (edf::kbm::Enabled() && has_focus_) edf::kbm::SetKey(static_cast<uint16_t>(e.virtual_key()), true);
  }
  void OnKeyUp(rex::ui::KeyEvent& e) override { edf::kbm::SetKey(static_cast<uint16_t>(e.virtual_key()), false); }
  void OnMouseDown(rex::ui::MouseEvent& e) override {
    if (edf::kbm::Enabled() && has_focus_) edf::kbm::SetMouseButton(ButtonIndex(e), true);
  }
  void OnMouseUp(rex::ui::MouseEvent& e) override { edf::kbm::SetMouseButton(ButtonIndex(e), false); }
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    if (!edf::kbm::Enabled() || !has_focus_) return;
    const int32_t x = e.x(), y = e.y();
    // Only captured motion aims; a free cursor crossing the window must not turn the view.
    if (mouse_captured_) {
      if (relative_mouse_mode_) edf::kbm::AddMouseDelta(e.dx(), e.dy());
      else edf::kbm::AddMouseDelta(float(x - prev_mouse_x_), float(y - prev_mouse_y_));
    }
    prev_mouse_x_ = x;
    prev_mouse_y_ = y;
    if (mouse_captured_ && !relative_mouse_mode_) RecenterCursorFromUIThread(x, y);
  }

  // WindowListener
  void OnClosing(rex::ui::UIEvent&) override { DetachFromWindow(); }
  void OnLostFocus(rex::ui::UISetupEvent&) override {
    has_focus_ = false;
    // Withdraw the request too, or an update queued before the focus loss grabs the cursor
    // straight back.
    mouse_capture_requested_.store(false, std::memory_order_relaxed);
    edf::kbm::ClearInput();
    if (attached_window_) ReleaseMouseCaptureFromUIThread(attached_window_);
  }
  void OnGotFocus(rex::ui::UISetupEvent&) override { has_focus_ = true; }

 private:
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x4B424D4E);
  // The SDK's X_ERROR_* macros only expand inside namespace rex; these are their values.
  static constexpr rex::X_RESULT kSuccess = 0, kNotConnected = 0x48F, kEmpty = 0x10D2;

  static int ButtonIndex(const rex::ui::MouseEvent& e) {
    switch (e.button()) {
      case rex::ui::MouseEvent::Button::kLeft: return 0;
      case rex::ui::MouseEvent::Button::kRight: return 1;
      case rex::ui::MouseEvent::Button::kMiddle: return 2;
      default: return -1;
    }
  }

  // Guest thread. The rest of the capture path stays on the UI thread, since every Window
  // call in it reaches SDL.
  void QueueMouseCaptureUpdate(bool should_capture) {
    mouse_capture_requested_.store(should_capture, std::memory_order_relaxed);
    bool already_queued = false;
    if (!mouse_capture_update_queued_.compare_exchange_strong(already_queued, true)) return;
    std::lock_guard lock(mutex_);
    if (!attached_window_) {
      mouse_capture_update_queued_.store(false, std::memory_order_relaxed);
      return;
    }
    // Deferred, not CallInUIThread: running inline would re-enter mutex_.
    attached_window_->app_context().CallInUIThreadDeferred([this] {
      mouse_capture_update_queued_.store(false, std::memory_order_relaxed);
      ApplyMouseCaptureFromUIThread();
    });
  }
  void ApplyMouseCaptureFromUIThread() {
    rex::ui::Window* window = attached_window_;
    if (!window) return;
    const bool should_capture = mouse_capture_requested_.load(std::memory_order_relaxed);
    if (should_capture == mouse_captured_) return;
    if (!should_capture) {
      ReleaseMouseCaptureFromUIThread(window);
      return;
    }
    mouse_captured_ = true;
    precapture_cursor_visibility_ = window->GetCursorVisibility();
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
    window->CaptureMouse();
    relative_mouse_mode_ = window->SetRelativeMouseMode(true);
    if (!relative_mouse_mode_)
      REXLOG_WARN("Native K/M: pointer lock unavailable, mouse aim falls back to recentering the cursor");
  }
  void ReleaseMouseCaptureFromUIThread(rex::ui::Window* window) {
    if (!mouse_captured_) return;
    mouse_captured_ = false;
    window->SetCursorVisibility(precapture_cursor_visibility_);
    window->SetRelativeMouseMode(false);
    relative_mouse_mode_ = false;
    window->ReleaseMouse();
  }
  void RecenterCursorFromUIThread(int32_t x, int32_t y) {
    rex::ui::Window* window = attached_window_;
    if (!window) return;
    const int32_t width = int32_t(window->GetActualPhysicalWidth());
    const int32_t height = int32_t(window->GetActualPhysicalHeight());
    if (width <= 0 || height <= 0) return;
    // Only once the pointer has drifted well off the middle, to keep warps rare.
    if (std::abs(x - width / 2) < width / 4 && std::abs(y - height / 2) < height / 4) return;
    int32_t center_x = 0, center_y = 0;
    if (window->WarpMouseToCenter(center_x, center_y)) {
      prev_mouse_x_ = center_x;
      prev_mouse_y_ = center_y;
    }
  }
  // Safe to call from any thread.
  void DetachFromWindow() {
    rex::ui::Window* window = attached_window_;
    if (!window) return;
    window->app_context().CallInUIThreadSynchronous([this, window] {
      // Detach first so nothing new is queued, then run out what already was.
      {
        std::lock_guard lock(mutex_);
        attached_window_ = nullptr;
      }
      if (instance_ == this) instance_ = nullptr;
      if (mouse_capture_update_queued_.load(std::memory_order_relaxed))
        window->app_context().ExecutePendingFunctionsFromUIThread();
      ReleaseMouseCaptureFromUIThread(window);
      window->RemoveInputListener(this);
      window->RemoveListener(this);
    });
  }

  // The driver attached to the window, for ReleaseForMenu. UI thread only.
  static inline NativeKbmDriver* instance_ = nullptr;
  // Only the UI thread writes it, so only guest-thread access needs the lock.
  rex::ui::Window* attached_window_ = nullptr;
  std::mutex mutex_;
  // UI thread only.
  int32_t prev_mouse_x_ = 0, prev_mouse_y_ = 0;
  bool mouse_captured_ = false;
  bool relative_mouse_mode_ = false;
  rex::ui::Window::CursorVisibility precapture_cursor_visibility_ = rex::ui::Window::CursorVisibility::kVisible;
  // Guest thread to UI thread. The queued flag coalesces the posts.
  std::atomic<bool> mouse_capture_requested_{false};
  std::atomic<bool> mouse_capture_update_queued_{false};
  std::atomic<bool> has_focus_{true};
};
