// EDF2027 - Xbox 360 disc image (XDVDFS) reader.
//
// A dumped 360 disc is a plain XDVDFS volume: a volume descriptor at sector 32
// of the game partition, and every directory stored as a binary search tree of
// fixed-header entries inside a contiguous run of sectors. Only the location of
// the partition differs between disc layouts, so the reader probes the handful
// of known base offsets.
//
// Everything here is pure logic over a byte-reader callback: no SDL, no OS calls,
// so the unit tests can drive it against an in-memory image.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace edf::xdvdfs {

inline constexpr uint32_t kSectorSize = 2048;
inline constexpr uint32_t kDescriptorSector = 32;
inline constexpr size_t kMagicLength = 20;
inline constexpr char kMagic[kMagicLength + 1] = "MICROSOFT*XBOX*MEDIA";

// Byte offset of the game partition for each disc layout a dump can have.
inline constexpr uint64_t kPartitionOffsets[] = {
    0,              // raw XDVDFS partition (original Xbox images, stripped dumps)
    0x0FD90000ULL,  // XGD2
    0x02080000ULL,  // XGD3
    0x18300000ULL,  // XGD1 full disc dump
};

inline constexpr uint8_t kAttributeDirectory = 0x10;
inline constexpr uint16_t kNoChild = 0xFFFF;
inline constexpr size_t kEntryHeaderSize = 14;
// Child links are 16-bit dword offsets, so nothing past 256 KiB is reachable.
inline constexpr uint32_t kMaxDirectoryBytes = 0xFFFF * 4;
inline constexpr size_t kMaxEntries = 200000;

// Reads exactly `size` bytes at `offset`; returns false past the end of the image.
using ReadFn = std::function<bool(uint64_t offset, void* dst, size_t size)>;

struct Entry {
  std::string path;  // relative to the disc root, '/' separated
  uint64_t offset;   // byte offset of the file data inside the image
  uint32_t size;
  bool directory;
};

inline uint16_t ReadLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t ReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Names come off the disc unvalidated; anything that could escape the destination
// folder or cannot be created on the host is dropped rather than trusted.
inline bool IsSafeName(const std::string& name) {
  if (name.empty() || name == "." || name == "..") return false;
  for (const char c : name) {
    if (static_cast<unsigned char>(c) < 0x20) return false;
    if (std::strchr("/\\:*?\"<>|", c) != nullptr) return false;
  }
  return true;
}

// Reads the volume descriptor of the partition at `base`.
inline bool ReadDescriptor(const ReadFn& read, uint64_t base, uint32_t* root_sector,
                           uint32_t* root_size) {
  uint8_t sector[kSectorSize];
  if (!read(base + static_cast<uint64_t>(kDescriptorSector) * kSectorSize, sector, kSectorSize))
    return false;
  // The magic appears at both ends of the descriptor sector.
  if (std::memcmp(sector, kMagic, kMagicLength) != 0) return false;
  if (std::memcmp(sector + kSectorSize - kMagicLength, kMagic, kMagicLength) != 0) return false;
  *root_sector = ReadLe32(sector + 0x14);
  *root_size = ReadLe32(sector + 0x18);
  return *root_sector != 0;
}

inline bool FindPartition(const ReadFn& read, uint64_t* base) {
  uint32_t root_sector = 0, root_size = 0;
  for (const uint64_t candidate : kPartitionOffsets) {
    if (ReadDescriptor(read, candidate, &root_sector, &root_size)) {
      *base = candidate;
      return true;
    }
  }
  return false;
}

// Walks every directory tree in the partition at `base`. Entries come back sorted
// by image offset, which is also the order that reads back sequentially.
inline bool List(const ReadFn& read, uint64_t base, std::vector<Entry>* out, std::string* error) {
  out->clear();
  uint32_t root_sector = 0, root_size = 0;
  if (!ReadDescriptor(read, base, &root_sector, &root_size)) {
    *error = "no XDVDFS volume descriptor";
    return false;
  }

  struct Pending {
    uint32_t sector, size;
    std::string prefix;
  };
  std::vector<Pending> queue{{root_sector, root_size, std::string()}};
  std::vector<uint8_t> table;

  for (size_t index = 0; index < queue.size(); ++index) {
    const Pending directory = queue[index];  // by value: the queue grows below
    if (directory.size == 0) continue;       // empty directory
    if (directory.size > kMaxDirectoryBytes) {
      *error = "directory table of " + (directory.prefix.empty() ? "the disc root" : directory.prefix) +
               " is implausibly large";
      return false;
    }
    table.assign(directory.size, 0);
    if (!read(base + static_cast<uint64_t>(directory.sector) * kSectorSize, table.data(), table.size())) {
      *error = "the image ends inside a directory table";
      return false;
    }

    // Depth-first over the tree, guarding against loops in a malformed image.
    std::vector<bool> visited(table.size() / 4 + 1, false);
    std::vector<uint16_t> stack{0};
    while (!stack.empty()) {
      const uint16_t node = stack.back();
      stack.pop_back();
      if (node == kNoChild || node >= visited.size() || visited[node]) continue;
      visited[node] = true;
      const size_t at = static_cast<size_t>(node) * 4;
      if (at + kEntryHeaderSize > table.size()) continue;

      const uint8_t* entry = table.data() + at;
      stack.push_back(ReadLe16(entry));
      stack.push_back(ReadLe16(entry + 2));
      const uint32_t sector = ReadLe32(entry + 4);
      const uint32_t size = ReadLe32(entry + 8);
      const uint8_t attributes = entry[12];
      const uint8_t name_length = entry[13];
      if (name_length == 0 || at + kEntryHeaderSize + name_length > table.size())
        continue;  // 0xFF padding at the end of the table

      const std::string name(reinterpret_cast<const char*>(entry) + kEntryHeaderSize, name_length);
      if (!IsSafeName(name)) continue;
      if (out->size() >= kMaxEntries) {
        *error = "the image lists more than " + std::to_string(kMaxEntries) + " files";
        return false;
      }
      const std::string path = directory.prefix.empty() ? name : directory.prefix + "/" + name;
      const bool is_directory = (attributes & kAttributeDirectory) != 0;
      out->push_back({path, base + static_cast<uint64_t>(sector) * kSectorSize, size, is_directory});
      if (is_directory) queue.push_back({sector, size, path});
    }
  }

  std::sort(out->begin(), out->end(),
            [](const Entry& a, const Entry& b) { return a.offset < b.offset; });
  return true;
}

}  // namespace edf::xdvdfs
