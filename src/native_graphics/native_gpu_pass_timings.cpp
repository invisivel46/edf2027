#include "native_gpu_pass_timings.h"
#include <algorithm>
#include <stdexcept>

namespace edf::native {
NativeGpuPassTimings::NativeGpuPassTimings(Options options) : options_(options) {
  if(!options_.ring || options_.slots_per_frame<4 || !options_.report_frames)
    throw std::invalid_argument("GPU pass timings need a ring, four slots a frame and a report period");
  entries_.resize(options_.ring);
  frame_name_=Intern("frame");
  interval_name_=Intern("interval");
}
NativeGpuPassTimings::~NativeGpuPassTimings()=default;

uint32_t NativeGpuPassTimings::Intern(std::string_view name) {
  for(uint32_t index=0;index<names_.size();++index) if(names_[index]==name) return index;
  names_.emplace_back(name);
  window_.passes.push_back({std::string(name)});
  return uint32_t(names_.size()-1);
}

uint64_t NativeGpuPassTimings::pending() const {
  return uint64_t(std::count_if(entries_.begin(),entries_.end(),
    [](const Entry& entry) { return entry.state==Entry::State::Pending; }));
}

bool NativeGpuPassTimings::BeginFrame(NativeRenderBackend& backend,NativeBackendRecorder& recorder) {
  if(unsupported_) return false;
  if(backend_!=&backend) {
    // A new backend: nothing in flight on the old one can be read any more.
    timestamps_.reset();
    for(auto& entry:entries_) entry=Entry{};
    open_=-1; last_sequence_=UINT64_MAX;
    backend_=&backend;
    frequency_=backend.TimestampFrequency();
    timestamps_=backend.CreateTimestamps(options_.ring*options_.slots_per_frame);
    if(!timestamps_ || !frequency_) { unsupported_=true; timestamps_.reset(); return false; }
  }
  // A frame never ended (its pass threw). Its markers may be in the stream,
  // but no resolve covers them and the next user of the entry rewrites every
  // slot it resolves, so the entry is simply free again.
  if(open_>=0) { entries_[size_t(open_)]=Entry{}; open_=-1; }
  Poll(backend);
  const auto sequence=next_sequence_++;
  const auto index=size_t(sequence%options_.ring);
  auto& entry=entries_[index];
  if(entry.state!=Entry::State::Free) { ++window_.skipped; return false; }
  entry.state=Entry::State::Recording;
  entry.sequence=sequence;
  entry.used=2;
  entry.spans.clear();
  recorder.WriteTimestamp(*timestamps_,Base(index));
  open_=int(index);
  return true;
}

int NativeGpuPassTimings::BeginSpan(std::string_view name,NativeBackendRecorder& recorder) {
  if(open_<0) return -1;
  auto& entry=entries_[size_t(open_)];
  if(entry.used+2>options_.slots_per_frame) { ++window_.dropped_spans; return -1; }
  const Span span{Intern(name),entry.used,entry.used+1,false};
  entry.used+=2;
  recorder.WriteTimestamp(*timestamps_,Base(size_t(open_))+span.begin);
  entry.spans.push_back(span);
  return int(entry.spans.size()-1);
}

void NativeGpuPassTimings::EndSpan(int span,NativeBackendRecorder& recorder) {
  if(open_<0 || span<0) return;
  auto& entry=entries_[size_t(open_)];
  if(size_t(span)>=entry.spans.size() || entry.spans[size_t(span)].closed) return;
  auto& closing=entry.spans[size_t(span)];
  recorder.WriteTimestamp(*timestamps_,Base(size_t(open_))+closing.end);
  closing.closed=true;
}

void NativeGpuPassTimings::EndFrame(NativeBackendRecorder& recorder) {
  if(open_<0) return;
  const auto index=size_t(open_);
  auto& entry=entries_[index];
  const auto base=Base(index);
  // Every resolved slot must have been written this frame; an unclosed span's
  // end is written here and the span is reported invalid when read.
  for(const auto& span:entry.spans)
    if(!span.closed) recorder.WriteTimestamp(*timestamps_,base+span.end);
  recorder.WriteTimestamp(*timestamps_,base+1);
  recorder.ResolveTimestamps(*timestamps_,base,entry.used);
  entry.state=Entry::State::Pending;
  open_=-1;
}

size_t NativeGpuPassTimings::Poll(NativeRenderBackend& backend) {
  if(!timestamps_ || &backend!=backend_) return 0;
  size_t read=0;
  for(;;) {
    Entry* oldest=nullptr;
    size_t index=0;
    for(size_t candidate=0;candidate<entries_.size();++candidate) {
      auto& entry=entries_[candidate];
      if(entry.state==Entry::State::Pending && (!oldest || entry.sequence<oldest->sequence)) {
        oldest=&entry; index=candidate;
      }
    }
    if(!oldest) break;
    ticks_.assign(oldest->used,0);
    // Frames finish in submission order, so an unfinished oldest frame means
    // nothing newer is finished either.
    if(!backend.ReadTimestamps(*timestamps_,Base(index),ticks_)) break;
    Read(*oldest,ticks_);
    *oldest=Entry{};
    ++read;
  }
  return read;
}

void NativeGpuPassTimings::Read(Entry& entry,const std::vector<uint64_t>& ticks) {
  frame_sums_.assign(names_.size(),-1.0);
  const auto add=[&](uint32_t name,double ms) {
    auto& sum=frame_sums_[name];
    sum=sum<0?ms:sum+ms;
  };
  if(ticks[1]>=ticks[0]) add(frame_name_,Milliseconds(ticks[1]-ticks[0]));
  else ++window_.invalid_spans;
  for(const auto& span:entry.spans) {
    if(!span.closed || ticks[span.end]<ticks[span.begin]) { ++window_.invalid_spans; continue; }
    add(span.name,Milliseconds(ticks[span.end]-ticks[span.begin]));
  }
  if(last_sequence_!=UINT64_MAX && last_sequence_+1==entry.sequence && ticks[0]>=last_begin_)
    add(interval_name_,Milliseconds(ticks[0]-last_begin_));
  last_sequence_=entry.sequence;
  last_begin_=ticks[0];
  for(size_t name=0;name<frame_sums_.size();++name) {
    const auto ms=frame_sums_[name];
    if(ms<0) continue;
    auto& pass=window_.passes[name];
    ++pass.frames; pass.total_ms+=ms; pass.max_ms=(std::max)(pass.max_ms,ms);
  }
  ++window_.frames;
  ++frames_read_;
}

bool NativeGpuPassTimings::TakeReport(NativeGpuPassTimingWindow& out,bool force) {
  if(window_.frames<options_.report_frames && !(force && window_.frames)) return false;
  out=NativeGpuPassTimingWindow{window_.frames,window_.skipped,window_.dropped_spans,window_.invalid_spans,{}};
  for(auto& pass:window_.passes) {
    if(pass.frames) out.passes.push_back(pass);
    pass=NativeGpuPassTiming{pass.name};
  }
  window_.frames=window_.skipped=window_.dropped_spans=window_.invalid_spans=0;
  return true;
}
}  // namespace edf::native
