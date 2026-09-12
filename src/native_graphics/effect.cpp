#include "effect.h"

#include <array>
#include <fstream>
#include <stdexcept>

namespace edf::native {
uint64_t EffectSourceFingerprint(std::string_view source) {
  uint64_t hash=14695981039346656037ull;
  for (unsigned char byte:source) { hash^=byte; hash*=1099511628211ull; }
  return hash;
}
namespace {
constexpr size_t kMaxBytes = 16 * 1024 * 1024;

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> bytes, bool big_endian = false)
      : bytes_(bytes), big_endian_(big_endian) {}
  void Range(size_t at, size_t count) const {
    if (at > bytes_.size() || count > bytes_.size() - at)
      throw std::runtime_error("truncated shader asset");
  }
  uint32_t Word(size_t at) const {
    Range(at, 4);
    if (big_endian_)
      return (uint32_t(bytes_[at]) << 24) | (uint32_t(bytes_[at + 1]) << 16) |
             (uint32_t(bytes_[at + 2]) << 8) | uint32_t(bytes_[at + 3]);
    return uint32_t(bytes_[at]) | (uint32_t(bytes_[at + 1]) << 8) |
           (uint32_t(bytes_[at + 2]) << 16) | (uint32_t(bytes_[at + 3]) << 24);
  }
  size_t Ref(size_t record, size_t field) const {
    const size_t offset = Word(record + field);
    Range(record, offset);
    return record + offset;
  }
  void Array(size_t at, size_t count, size_t stride) const {
    Range(at, 0);
    if (count > (bytes_.size() - at) / stride)
      throw std::runtime_error("invalid shader array extent");
  }
  std::string String(size_t at) const {
    Range(at, 1);
    size_t end = at;
    while (end < bytes_.size() && bytes_[end]) ++end;
    if (end == bytes_.size()) throw std::runtime_error("unterminated shader string");
    return std::string(reinterpret_cast<const char*>(bytes_.data() + at), end - at);
  }
 private:
  std::span<const uint8_t> bytes_;
  bool big_endian_;
};
}  // namespace

std::vector<uint8_t> DecodeSourceAsset(std::span<const uint8_t> bytes) {
  if (bytes.size() > kMaxBytes) throw std::runtime_error("shader asset exceeds size limit");
  if (bytes.size() < 4 || bytes[0] != 'S' || bytes[1] != 'G' ||
      bytes[2] != 'S' || bytes[3] != 'L') return {bytes.begin(), bytes.end()};
  Reader read(bytes);
  const size_t size = read.Word(4);
  if (size > kMaxBytes) throw std::runtime_error("decoded shader exceeds size limit");
  std::vector<uint8_t> out;
  out.reserve(size);
  std::array<uint8_t, 4096> ring{};
  size_t write = 4078, cursor = 8;
  // sub_821D5548: absolute ring offsets, zero initialization, LSB-first flags.
  while (out.size() < size) {
    read.Range(cursor, 1);
    const uint8_t flags = bytes[cursor++];
    for (unsigned bit = 0; bit < 8 && out.size() < size; ++bit) {
      if (flags & (1u << bit)) {
        read.Range(cursor, 1);
        const auto value = bytes[cursor++];
        out.push_back(value);
        ring[write] = value;
        write = (write + 1) & 4095;
      } else {
        read.Range(cursor, 2);
        const uint8_t lo = bytes[cursor++], hi = bytes[cursor++];
        const size_t offset = lo | ((hi & 0xf0u) << 4);
        const size_t length = (hi & 15u) + 3;
        if (length > size - out.size())
          throw std::runtime_error("SGSL match exceeds declared output size");
        for (size_t j = 0; j < length; ++j) {
          const auto value = ring[(offset + j) & 4095];
          out.push_back(value);
          ring[write] = value;
          write = (write + 1) & 4095;
        }
      }
    }
  }
  return out;
}

std::vector<uint8_t> ReadSourceAsset(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("cannot open " + path.string());
  const auto length = input.tellg();
  if (length < 0 || length > std::streamoff(kMaxBytes))
    throw std::runtime_error("invalid shader file size: " + path.string());
  std::vector<uint8_t> bytes(static_cast<size_t>(length));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    throw std::runtime_error("cannot read " + path.string());
  return DecodeSourceAsset(bytes);
}

static Effect ParseEffectImpl(std::span<const uint8_t> bytes, bool guest) {
  if (bytes.size() > kMaxBytes) throw std::runtime_error("effect exceeds size limit");
  Reader read(bytes, guest);
  read.Range(0, 24);
  if (read.Word(0) != 0x4c535844) throw std::runtime_error("missing DXSL signature");
  Effect result;
  result.source = read.String(read.Ref(0, 4));
  if (result.source.empty()) throw std::runtime_error("empty HLSL source");
  // sub_821B6880: little-endian, record-relative references (not field-relative).
  const auto entries = read.Word(16);
  const size_t array = read.Ref(0, 20);
  read.Array(array, entries, 12);
  for (size_t i = 0; i < entries; ++i) {
    const size_t at = array + i * 12;
    const auto stage = read.Word(at);
    if (stage > 1) throw std::runtime_error("unknown shader stage");
    result.entries.push_back({stage == 1, read.String(read.Ref(at, 4)),
                             read.String(read.Ref(at, 8))});
  }
  const auto techniques = read.Word(8);
  const size_t table = read.Ref(0, 12);
  read.Array(table, techniques, 12);
  for (size_t i = 0; i < techniques; ++i) {
    const size_t at = table + i * 12;
    EffectTechnique technique{read.String(read.Ref(at, 0)), {}};
    const auto passes = read.Word(at + 4);
    size_t pass = read.Ref(at, 8);
    read.Array(pass, passes, 28);
    for (size_t j = 0; j < passes; ++j) {
      read.Range(pass, 28);
      const auto states = read.Word(pass + 4);
      const auto state_array = read.Ref(pass, 8);
      read.Array(state_array, states, 8);
      EffectPass parsed{read.String(read.Ref(pass, 12)), read.String(read.Ref(pass, 16)),
                        read.String(read.Ref(pass, 20)), read.String(read.Ref(pass, 24)), {}};
      for (size_t k = 0; k < states; ++k)
        parsed.states.push_back({read.Word(state_array + k * 8), read.Word(state_array + k * 8 + 4)});
      // The recovered pass stride includes inline state records.
      read.Array(pass + 28, states, 8);
      pass += 28 + size_t(states) * 8;
      technique.passes.push_back(std::move(parsed));
    }
    result.techniques.push_back(std::move(technique));
  }
  for (const auto& technique : result.techniques) for (const auto& pass : technique.passes) {
    auto has_entry = [&](bool pixel, const std::string& name, const std::string& profile) {
      for (const auto& entry : result.entries)
        if (entry.pixel == pixel && entry.name == name && entry.profile == profile) return true;
      return false;
    };
    if (!has_entry(false, pass.vertex, pass.vertex_profile) ||
        !has_entry(true, pass.pixel, pass.pixel_profile))
      throw std::runtime_error("pass refers to an undeclared shader entry");
  }
  return result;
}
Effect ParseEffect(std::span<const uint8_t> bytes) { return ParseEffectImpl(bytes, false); }
Effect ParseGuestEffect(std::span<const uint8_t> bytes) { return ParseEffectImpl(bytes, true); }
}  // namespace edf::native
