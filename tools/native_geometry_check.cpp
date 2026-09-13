// Construct a native mesh for every captured vertex declaration against every
// vertex shader the disc can supply, without booting the game.
//
// Playing only ever proves that the content someone walked past is handled. The
// shader half of this is a closed world - the disc's DXSL effects are all there
// is - but the declaration half is not: a scan of all 331 .sgo/.dxm assets finds
// none of the runtime's vertex type words anywhere, so the guest synthesises the
// 12-byte elements at load and no static scan can recover them. A declaration
// therefore has to be learned once by running the game with
// --edf_native_contract_export, after which it is replayable here forever, in
// CI, needing no display and no GPU clocks.
//
// That makes the coverage statement precise: every captured declaration is
// bindable by some disc vertex shader, or it is named here as a gap. It is a
// lower bound on the content's real declaration set, never a false pass.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "native_graphics/d3d11_mesh.h"
#include "native_graphics/d3d11_effect.h"
#include "native_graphics/effect.h"
#include "native_graphics/native_contract_ledger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

uint32_t BigWord(std::span<const uint8_t> bytes, size_t at) {
  return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
         (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
}

std::vector<uint8_t> ParseHex(const std::string& text) {
  if (text.size() % 2) throw std::runtime_error("odd-length declaration payload");
  std::vector<uint8_t> bytes(text.size() / 2);
  for (size_t i = 0; i < bytes.size(); ++i) {
    unsigned value = 0;
    for (size_t nibble = 0; nibble < 2; ++nibble) {
      const char c = text[i * 2 + nibble];
      const unsigned digit = c >= '0' && c <= '9'   ? unsigned(c - '0')
                             : c >= 'a' && c <= 'f' ? unsigned(c - 'a') + 10
                             : c >= 'A' && c <= 'F' ? unsigned(c - 'A') + 10
                                                    : 16u;
      if (digit > 15) throw std::runtime_error("non-hex declaration payload");
      value = value * 16 + digit;
    }
    bytes[i] = uint8_t(value);
  }
  return bytes;
}

// The stride a declaration was drawn with belongs to the draw, not the layout,
// so the catalog's contract lines carry it. Where a capture has none, fall back
// to the minimum stride the elements themselves imply.
uint32_t MinimumStride(std::span<const uint8_t> declaration) {
  uint32_t end = 0;
  for (size_t at = 0; at < declaration.size(); at += 12) {
    const auto offset = BigWord(declaration, at) & 65535;
    uint32_t bytes = 4;
    switch (BigWord(declaration, at + 4)) {
      case 0x2c83a4: bytes = 4; break;
      case 0x2c23a5: bytes = 8; break;
      case 0x2a23b9: bytes = 12; break;
      case 0x1a23a6: bytes = 16; break;
      default: bytes = 4; break;  // UBYTE4 and D3DCOLOR occupy four bytes.
    }
    end = (std::max)(end, offset + bytes);
  }
  return (end + 3) & ~3u;
}

std::string Describe(std::span<const uint8_t> declaration) {
  std::string text = std::to_string(declaration.size() / 12) + " elements";
  for (size_t at = 0; at < declaration.size(); at += 12) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), " [%u,%#x,u%u:%u]",
                  BigWord(declaration, at) & 65535, BigWord(declaration, at + 4),
                  declaration[at + 9], declaration[at + 10]);
    text += buffer;
  }
  return text;
}

struct Catalog {
  std::map<uint64_t, std::vector<uint8_t>> declarations;
  // Declaration identity -> the (stride, index width) pairs it was drawn with.
  std::map<uint64_t, std::set<std::pair<uint32_t, uint32_t>>> draws;
};

Catalog ReadCatalog(const fs::path& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot read contract catalog: " + path.string());
  Catalog catalog;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream fields(line);
    std::string kind;
    fields >> kind;
    if (kind == "declaration") {
      std::string hash, payload;
      fields >> hash >> payload;
      if (hash.empty() || payload.empty()) throw std::runtime_error("malformed declaration line");
      catalog.declarations[std::stoull(hash, nullptr, 16)] = ParseHex(payload);
    } else if (kind == "submitted" || kind == "rejected") {
      std::string path_name, vertex_source, pixel_source, declaration;
      uint32_t topology = 0, stride = 0, index_width = 0, elements = 0;
      fields >> path_name >> vertex_source >> pixel_source >> declaration >> topology >> stride >>
          index_width >> elements;
      if (declaration.empty()) continue;
      const auto hash = std::stoull(declaration, nullptr, 16);
      if (hash && stride) catalog.draws[hash].emplace(stride, index_width ? index_width : 2);
    }
  }
  return catalog;
}

