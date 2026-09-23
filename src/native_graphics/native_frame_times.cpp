#include "native_frame_times.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace edf::native {
size_t NativeFrameTimeBucket(double ms) {
  if(!(ms>0)) return 0;
  if(ms<50) return size_t(ms/0.25);
  if(ms<100) return 200+size_t(ms-50);
  if(ms<1000) return 250+size_t((ms-100)/10);
  return kNativeFrameTimeBuckets-1;
}
double NativeFrameTimeBucketLower(size_t bucket) {
  if(bucket<200) return double(bucket)*0.25;
  if(bucket<250) return 50+double(bucket-200);
  if(bucket<340) return 100+double(bucket-250)*10;
  return 1000;
}
std::string FormatNativeFrameTimeHistogram(std::span<const uint32_t> histogram) {
  std::string out;
  char item[48];
  for(size_t bucket=0;bucket<histogram.size();++bucket) {
    if(!histogram[bucket]) continue;
    std::snprintf(item,sizeof(item),"%s%.2f:%u",out.empty()?"":",",NativeFrameTimeBucketLower(bucket),
                  histogram[bucket]);
    out+=item;
  }
  return out.empty()?"none":out;
}

NativeFrameTimeRecorder::NativeFrameTimeRecorder(Options options) : options_(options) {
  if(!options_.report_frames || !options_.median_window || options_.median_minimum>options_.median_window)
    throw std::invalid_argument("frame time recorder needs a report period and a median window");
  recent_.reserve(options_.median_window);
  scratch_.reserve(options_.median_window);
  window_.reserve(options_.report_frames);
  histogram_.assign(kNativeFrameTimeBuckets,0);
}

NativeFrameTimeRecorder::Sample NativeFrameTimeRecorder::Record(double ms) {
  if(!std::isfinite(ms) || !(ms>=0)) ms=0;
  Sample sample;
  sample.frame=++frames_;
  sample.ms=ms;
  if(recent_.size()>=options_.median_minimum && !recent_.empty()) {
    scratch_.assign(recent_.begin(),recent_.end());
    const auto middle=scratch_.begin()+ptrdiff_t(scratch_.size()/2);
    std::nth_element(scratch_.begin(),middle,scratch_.end());
    sample.median_ms=*middle;
    sample.over_median=ms>options_.spike_ratio*sample.median_ms;
  }
  sample.over_limit=ms>options_.spike_ms;
  if(recent_.size()<options_.median_window) recent_.push_back(ms);
  else { recent_[recent_next_]=ms; recent_next_=(recent_next_+1)%options_.median_window; }
  window_.push_back(ms);
  window_ms_+=ms;
  ++histogram_[NativeFrameTimeBucket(ms)];
  if(sample.spike()) ++window_spikes_;
  return sample;
}

bool NativeFrameTimeRecorder::TakeReport(NativeFrameTimeWindow& out,bool force) {
  if(window_.empty()) return false;
  if(!force && window_.size()<options_.report_frames && window_ms_<options_.report_ms) return false;
  std::sort(window_.begin(),window_.end());
  // Nearest rank: the smallest sample with at least p of the window at or below it.
  const auto rank=[&](double p) {
    const auto index=size_t(std::ceil(p*double(window_.size())));
    return window_[std::clamp<size_t>(index,1,window_.size())-1];
  };
  out.frames=window_.size();
  out.spikes=window_spikes_;
  out.span_ms=window_ms_;
  out.mean_ms=window_ms_/double(window_.size());
  out.p50_ms=rank(0.50); out.p90_ms=rank(0.90); out.p99_ms=rank(0.99); out.p999_ms=rank(0.999);
  out.max_ms=window_.back();
  out.histogram=histogram_;
  window_.clear();
  window_ms_=0;
  window_spikes_=0;
  std::fill(histogram_.begin(),histogram_.end(),0u);
  return true;
}

void NativeFrameCounterDeltas::Update(std::span<const NativeFrameCounter> counters) {
  const bool first=previous_.size()!=counters.size();
  if(first) { previous_.assign(counters.size(),0); names_.assign(counters.size(),nullptr); }
  deltas_.assign(counters.size(),0);
  for(size_t index=0;index<counters.size();++index) {
    const auto value=counters[index].value;
    names_[index]=counters[index].name;
    if(!first && value>=previous_[index]) deltas_[index]=value-previous_[index];
    previous_[index]=value;
  }
}
std::string NativeFrameCounterDeltas::Describe() const {
  std::string out;
  for(size_t index=0;index<deltas_.size();++index) {
    if(!out.empty()) out+=' ';
    out+=names_[index];
    out+='=';
    out+=std::to_string(deltas_[index]);
  }
  return out;
}
}  // namespace edf::native
