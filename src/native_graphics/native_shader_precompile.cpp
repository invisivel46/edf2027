#include "native_shader_precompile.h"
#include "d3d11_effect.h"
#include "native_disk_cache.h"
#include "native_first_use.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <format>

namespace edf::native {
namespace {
constexpr char kStampMagic[8]={'E','D','F','P','R','E','C','1'};
constexpr size_t kSourceLimit=16u<<20;

double MillisecondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
}  // namespace

std::filesystem::path GuestEffectSourcePath(const std::filesystem::path& game_root) {
  return game_root/"Shader"/"guest.fx";
}

void PrepareGuestShaderEntry(const Effect& effect,const ShaderEntry& entry,const std::filesystem::path& source_path) {
  // Mirrors the worker body of RegisterShaders; a variant missing here is a
  // registration-time compile on a cold cache, never a wrong shader.
  auto shader=CompileNativeShader(nullptr,effect,entry,source_path);
  if(entry.pixel) return;
  AddNativeWorldInstancing(shader,effect,source_path);
  auto reversed=CompileNativeShader(nullptr,effect,entry,source_path,true);
  AddNativeWorldInstancing(reversed,effect,source_path,true);
}

NativeShaderPrecompiler::NativeShaderPrecompiler() {
  // The shader cache is a function-local static; touching it now constructs
  // it before this object, so it is destroyed after this object's workers
  // are joined.
  (void)GetNativeShaderCacheStatistics();
}
NativeShaderPrecompiler::~NativeShaderPrecompiler() { Stop(); }

void NativeShaderPrecompiler::Start(NativeShaderPrecompileOptions options) {
  std::lock_guard lock(mutex_);
  if(started_once_) return;
  started_once_=true;
  options_=std::move(options);
  options_.threads=std::clamp<uint32_t>(options_.threads,1,16);
  status_.state=NativeShaderPrecompileStatus::State::Running;
  started_=std::chrono::steady_clock::now();
  compiles_at_start_=GetNativeShaderCacheStatistics().compiles;
  try {
    threads_.emplace_back([this] { Plan(); Work(); });
    for(uint32_t index=1;index<options_.threads;++index) threads_.emplace_back([this] { Work(); });
  } catch(const std::exception& error) {
    // Threads that did start see no jobs and leave.
    status_.state=NativeShaderPrecompileStatus::State::Stopped;
    status_.reason=std::string("cannot start: ")+error.what();
    ended_flag_=true;
    stopping_=true;
    ended_.notify_all();
  }
}

void NativeShaderPrecompiler::Plan() {
  SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
  NativeFirstUseLog::background_thread=true;
  std::string failure;
  bool skip=false;
  try {
    if(options_.cache_directory.empty()) throw std::runtime_error("no persistent cache");
    const auto shader_dir=options_.game_root/"Shader";
    std::vector<std::filesystem::path> paths;
    for(const auto& file:std::filesystem::directory_iterator(shader_dir))
      if(file.is_regular_file() && file.path().extension()==".dxsl") paths.push_back(file.path());
    std::sort(paths.begin(),paths.end());
    if(paths.empty()) throw std::runtime_error("no .dxsl effects in "+shader_dir.string());
    source_path_=GuestEffectSourcePath(options_.game_root);
    // The stamp covers everything that decides what the pass compiles and
    // under which keys. Raw file bytes, not parsed effects: reading them is
    // the whole cost of a warm check.
    NativeSha256 hash;
    hash.AddField(std::string_view("edf-shader-precompile-v1"));
    hash.AddField(std::string_view(options_.build_identity));
    hash.AddField(std::string_view(NativeShaderCompilerIdentity()));
    hash.AddField(std::string_view(source_path_.string()));
    std::vector<std::vector<uint8_t>> sources;
    for(auto path:paths) {
      auto bytes=NativeReadCacheFile(path,kSourceLimit);
      if(!bytes) throw std::runtime_error("cannot read "+path.string());
      hash.AddField(std::string_view(path.filename().string()));
      hash.AddField(std::span<const uint8_t>(*bytes));
      sources.push_back(std::move(*bytes));
    }
    if(const auto common=NativeReadCacheFile(shader_dir/"Common.fx",kSourceLimit))
      hash.AddField(std::span<const uint8_t>(*common));
    const auto digest=hash.Finish();
    NativeCacheWriter writer;
    writer.Bytes(kStampMagic,sizeof(kStampMagic));
    writer.Hash(digest);
    stamp_=std::move(writer.bytes());
    stamp_path_=options_.cache_directory/"shaders"/"precompile.stamp";
    if(!options_.force)
      if(const auto previous=NativeReadCacheFile(stamp_path_,4096);previous && *previous==stamp_) skip=true;
    if(!skip) {
      std::vector<Effect> effects;
      for(size_t index=0;index<paths.size();++index) {
        // An effect that does not parse is left to its registration, which
        // reports it in context.
        try { effects.push_back(ParseEffect(DecodeSourceAsset(sources[index]))); } catch(...) {}
      }
      std::lock_guard lock(mutex_);
      effects_=std::move(effects);
      for(size_t effect=0;effect<effects_.size();++effect)
        for(size_t entry=0;entry<effects_[effect].entries.size();++entry) jobs_.push_back({effect,entry});
      status_.effects=uint32_t(effects_.size());
      status_.jobs=uint32_t(jobs_.size());
    }
  } catch(const std::exception& error) {
    failure=error.what();
  }
  if(!failure.empty()) End(NativeShaderPrecompileStatus::State::Skipped,failure);
  else if(skip) End(NativeShaderPrecompileStatus::State::Skipped,"warm: the cache holds this build's pass");
  else {
    std::lock_guard lock(mutex_);
    planned_=true;
    planned_cv_.notify_all();
  }
}

void NativeShaderPrecompiler::Work() {
  SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
  NativeFirstUseLog::background_thread=true;
  std::unique_lock lock(mutex_);
  planned_cv_.wait(lock,[&] { return planned_ || ended_flag_; });
  for(;;) {
    if(ended_flag_ || next_>=jobs_.size() || stopping_) break;
    const auto job=jobs_[next_++];
    ++active_;
    lock.unlock();
    std::string failure;
    try {
      PrepareGuestShaderEntry(effects_[job.effect],effects_[job.effect].entries[job.entry],source_path_);
    } catch(const std::exception& error) {
      failure=effects_[job.effect].entries[job.entry].name+": "+error.what();
    }
    lock.lock();
    --active_;
    ++status_.done;
    if(!failure.empty()) {
      ++status_.failed;
      if(status_.reason.empty()) status_.reason=failure;
    }
  }
  // The last worker out ends the pass.
  if(ended_flag_ || finishing_ || active_) return;
  finishing_=true;
  const bool complete=status_.done==jobs_.size();
  lock.unlock();
  if(complete) {
    // Written only for a whole pass: a stopped one must run again next time.
    NativeWriteCacheFile(stamp_path_,stamp_);
    End(NativeShaderPrecompileStatus::State::Finished,{});
  } else {
    End(NativeShaderPrecompileStatus::State::Stopped,"stopped");
  }
}

void NativeShaderPrecompiler::End(NativeShaderPrecompileStatus::State state,std::string reason) {
  NativeShaderPrecompileStatus snapshot;
  {
    std::lock_guard lock(mutex_);
    if(ended_flag_) return;
    ended_flag_=true;
    status_.state=state;
    if(!reason.empty() && status_.reason.empty()) status_.reason=std::move(reason);
    status_.elapsed_ms=MillisecondsSince(started_);
    status_.compiles=GetNativeShaderCacheStatistics().compiles-compiles_at_start_;
    snapshot=status_;
    planned_cv_.notify_all();
    ended_.notify_all();
  }
  if(NativeFirstUseLog::Get().enabled())
    NativeFirstUseLog::Get().Record(NativeFirstUseKind::Precompile,snapshot.elapsed_ms,snapshot.state_name(),
      std::format("effects={} entries={} done={} failed={} compiles={} {}",snapshot.effects,snapshot.jobs,
                  snapshot.done,snapshot.failed,snapshot.compiles,snapshot.reason));
  // Not while stopping: that is process exit, when the logger may be gone.
  if(options_.finished && !stopping_) {
    try { options_.finished(snapshot); } catch(...) {}
  }
}

NativeShaderPrecompileStatus NativeShaderPrecompiler::status() const {
  std::lock_guard lock(mutex_);
  auto out=status_;
  if(out.state==NativeShaderPrecompileStatus::State::Running) {
    out.elapsed_ms=MillisecondsSince(started_);
    out.compiles=GetNativeShaderCacheStatistics().compiles-compiles_at_start_;
  }
  return out;
}

void NativeShaderPrecompiler::Wait() {
  std::unique_lock lock(mutex_);
  ended_.wait(lock,[&] { return ended_flag_ || !started_once_; });
}

void NativeShaderPrecompiler::Stop() {
  stopping_=true;
  std::vector<std::thread> threads;
  {
    std::lock_guard lock(mutex_);
    planned_cv_.notify_all();
    threads.swap(threads_);
  }
  for(auto& thread:threads) if(thread.joinable()) thread.join();
  // A pass that was still planning or running when asked to stop.
  bool started=false;
  {
    std::lock_guard lock(mutex_);
    started=started_once_;
  }
  if(started) End(NativeShaderPrecompileStatus::State::Stopped,"stopped");
}

namespace {
NativeShaderPrecompiler& Process() { static NativeShaderPrecompiler precompiler; return precompiler; }
}  // namespace
void StartNativeShaderPrecompile(NativeShaderPrecompileOptions options) { Process().Start(std::move(options)); }
NativeShaderPrecompileStatus GetNativeShaderPrecompileStatus() { return Process().status(); }
void StopNativeShaderPrecompile() { Process().Stop(); }
}  // namespace edf::native
