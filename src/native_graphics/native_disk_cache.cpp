#include "native_disk_cache.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>

namespace edf::native {
namespace {
void RequireStatus(NTSTATUS status,const char* what) {
  if(status>=0) return;
  throw std::runtime_error(std::string("SHA-256 ")+what+" failed");
}
std::mutex& DirectoryMutex() { static std::mutex mutex; return mutex; }
std::filesystem::path& Directory() { static std::filesystem::path directory; return directory; }
}  // namespace

NativeSha256::NativeSha256() {
  BCRYPT_HASH_HANDLE handle=nullptr;
  // The pseudo-handle needs no provider open/close and is free-threaded.
  RequireStatus(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&handle,nullptr,0,nullptr,0,0),"creation");
  hash_=handle;
}
NativeSha256::~NativeSha256() {
  if(hash_) BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hash_));
}
NativeSha256& NativeSha256::Add(const void* data,size_t bytes) {
  if(finished_) throw std::logic_error("SHA-256 used after Finish");
  const auto* at=static_cast<const uint8_t*>(data);
  while(bytes) {
    const ULONG chunk=static_cast<ULONG>((std::min)(bytes,size_t(1)<<30));
    RequireStatus(BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(hash_),const_cast<PUCHAR>(at),chunk,0),"update");
    at+=chunk; bytes-=chunk;
  }
  return *this;
}
NativeContentHash NativeSha256::Finish() {
  if(finished_) throw std::logic_error("SHA-256 finished twice");
  NativeContentHash out{};
  RequireStatus(BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(hash_),out.data(),static_cast<ULONG>(out.size()),0),"finish");
  finished_=true;
  return out;
}
NativeContentHash NativeSha256Of(std::span<const uint8_t> bytes) {
  NativeSha256 hash;
  hash.Add(bytes);
  return hash.Finish();
}
std::string NativeHashHex(const NativeContentHash& hash) {
  static constexpr char kDigits[]="0123456789abcdef";
  std::string out;
  out.reserve(hash.size()*2);
  for(const auto byte:hash) { out.push_back(kDigits[byte>>4]); out.push_back(kDigits[byte&15]); }
  return out;
}

std::optional<std::vector<uint8_t>> NativeReadCacheFile(const std::filesystem::path& path,size_t max_bytes) {
  try {
    std::ifstream file(path,std::ios::binary);
    if(!file) return std::nullopt;
    file.seekg(0,std::ios::end);
    const auto size=file.tellg();
    if(size<0 || uint64_t(size)>max_bytes) return std::nullopt;
    file.seekg(0,std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if(!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()),size)) return std::nullopt;
    return bytes;
  } catch(...) { return std::nullopt; }
}

bool NativeWriteCacheFile(const std::filesystem::path& path,std::span<const uint8_t> bytes) {
  try {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(),error);
    // Unique per process and call: two writers of one key (two shader workers
    // compiling the same entry) never share a temporary.
    static std::atomic<uint64_t> serial{0};
    auto temporary=path;
    temporary+=".tmp"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(serial.fetch_add(1));
    {
      std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
      if(!file) return false;
      if(!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
      file.flush();
      if(!file) { file.close(); std::filesystem::remove(temporary,error); return false; }
    }
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)) {
      std::filesystem::remove(temporary,error);
      return false;
    }
    return true;
  } catch(...) { return false; }
}

void SetNativeCacheDirectory(std::filesystem::path directory) {
  std::lock_guard lock(DirectoryMutex());
  Directory()=std::move(directory);
}
std::filesystem::path NativeCacheDirectory() {
  std::lock_guard lock(DirectoryMutex());
  return Directory();
}
std::filesystem::path NativeDefaultCacheDirectory() {
  std::wstring path(MAX_PATH,L'\0');
  for(;;) {
    const DWORD length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!length) return std::filesystem::current_path()/"native_cache";
    if(length<path.size()) { path.resize(length); break; }
    path.resize(path.size()*2);
  }
  return std::filesystem::path(path).parent_path()/"native_cache";
}

std::string NativeModuleIdentity(const wchar_t* module_name) {
  const HMODULE module=GetModuleHandleW(module_name);
  if(!module) return {};
  std::wstring path(MAX_PATH,L'\0');
  const DWORD length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
  if(!length || length>=path.size()) return {};
  path.resize(length);
  std::string identity;
  // VS_FIXEDFILEINFO without version.dll: the resource is in the mapped image.
  if(const HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(1),MAKEINTRESOURCEW(16))) {
    if(const HGLOBAL loaded=LoadResource(module,resource)) {
      const auto* data=static_cast<const uint8_t*>(LockResource(loaded));
      const DWORD size=SizeofResource(module,resource);
      // VS_VERSIONINFO: WORD length, WORD value length, WORD type, L"VS_VERSION_INFO", padding, then
      // VS_FIXEDFILEINFO starting with the 0xFEEF04BD signature.
      for(DWORD at=0;data && at+sizeof(VS_FIXEDFILEINFO)<=size;at+=4) {
        VS_FIXEDFILEINFO info;
        std::memcpy(&info,data+at,sizeof(info));
        if(info.dwSignature!=0xFEEF04BD) continue;
        identity=std::to_string(HIWORD(info.dwFileVersionMS))+"."+std::to_string(LOWORD(info.dwFileVersionMS))+"."+
                 std::to_string(HIWORD(info.dwFileVersionLS))+"."+std::to_string(LOWORD(info.dwFileVersionLS));
        break;
      }
    }
  }
  std::error_code error;
  const auto bytes=std::filesystem::file_size(std::filesystem::path(path),error);
  identity+="|"+std::to_string(error?0:bytes);
  return identity;
}
}  // namespace edf::native
