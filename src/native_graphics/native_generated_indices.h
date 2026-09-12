#pragma once
#include "triangle_strip.h"
#include <algorithm>
#include <map>
#include <memory>
#include <span>

namespace edf::native {
enum class NativeIndexPattern { Quads, Lines, Strip };
// Immutable indices generated exclusively by the native port, not guest memory.
class NativeGeneratedIndices {
 public:
  static std::shared_ptr<const NativeGeneratedIndices> Create(NativeIndexPattern pattern,uint32_t count) {
    if(!count || count>65536 ||
       (pattern==NativeIndexPattern::Quads && count%4) ||
       (pattern==NativeIndexPattern::Lines && count%2))
      throw std::runtime_error("invalid generated index vertex count");
    std::vector<uint8_t> bytes;
    if(pattern==NativeIndexPattern::Strip) bytes=TriangleStripIndices16(count);
    else {
      bytes.reserve(size_t(count)*(pattern==NativeIndexPattern::Quads?3:2));
      auto emit=[&](uint32_t i) { bytes.push_back(uint8_t(i>>8)); bytes.push_back(uint8_t(i)); };
      if(pattern==NativeIndexPattern::Lines) for(uint32_t i=0;i<count;++i) emit(i);
      else if(pattern==NativeIndexPattern::Quads)
        for(uint32_t first=0;first<count;first+=4) for(uint32_t lane:{0u,1u,2u,0u,2u,3u}) emit(first+lane);
      else throw std::runtime_error("invalid native index pattern");
    }
    return std::shared_ptr<const NativeGeneratedIndices>(new NativeGeneratedIndices(std::move(bytes)));
  }
  std::span<const uint8_t> bytes() const { return bytes_; }
 private:
  explicit NativeGeneratedIndices(std::vector<uint8_t> bytes):bytes_(std::move(bytes)) {}
  const std::vector<uint8_t> bytes_;
};
// Bounded CPU pattern cache. GPU/cache users retain identities across eviction.
// Call under the renderer submission lock.
class NativeGeneratedIndexCache {
 public:
  explicit NativeGeneratedIndexCache(size_t limit=64,size_t budget=4*1024*1024):limit_(limit),budget_(budget) {}
  std::shared_ptr<const NativeGeneratedIndices> Get(NativeIndexPattern pattern,uint32_t count) {
    const auto key=std::pair{pattern,count}; ++tick_;
    if(auto found=entries_.find(key);found!=entries_.end()) { found->second.used=tick_; return found->second.data; }
    auto data=NativeGeneratedIndices::Create(pattern,count);
    const auto bytes=data->bytes().size();
    if(!limit_ || bytes>budget_) return data;
    while(!entries_.empty() && (entries_.size()>=limit_ || used_>budget_-bytes)) {
      auto oldest=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b) { return a.second.used<b.second.used; });
      used_-=oldest->second.data->bytes().size(); entries_.erase(oldest);
    }
    entries_.emplace(key,Entry{data,tick_}); used_+=bytes;
    return data;
  }
 private:
  struct Entry { std::shared_ptr<const NativeGeneratedIndices> data; uint64_t used; };
  std::map<std::pair<NativeIndexPattern,uint32_t>,Entry> entries_;
  size_t limit_,budget_,used_=0;
  uint64_t tick_=0;
};
}
