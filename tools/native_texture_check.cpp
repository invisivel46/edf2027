// Validate the disc's SGSL-wrapped DDS assets directly, without booting the game.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "native_graphics/d3d11_texture.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/effect.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
  if (argc!=2) { std::cerr << "usage: edf_native_texture_check <game directory>\n"; return 2; }
  try {
    // WARP through the backend interface: this checks the disc's assets
    // against the same creation path the game uses, which is now the seam.
    edf::native::RegisterNativeD3D11Backend();
    auto backend=edf::native::CreateNativeRenderBackend("d3d11-warp");
    if(!backend) throw std::runtime_error("D3D11 WARP backend creation failed");
    std::vector<std::filesystem::path> paths;
    for (const auto& file : std::filesystem::recursive_directory_iterator(argv[1])) {
      if (!file.is_regular_file()) continue;
      auto extension=file.path().extension().string();
      std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c) { return char(std::tolower(c)); });
      if (extension==".dds") paths.push_back(file.path());
    }
    std::sort(paths.begin(),paths.end());
    if (paths.empty()) throw std::runtime_error("no DDS assets found");
    size_t failures=0;
    for (const auto& path : paths) {
      try {
        const auto bytes=edf::native::ReadSourceAsset(path);
        auto texture=edf::native::CreateNativeDdsTexture(*backend,bytes);
      } catch (const std::exception& error) {
        ++failures;
        std::cerr << path.string() << ": " << error.what() << '\n';
      }
    }
    std::cout << paths.size() << " disc DDS assets, " << failures << " failures\n";
    return failures?1:0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
