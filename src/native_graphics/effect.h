#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace edf::native {
// Stable asset identity for verified renderer contracts, not a security hash.
uint64_t EffectSourceFingerprint(std::string_view source);

// Host-side source assets. No guest GPU registers, microcode, or SDK types.
struct ShaderEntry {
  bool pixel;
  std::string name, profile;
};
struct EffectPass {
  struct State { uint32_t id, value; };
  std::string vertex, vertex_profile, pixel, pixel_profile;
  std::vector<State> states;
};
struct EffectTechnique {
  std::string name;
  std::vector<EffectPass> passes;
};
struct Effect {
  std::string source;
  std::vector<ShaderEntry> entries;
  std::vector<EffectTechnique> techniques;
};

// Throws std::runtime_error on malformed/truncated input. Limits allocation to
// 16 MiB per source asset; this limit is for shaders, not arbitrary game files.
std::vector<uint8_t> DecodeSourceAsset(std::span<const uint8_t> bytes);
std::vector<uint8_t> ReadSourceAsset(const std::filesystem::path& path);
Effect ParseEffect(std::span<const uint8_t> uncompressed);
// sub_821D90C8 swaps numeric DXSL fields in place before sub_821B6880
// consumes them. Strings remain byte strings. The adapter supplies the exact
// mapped allocation span; parsing does not modify or retain guest memory.
Effect ParseGuestEffect(std::span<const uint8_t> converted_guest_bytes);

}  // namespace edf::native
