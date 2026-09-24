#pragma once
#include "effect.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace edf::native {
// Boot-time shader precompile (edf_native_shader_precompile).
//
// The game hands its effects to the renderer one registration at a time, as
// it loads them: at boot, on the title screen and on each mission's loading
// screen. Every entry of every registration is compiled with FXC there
// (CompileNativeShader) unless the bytecode cache already holds it, so on a
// first run - a new install, a new d3dcompiler, a cleared cache - each first
// load of an effect pays tens of milliseconds per entry, and one first
// registered mid-mission pays it in the middle of play.
//
// The effects all come from the disc's Shader/*.dxsl, so they can be compiled
// before the game asks: this reads every .dxsl in the user's game folder and
// compiles every entry in exactly the variants a registration builds
// (PrepareGuestShaderEntry), on low-priority background threads, from the
// moment the renderer has the game folder. The compiles go through the same
// cache, so they land in memory and on disk, and a registration that comes
// later is a cache hit. One that comes while a compile of the same key is
// running waits for that compile rather than repeating it (d3d11_effect.cpp).
//
// Warm runs skip it: a stamp in the shader cache records the inputs of the
// last complete pass (this build, the compiler, the game folder's shader
// files and the path they are compiled under), and when it matches nothing is
// read or compiled. A stale stamp costs only speed - the registration compiles
// what the cache lacks, exactly as it did before the precompile existed.

// The path every guest effect is compiled under. It reaches the shader cache
// key through the preprocessor's #line directives, so the precompile and the
// registration must use exactly this.
std::filesystem::path GuestEffectSourcePath(const std::filesystem::path& game_root);

// Compiles one registered entry in every variant RegisterShaders
// (edf/hooks/resources.cpp) builds for it, without a device: the plain
// shader, and for a vertex shader its world-instanced form and the
// reversed-depth pair with its instanced form. Throws what
// CompileNativeShader throws.
void PrepareGuestShaderEntry(const Effect& effect,const ShaderEntry& entry,const std::filesystem::path& source_path);

struct NativeShaderPrecompileStatus {
  enum class State { Idle, Skipped, Running, Finished, Stopped };
  State state=State::Idle;
  std::string reason;          // Why it was skipped, or the first failure.
  uint32_t effects=0,jobs=0,done=0,failed=0;
  uint64_t compiles=0;         // D3DCompile calls made process-wide while it ran.
  double elapsed_ms=0;
  bool running() const { return state==State::Running; }
  const char* state_name() const {
    switch(state) {
      case State::Idle: return "idle";
      case State::Skipped: return "skipped";
      case State::Running: return "running";
      case State::Finished: return "finished";
      case State::Stopped: return "stopped";
    }
    return "?";
  }
};

struct NativeShaderPrecompileOptions {
  std::filesystem::path game_root;
  std::filesystem::path cache_directory;  // Empty: nothing persists, so nothing is precompiled.
  std::string build_identity;             // Part of the stamp: a new build may compile differently.
  uint32_t threads=2;
  bool force=false;                       // Ignore a matching stamp.
  // Called once, on a precompile thread, when it ends (skipped, finished or
  // stopped).
  std::function<void(const NativeShaderPrecompileStatus&)> finished;
};

class NativeShaderPrecompiler {
 public:
  NativeShaderPrecompiler();
  ~NativeShaderPrecompiler();  // Stops and joins.
  NativeShaderPrecompiler(const NativeShaderPrecompiler&)=delete;
  NativeShaderPrecompiler& operator=(const NativeShaderPrecompiler&)=delete;
  // Returns at once; the stamp check, the reads and the compiles all happen
  // on the precompile threads. Once per object. Never throws for a problem
  // with the game folder or the cache: those end the precompile, which leaves
  // the game exactly as it would have been without one.
  void Start(NativeShaderPrecompileOptions options);
  NativeShaderPrecompileStatus status() const;
  void Wait();  // Until it has ended (tests, tools).
  void Stop();  // Workers finish their current entry; then joined.

 private:
  struct Job { size_t effect; size_t entry; };
  void Plan();
  void Work();
  void End(NativeShaderPrecompileStatus::State state,std::string reason);

  NativeShaderPrecompileOptions options_;
  mutable std::mutex mutex_;
  std::condition_variable ended_;
  NativeShaderPrecompileStatus status_;
  std::vector<Effect> effects_;
  std::vector<Job> jobs_;
  std::filesystem::path source_path_,stamp_path_;
  std::vector<uint8_t> stamp_;
  std::condition_variable planned_cv_;
  bool planned_=false,finishing_=false;
  size_t next_=0,active_=0;
  uint64_t compiles_at_start_=0;
  std::chrono::steady_clock::time_point started_{};
  std::atomic<bool> stopping_{false};
  bool started_once_=false,ended_flag_=false;
  std::vector<std::thread> threads_;
};

// The process-wide precompiler the game uses.
void StartNativeShaderPrecompile(NativeShaderPrecompileOptions options);
NativeShaderPrecompileStatus GetNativeShaderPrecompileStatus();
void StopNativeShaderPrecompile();
}  // namespace edf::native
