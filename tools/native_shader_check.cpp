// Compiles the user's disc HLSL directly to D3D11 shaders; no ReXGlue/Xenos.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <iostream>
#include <map>
#include <tuple>
#include <stdexcept>
#include "native_graphics/d3d11_effect.h"
#include "native_graphics/d3d11_bindings.h"
#include "native_post_arithmetic.h"

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "--bindings" && std::string(argv[2]) != "--source" && std::string(argv[2]) != "--fingerprints" && std::string(argv[2]) != "--post-arithmetic")) {
    std::cerr << "usage: edf_native_shader_check <game directory> [--bindings|--source|--fingerprints|--post-arithmetic]\n";
    return 2;
  }
  try {
    ComPtr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                              levels, 1, D3D11_SDK_VERSION, &device, nullptr, nullptr);
    if (FAILED(hr)) throw std::runtime_error("D3D11 hardware device creation failed");
    const fs::path root = fs::path(argv[1]) / "Shader";
    if(argc==3 && std::string(argv[2])=="--post-arithmetic") {
      CheckRetailPostArithmetic(*device.Get(),root); return 0;
    }
    if (argc==3 && std::string(argv[2])=="--source") {
      const auto common=edf::native::ReadSourceAsset(root/"Common.fx");
      std::cout << "// Common.fx\n";
      std::cout.write(reinterpret_cast<const char*>(common.data()),common.size());
      std::cout << '\n';
    }
    std::vector<fs::path> paths;
    for (const auto& file : fs::directory_iterator(root))
      if (file.is_regular_file() && file.path().extension() == ".dxsl") paths.push_back(file.path());
    std::sort(paths.begin(), paths.end());
    if (paths.empty()) throw std::runtime_error("no DXSL effects found");
    size_t effects = 0, techniques = 0, shaders = 0, reversed_shaders = 0, linked_passes = 0, failures = 0;
    for (const auto& path : paths) {
      const auto effect = edf::native::ParseEffect(edf::native::ReadSourceAsset(path));
      if (argc==3 && std::string(argv[2])=="--fingerprints") {
        std::cout << path.filename().string() << " bytes=" << effect.source.size()
                  << " fnv1a=" << std::hex << edf::native::EffectSourceFingerprint(effect.source) << std::dec << '\n';
        continue;
      }
      if (argc==3 && std::string(argv[2])=="--source") {
        std::cout << "// " << path.filename().string() << '\n' << effect.source << '\n';
        continue;
      }
      ++effects;
      techniques += effect.techniques.size();
      using ShaderKey=std::tuple<bool,std::string,std::string>;
      std::map<ShaderKey,edf::native::NativeShader> compiled,reversed_compiled;
      for (const auto& entry : effect.entries) {
        ++shaders;
        try {
          auto shader = edf::native::CompileNativeShader(*device.Get(), effect, entry, path);
          const ShaderKey key{entry.pixel,entry.name,entry.profile};
          compiled.emplace(key,shader);
          if (!entry.pixel) {
            auto reversed=edf::native::CompileNativeShader(*device.Get(),effect,entry,path,true);
            reversed_compiled.emplace(key,reversed);
            edf::native::ShaderBindings reversed_bindings(*device.Get(),std::move(reversed));
            ++reversed_shaders;
          }
          D3D11_SHADER_DESC description{};
          if (FAILED(shader.reflection->GetDesc(&description)))
            throw std::runtime_error("cannot inspect native shader bindings");
          if (argc == 3) for (UINT i = 0; i < description.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC binding{};
            if (FAILED(shader.reflection->GetResourceBindingDesc(i, &binding)))
              throw std::runtime_error("cannot inspect native resource binding");
            std::cout << path.filename().string() << ':' << entry.name << ' ' << binding.Name
                      << " type=" << binding.Type << " slot=" << binding.BindPoint << '\n';
          }
          edf::native::ShaderBindings bindings(*device.Get(), std::move(shader));
        } catch (const std::exception& error) {
          ++failures;
          std::cerr << error.what() << '\n';
        }
      }
      for (const auto& technique : effect.techniques) for (const auto& pass : technique.passes) {
        try {
          const ShaderKey vertex{false,pass.vertex,pass.vertex_profile},pixel{true,pass.pixel,pass.pixel_profile};
          edf::native::ValidateNativeShaderLink(compiled.at(vertex),compiled.at(pixel));
          edf::native::ValidateNativeShaderLink(reversed_compiled.at(vertex),compiled.at(pixel));
          ++linked_passes;
        } catch (const std::exception& error) {
          ++failures;
          std::cerr << path.filename().string() << ':' << technique.name << " linkage: " << error.what() << '\n';
        }
      }
    }
    std::cout << effects << " effects, " << techniques << " techniques, " << shaders
              << " native shader entries, " << reversed_shaders << " reversed-depth variants, "
              << linked_passes << " linked passes (normal and reversed), " << failures << " failures\n";
    return failures ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
