#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace edf::native {
// Shared plumbing for the renderer's persistent caches: compiled shader
// bytecode (d3d11_effect.cpp) and the D3D12 pipeline manifest
// (d3d12_pipeline.cpp). Both exist to move first-use work - a D3DCompile, a
// CreateGraphicsPipelineState - out of the frame that first needs it, and both
// must never hand back something that differs from what that work would have
// produced. So every key is a SHA-256 of everything the result depends on,
// never a guess at what is "probably" the same.

using NativeContentHash=std::array<uint8_t,32>;

// Incremental SHA-256 (Windows CNG). Length-prefixed helpers keep adjacent
// variable-length fields from running into each other: ("ab","c") and
// ("a","bc") hash differently.
class NativeSha256 {
 public:
  NativeSha256();
  ~NativeSha256();
  NativeSha256(const NativeSha256&)=delete;
  NativeSha256& operator=(const NativeSha256&)=delete;
  NativeSha256& Add(const void* data,size_t bytes);
  NativeSha256& Add(std::span<const uint8_t> bytes) { return Add(bytes.data(),bytes.size()); }
  NativeSha256& AddU32(uint32_t value) { return Add(&value,sizeof(value)); }
  NativeSha256& AddU64(uint64_t value) { return Add(&value,sizeof(value)); }
  NativeSha256& AddField(std::string_view text) {
    AddU64(text.size());
    return Add(text.data(),text.size());
  }
  NativeSha256& AddField(std::span<const uint8_t> bytes) { AddU64(bytes.size()); return Add(bytes); }
  NativeContentHash Finish();
 private:
  void* hash_=nullptr;
  bool finished_=false;
};
NativeContentHash NativeSha256Of(std::span<const uint8_t> bytes);
std::string NativeHashHex(const NativeContentHash& hash);

// Whole-file read, refusing anything larger than max_bytes. nullopt when the
// file is missing or unreadable: a cache that cannot be read is a cold cache,
// never an error.
std::optional<std::vector<uint8_t>> NativeReadCacheFile(const std::filesystem::path& path,size_t max_bytes);
// Writes to a unique temporary beside the target and renames it over the
// target, so a reader (or a crash mid-write) sees the old file or the new one,
// never half of either. Creates the directory. false on any failure; never
// throws, because a cache that cannot be written only costs the next run time.
bool NativeWriteCacheFile(const std::filesystem::path& path,std::span<const uint8_t> bytes);

// The process-wide cache root. Empty (the default) means no disk caching at
// all: nothing is read or written, and each cache keeps only what it built in
// this process. The game sets it once, before anything compiles
// (edf_native_cache_dir); tests set it to a scratch directory.
void SetNativeCacheDirectory(std::filesystem::path directory);
std::filesystem::path NativeCacheDirectory();
// <directory of the running executable>/native_cache: beside the binary, so
// it survives across runs but belongs to one install. When that folder cannot
// be written, %LOCALAPPDATA%\edf2027\native_cache.
std::filesystem::path NativeDefaultCacheDirectory();

// A loaded module's identity (e.g. d3dcompiler_47.dll) as
// "major.minor.build.revision|bytes", or "" when it is not loaded. A key
// ingredient: a compiler update must miss every entry the old one built.
std::string NativeModuleIdentity(const wchar_t* module_name);

// Little-endian serialization with bounds-checked reads. A read past the end
// throws NativeCacheFormatError, which a loader turns into "discard the file".
struct NativeCacheFormatError : std::runtime_error { using std::runtime_error::runtime_error; };
class NativeCacheWriter {
 public:
  void Bytes(const void* data,size_t size) {
    const auto* bytes=static_cast<const uint8_t*>(data);
    out_.insert(out_.end(),bytes,bytes+size);
  }
  void U32(uint32_t value) { Bytes(&value,sizeof(value)); }
  void U64(uint64_t value) { Bytes(&value,sizeof(value)); }
  void Hash(const NativeContentHash& hash) { Bytes(hash.data(),hash.size()); }
  void Blob(std::span<const uint8_t> bytes) { U64(bytes.size()); Bytes(bytes.data(),bytes.size()); }
  void Text(std::string_view text) { U64(text.size()); Bytes(text.data(),text.size()); }
  std::vector<uint8_t>& bytes() { return out_; }
 private:
  std::vector<uint8_t> out_;
};
class NativeCacheReader {
 public:
  explicit NativeCacheReader(std::span<const uint8_t> bytes):bytes_(bytes) {}
  void Bytes(void* data,size_t size) {
    if(size>bytes_.size()-at_) throw NativeCacheFormatError("cache file is truncated");
    std::memcpy(data,bytes_.data()+at_,size);
    at_+=size;
  }
  uint32_t U32() { uint32_t value; Bytes(&value,sizeof(value)); return value; }
  uint64_t U64() { uint64_t value; Bytes(&value,sizeof(value)); return value; }
  NativeContentHash Hash() { NativeContentHash hash; Bytes(hash.data(),hash.size()); return hash; }
  std::vector<uint8_t> Blob(size_t limit) {
    const auto size=U64();
    if(size>limit || size>bytes_.size()-at_) throw NativeCacheFormatError("cache blob is out of range");
    std::vector<uint8_t> out(bytes_.begin()+at_,bytes_.begin()+at_+size);
    at_+=size;
    return out;
  }
  std::string Text(size_t limit) {
    const auto blob=Blob(limit);
    return std::string(blob.begin(),blob.end());
  }
  size_t offset() const { return at_; }
  size_t remaining() const { return bytes_.size()-at_; }
 private:
  std::span<const uint8_t> bytes_;
  size_t at_=0;
};
}  // namespace edf::native