// One triangle: enough to exercise the real declaration, stride and index path
// rather than a degenerate shortcut, and small enough to run thousands of times.
std::vector<uint8_t> SyntheticVertices(uint32_t stride) {
  return std::vector<uint8_t>(size_t(stride) * 3, 0);
}
std::vector<uint8_t> SyntheticIndices(uint32_t width) {
  std::vector<uint8_t> bytes(size_t(width) * 3, 0);
  for (uint32_t i = 0; i < 3; ++i) bytes[size_t(i + 1) * width - 1] = uint8_t(i);
  return bytes;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: edf_native_geometry_check <game directory> <contract catalog>\n"
                 "  capture a catalog by running the game with --edf_native_contract_export=<path>\n";
    return 2;
  }
  try {
    const auto catalog = ReadCatalog(argv[2]);
    if (catalog.declarations.empty()) {
      std::cout << "contract catalog holds no declarations; nothing to check\n";
      return 0;
    }
    ComPtr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &level, 1,
                                 D3D11_SDK_VERSION, &device, nullptr, nullptr)))
      throw std::runtime_error("D3D11 WARP device creation failed");

    const fs::path shaders = fs::path(argv[1]) / "Shader";
    std::vector<fs::path> effects;
    for (const auto& file : fs::directory_iterator(shaders))
      if (file.is_regular_file() && file.path().extension() == ".dxsl") effects.push_back(file.path());
    std::sort(effects.begin(), effects.end());
    if (effects.empty()) throw std::runtime_error("no DXSL effects found");

    struct VertexShader { std::string effect, entry; edf::native::NativeShader shader; };
    std::vector<VertexShader> vertex_shaders;
    size_t compile_failures = 0;
    for (const auto& path : effects) {
      const auto effect = edf::native::ParseEffect(edf::native::ReadSourceAsset(path));
      std::set<std::string> seen;
      for (const auto& entry : effect.entries) {
        if (entry.pixel || !seen.insert(entry.name).second) continue;
        try {
          vertex_shaders.push_back({path.filename().string(), entry.name,
                                    edf::native::CompileNativeShader(*device.Get(), effect, entry,
                                                                     shaders / "guest.fx")});
        } catch (const std::exception& error) {
          ++compile_failures;
          std::cerr << path.filename().string() << ':' << entry.name << ": " << error.what() << '\n';
        }
      }
    }
    std::cout << effects.size() << " effects, " << vertex_shaders.size() << " vertex entries, "
              << compile_failures << " compile failures\n"
              << catalog.declarations.size() << " captured declarations\n";

    // A declaration most entries reject is ordinary: an entry that does not
    // consume those semantics has no business binding it. A declaration *no*
    // entry accepts is a real gap, because the game drew it.
    size_t pairs = 0, constructed = 0, uncovered = 0;
    std::map<uint64_t, std::map<std::string, size_t>> reasons;
    std::map<uint64_t, size_t> accepted;
    for (const auto& [hash, declaration] : catalog.declarations) {
      auto found = catalog.draws.find(hash);
      auto draws = found != catalog.draws.end() ? found->second
                                                : std::set<std::pair<uint32_t, uint32_t>>{};
      if (draws.empty()) draws.emplace(MinimumStride(declaration), 2);
      for (const auto& [stride, index_width] : draws) {
        const auto vertices = SyntheticVertices(stride);
        const auto indices = SyntheticIndices(index_width);
        for (const auto& candidate : vertex_shaders) {
          ++pairs;
          try {
            edf::native::NativeIndexedMesh mesh(*device.Get(), candidate.shader, declaration, stride,
                                                vertices, indices, index_width);
            ++constructed;
            ++accepted[hash];
          } catch (const std::exception& error) {
            ++reasons[hash][error.what()];
          }
        }
      }
    }
    for (const auto& [hash, declaration] : catalog.declarations) {
      if (accepted[hash]) continue;
      ++uncovered;
      std::cout << "  uncovered declaration " << std::hex << hash << std::dec << ": "
                << Describe(declaration) << '\n';
      for (const auto& [reason, count] : reasons[hash])
        std::cout << "      " << count << "x " << reason << '\n';
    }
    // Show every distinct rejection even when some entry accepted the layout:
    // a reason that only ever appears here is ordinary, but a new one appearing
    // after a content or renderer change is worth reading before it becomes a
    // missing mesh.
    std::map<std::string, size_t> all_reasons;
    for (const auto& [hash, per_declaration] : reasons)
      for (const auto& [reason, count] : per_declaration) all_reasons[reason] += count;
    for (const auto& [reason, count] : all_reasons)
      std::cout << "  " << count << "x rejected: " << reason << '\n';
    std::cout << pairs << " (declaration, vertex entry) pairs, " << constructed << " constructed, "
              << uncovered << " declarations no entry can bind\n";
    return uncovered || compile_failures ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << "geometry check failed: " << error.what() << '\n';
    return 1;
  }
}
