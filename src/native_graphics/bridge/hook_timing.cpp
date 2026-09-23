#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "hook_timing.h"
#include <cstdio>
#include <string>

namespace edf::native {
thread_local constinit uint32_t texture_loader_depth=0;
thread_local constinit EngineRegionTotals* native_engine_region=nullptr;
thread_local constinit uint32_t native_guest_wait_function=0;
std::atomic<int> native_render_helper_active{0};
thread_local constinit std::array<EngineRegionTotals,size_t(EngineRegion::Count)> native_engine_regions;
void ReportEngineRegion(EngineRegion region,EngineRegionTotals& totals) {
  static constexpr const char* kRegionNames[]{"dispatch","transition"};
  const double calls=double(totals.calls);
  // The eight costliest phases nested in the region, per region call.
  std::array<size_t,8> top{};
  size_t count=0;
  for(size_t index=0;index<totals.phase_ms.size();++index) {
    if(!totals.phase_calls[index]) continue;
    size_t at=count;
    while(at>0 && totals.phase_ms[top[at-1]]<totals.phase_ms[index]) {
      if(at<top.size()) top[at]=top[at-1];
      --at;
    }
    if(at<top.size()) { top[at]=index; count=(std::min)(count+1,top.size()); }
  }
  std::string phases,waits;
  char text[192];
  for(size_t rank=0;rank<count;++rank) {
    const auto index=top[rank];
    std::snprintf(text,sizeof(text),"%s=%.3fms(%.1fx%.2fus) ",kHookPhaseNames[index],totals.phase_ms[index]/calls,
      double(totals.phase_calls[index])/calls,1000.0*totals.phase_ms[index]/double(totals.phase_calls[index]));
    phases+=text;
  }
  for(const auto& wait:totals.waits) {
    if(!wait.calls) continue;
    std::snprintf(text,sizeof(text),"%08X=%.3fms(%.2fx) ",wait.function,wait.ms/calls,double(wait.calls)/calls);
    waits+=text;
  }
  if(totals.other_waits) {
    std::snprintf(text,sizeof(text),"other=%.3fms(%.2fx) ",totals.other_wait_ms/calls,double(totals.other_waits)/calls);
    waits+=text;
  }
  const double calibrations=double((std::max)(totals.calibrations,uint64_t(1)));
  REXLOG_INFO("Native engine region probe: region={} calls={} steps={} wall_ms/call={:.3f} wall_ms/step={:.3f} "
    "oncpu={:.1f}% offcpu_ms/call={:.3f} cores[performance/lower/unknown]={}/{}/{} migrations={} "
    "helper_running[entry/exit]={}/{} mxcsr_stale={} calibration[n={} alu_us={:.1f} memory_us={:.1f} "
    "cores={}/{}/{}] phases/call: {}waits/call: {}",
    kRegionNames[size_t(region)],totals.calls,totals.steps,totals.wall_ms/calls,
    totals.steps?totals.wall_ms/double(totals.steps):0.0,
    totals.wall_ms>0?100.0*totals.oncpu_ms/totals.wall_ms:0.0,(totals.wall_ms-totals.oncpu_ms)/calls,
    totals.cores[0],totals.cores[1],totals.cores[2],totals.migrations,totals.helper_entry,totals.helper_exit,
    totals.mxcsr_stale,totals.calibrations,totals.alu_us/calibrations,totals.memory_us/calibrations,
    totals.calibration_cores[0],totals.calibration_cores[1],totals.calibration_cores[2],phases,waits);
}
void RunEngineCalibration(EngineRegion region) {
  if(!REXCVAR_GET(edf_native_hook_timings)) return;
  static thread_local std::array<uint64_t,size_t(EngineRegion::Count)> calls{};
  if(calls[size_t(region)]++%32) return;
  static thread_local std::vector<uint32_t> chase;
  static constexpr size_t kLines=(16u<<20)/64,kStride=16;
  if(chase.empty()) {
    // One random cycle through every line: no stride a prefetcher can follow.
    std::vector<uint32_t> order(kLines);
    for(size_t index=0;index<kLines;++index) order[index]=uint32_t(index);
    uint64_t state=0x9E3779B97F4A7C15ull;
    for(size_t index=kLines-1;index>0;--index) {
      state=state*6364136223846793005ull+1442695040888963407ull;
      std::swap(order[index],order[size_t(state>>33)%(index+1)]);
    }
    chase.assign(kLines*kStride,0);
    for(size_t index=0;index<kLines;++index)
      chase[size_t(order[index])*kStride]=uint32_t(order[(index+1)%kLines]*kStride);
  }
  using Clock=std::chrono::steady_clock;
  const auto core=NativeCpuTopology::Get().Current();
  const auto start=Clock::now();
  uint64_t value=calls[size_t(region)];
  for(uint32_t index=0;index<(1u<<15);++index) value=value*0x5851F42D4C957F2Dull+0x14057B7EF767814Full;
  const auto alu=Clock::now();
  uint32_t cursor=uint32_t((value>>40)%kLines)*kStride;
  for(uint32_t hop=0;hop<2048;++hop) cursor=chase[cursor];
  const auto end=Clock::now();
  static volatile uint64_t sink=0;
  sink=value+cursor;
  auto& totals=native_engine_regions[size_t(region)];
  ++totals.calibrations; ++totals.calibration_cores[core];
  totals.alu_us+=std::chrono::duration<double,std::micro>(alu-start).count();
  totals.memory_us+=std::chrono::duration<double,std::micro>(end-alu).count();
}
void ApplyNativeThreadQos(NativeThreadRole role) {
  static thread_local bool applied=false;
  if(applied) return;
  applied=true;
  const auto mode=REXCVAR_GET(edf_native_thread_qos);
  const auto& topology=NativeCpuTopology::Get();
  static std::once_flag topology_once;
  std::call_once(topology_once,[&] {
    REXLOG_INFO("Native thread QoS: mode={} hybrid={} performance_logical={} other_logical={}",
      mode,topology.hybrid(),topology.performance_logical,topology.other_logical);
  });
  if(mode<=0) return;
  THREAD_POWER_THROTTLING_STATE state{};
  state.Version=THREAD_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask=THREAD_POWER_THROTTLING_EXECUTION_SPEED;
  state.StateMask=0;
  const bool high=SetThreadInformation(GetCurrentThread(),ThreadPowerThrottling,&state,sizeof(state))!=0;
  bool performance=false;
  if(mode>=2 && topology.hybrid() && !topology.performance_sets.empty())
    performance=SetThreadSelectedCpuSets(GetCurrentThread(),topology.performance_sets.data(),
      ULONG(topology.performance_sets.size()))!=0;
  REXLOG_INFO("Native thread QoS: role={} thread={} high_qos={} performance_cpu_sets={}",
    role==NativeThreadRole::Engine?"engine":"render_helper",GetCurrentThreadId(),high,performance);
}
}  // namespace edf::native
