// EDF2027 - ISO extraction: reads the XDVDFS game partition of an Xbox 360 disc
// image directly (see xdvdfs.h) on a worker thread. No external tool involved.
#pragma once
#include <rex/logging.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "xdvdfs.h"

namespace edf {

class IsoExtractor {
 public:
  enum class State { kIdle, kListing, kExtracting, kDone, kFailed, kCancelled };

  ~IsoExtractor() { Cancel(); if (thread_.joinable()) thread_.join(); }

  void Start(std::filesystem::path iso, std::filesystem::path dest) {
    if (thread_.joinable()) thread_.join();
    iso_ = std::move(iso); dest_ = std::move(dest);
    cancel_ = false; progress_ = 0.f; done_bytes_ = 0; total_bytes_ = 0; state_ = State::kListing; error_.clear();
    thread_ = std::thread([this] { Run(); });
  }
  void Cancel() { cancel_ = true; }
  void Reset() { if (thread_.joinable()) thread_.join(); state_ = State::kIdle; }

  State state() const { return state_; }
  float progress() const { return progress_; }
  uint64_t done_bytes() const { return done_bytes_; }
  uint64_t total_bytes() const { return total_bytes_; }
  std::string error() const { std::lock_guard<std::mutex> lk(m_); return error_; }
  const std::filesystem::path& dest() const { return dest_; }

 private:
  // The dashboard update is never needed and is a sizeable part of the disc.
  static bool IsSystemUpdate(const std::string& path) {
    return path == "$SystemUpdate" || path.starts_with("$SystemUpdate/");
  }

  void Fail(std::string msg) { REXLOG_ERROR("IsoExtractor: {}", msg); { std::lock_guard<std::mutex> lk(m_); error_ = std::move(msg); } state_ = State::kFailed; }

  void Run() {
    std::error_code ec;
    const uint64_t image_size = std::filesystem::file_size(iso_, ec);
    if (ec) return Fail("cannot read " + iso_.string() + ": " + ec.message());
    std::ifstream iso(iso_, std::ios::binary);
    if (!iso) return Fail("cannot open " + iso_.string());

    auto read = [&](uint64_t offset, void* dst, size_t size) {
      if (offset > image_size || image_size - offset < size) return false;
      iso.seekg(static_cast<std::streamoff>(offset));
      return static_cast<bool>(iso.read(static_cast<char*>(dst), static_cast<std::streamsize>(size)));
    };

    // 1) locate the game partition and read its file table.
    uint64_t base = 0;
    if (!xdvdfs::FindPartition(read, &base)) {
      iso.clear();
      return Fail("not a readable Xbox 360 disc image: no XDVDFS volume descriptor in " + iso_.filename().string());
    }
    std::vector<xdvdfs::Entry> entries;
    std::string error;
    if (!xdvdfs::List(read, base, &entries, &error)) return Fail("cannot read the disc file table: " + error);

    uint64_t total = 0;
    size_t files = 0;
    for (const auto& e : entries) {
      if (e.directory || IsSystemUpdate(e.path)) continue;
      total += e.size;
      ++files;
    }
    if (files == 0) return Fail("the disc image contains no files");
    total_bytes_ = total;
    state_ = State::kExtracting;

    // 2) create the tree, then copy the files in image order.
    std::filesystem::create_directories(dest_, ec);
    for (const auto& e : entries)
      if (e.directory && !IsSystemUpdate(e.path)) std::filesystem::create_directories(dest_ / e.path, ec);

    std::vector<char> buffer(1 << 20);
    for (const auto& e : entries) {
      if (e.directory || IsSystemUpdate(e.path)) continue;
      if (cancel_) { state_ = State::kCancelled; return; }
      const std::filesystem::path out_path = dest_ / e.path;
      std::filesystem::create_directories(out_path.parent_path(), ec);
      std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
      if (!out) return Fail("cannot write " + out_path.string());
      uint64_t left = e.size, offset = e.offset;
      while (left > 0) {
        if (cancel_) { state_ = State::kCancelled; return; }
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(left, buffer.size()));
        if (!read(offset, buffer.data(), chunk)) { iso.clear(); return Fail("the disc image ends inside " + e.path); }
        if (!out.write(buffer.data(), static_cast<std::streamsize>(chunk)))
          return Fail("cannot write " + out_path.string() + " (out of disk space?)");
        offset += chunk; left -= chunk; done_bytes_ += chunk;
        progress_ = total_bytes_ ? static_cast<float>(static_cast<double>(done_bytes_) / static_cast<double>(total_bytes_)) : 0.f;
      }
      out.close();
      if (!out) return Fail("cannot write " + out_path.string() + " (out of disk space?)");
    }
    progress_ = 1.f;
    state_ = State::kDone;
  }

  std::filesystem::path iso_, dest_;
  std::thread thread_;
  std::atomic<bool> cancel_{false};
  std::atomic<State> state_{State::kIdle};
  std::atomic<float> progress_{0.f};
  std::atomic<uint64_t> done_bytes_{0}, total_bytes_{0};
  mutable std::mutex m_; std::string error_;
};

}  // namespace edf
