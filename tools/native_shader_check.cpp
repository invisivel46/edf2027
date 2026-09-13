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
  if (argc < 2 || argc > 3 || (argc == 3 && std::string(argv[2]) != "--bindings" && std::string(argv[2]) != "--source" && std::string(argv[2]) != "--fingerprints" && std::string(argv[2]) != "--post-arithmetic" && std::string(argv[2]) != "--slots")) {
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
    // Widest slot actually used by any disc shader, per stage. A D3D12 root
    // signature is fixed at creation, so it has to be sized from this rather
    // than from the API maximum: declaring 14 constant buffers when the game
    // uses 5 costs root-signature space and descriptor traffic on every draw.
    struct Widest { UINT slot = 0; std::string where; bool seen = false; };
    const bool slots_mode = argc == 3 && std::string(argv[2]) == "--slots";
    std::map<std::string, Widest> widest;  // "vs.cb", "ps.texture", ...
    UINT widest_cb_bytes = 0, widest_inputs = 0;
    std::string widest_cb_where, widest_inputs_where;
    const auto note = [&](const std::string& key, UINT slot, const std::string& where) {
      auto& entry = widest[key];
      if (!entry.seen || slot > entry.slot) { entry.slot = slot; entry.where = where; entry.seen = true; }
    };
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
            const std::string where = path.filename().string() + ':' + entry.name;
            if (slots_mode) {
              const std::string stage = entry.pixel ? "ps" : "vs";
              const char* kind = binding.Type == D3D_SIT_CBUFFER ? "cb"
                               : binding.Type == D3D_SIT_SAMPLER ? "sampler"
                               : binding.Type == D3D_SIT_TEXTURE ? "texture"
                                                                 : nullptr;
              // Anything else needs its own root parameter, so it is reported
              // under its own key rather than folded into one of the three.
              if (kind) note(stage + '.' + kind, binding.BindPoint, where);
              else note(stage + ".other(type" + std::to_string(binding.Type) + ')', binding.BindPoint, where);
            } else {
              std::cout << where << ' ' << binding.Name
                        << " type=" << binding.Type << " slot=" << binding.BindPoint << '\n';
            }
          }
          if (slots_mode) {
            for (UINT i = 0; i < description.ConstantBuffers; ++i) {
              D3D11_SHADER_BUFFER_DESC buffer{};
              if (auto* reflected = shader.reflection->GetConstantBufferByIndex(i);
                  reflected && SUCCEEDED(reflected->GetDesc(&buffer)) && buffer.Size > widest_cb_bytes) {
                widest_cb_bytes = buffer.Size;
                widest_cb_where = path.filename().string() + ':' + entry.name + ':' + buffer.Name;
              }
            }
            if (!entry.pixel && description.InputParameters > widest_inputs) {
              widest_inputs = description.InputParameters;
              widest_inputs_where = path.filename().string() + ':' + entry.name;
            }
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
    if (slots_mode) {
      std::cout << "widest slot actually used by any disc shader (root signature must cover these):\n";
      for (const auto& [key, entry] : widest)
        std::cout << "  " << key << ": highest slot " << entry.slot << " (" << entry.slot + 1
                  << " needed) from " << entry.where << '\n';
      std::cout << "  widest constant buffer: " << widest_cb_bytes << " bytes from " << widest_cb_where << '\n';
      std::cout << "  most vertex input elements: " << widest_inputs << " from " << widest_inputs_where << '\n';
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
