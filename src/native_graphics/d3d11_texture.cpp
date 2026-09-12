#include "d3d11_texture.h"
#include <d3dcompiler.h>
#include <string>
#include <string_view>
#include <limits>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace edf::native {
namespace {
void ValidateSamples(ID3D11Device& device,DXGI_FORMAT format,uint32_t samples) {
  if(samples!=1 && samples!=2 && samples!=4)
    throw std::runtime_error("unsupported native sample count");
  if(samples>1) {
    UINT levels=0;
    if(FAILED(device.CheckMultisampleQualityLevels(format,samples,&levels)) || !levels)
      throw std::runtime_error("native format/sample count unavailable");
  }
}
Microsoft::WRL::ComPtr<ID3D11Texture2D> ResolveDiagnosticColor(
    ID3D11DeviceContext& context,ID3D11Texture2D& surface) {
  D3D11_TEXTURE2D_DESC desc{}; surface.GetDesc(&desc);
  if(desc.SampleDesc.Count==1) return {};
  if((desc.SampleDesc.Count!=2 && desc.SampleDesc.Count!=4) || desc.ArraySize!=1 || desc.MipLevels!=1 ||
     (desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM))
    throw std::runtime_error("unsupported multisample diagnostic color");
  desc.SampleDesc={1,0}; desc.Usage=D3D11_USAGE_DEFAULT;
  desc.BindFlags=desc.CPUAccessFlags=desc.MiscFlags=0;
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved;
  if(FAILED(device->CreateTexture2D(&desc,nullptr,&resolved)))
    throw std::runtime_error("multisample diagnostic resolve allocation failed");
  context.ResolveSubresource(resolved.Get(),0,&surface,0,desc.Format);
  return resolved;
}
uint32_t Word(std::span<const uint8_t> data, size_t at) {
  if (at > data.size() || 4 > data.size() - at) throw std::runtime_error("truncated DDS header");
  return uint32_t(data[at]) | (uint32_t(data[at+1]) << 8) |
         (uint32_t(data[at+2]) << 16) | (uint32_t(data[at+3]) << 24);
}
uint8_t Channel(uint32_t pixel, uint32_t mask, uint8_t absent) {
  if (!mask) return absent;
  const auto shift = std::countr_zero(mask);
  const uint32_t maximum = mask >> shift;
  return static_cast<uint8_t>((uint64_t((pixel & mask) >> shift) * 255 + maximum / 2) / maximum);
}
}
NativeDepthTarget CreateNativeDepthTarget(ID3D11Device& device,uint32_t width,
                                          uint32_t height,DXGI_FORMAT format,uint32_t samples) {
  if (!width || !height || width>16384 || height>16384 ||
      (format!=DXGI_FORMAT_D24_UNORM_S8_UINT && format!=DXGI_FORMAT_D32_FLOAT &&
       format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT))
    throw std::runtime_error("unsupported native depth target dimensions/format");
  ValidateSamples(device,format,samples);
  NativeDepthTarget result;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=1;
  desc.Format=format; desc.SampleDesc.Count=samples;
  desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(device.CreateTexture2D(&desc,nullptr,&result.surface)) ||
      FAILED(device.CreateDepthStencilView(result.surface.Get(),nullptr,&result.target)))
    throw std::runtime_error("native depth target creation failed");
  result.width=width; result.height=height;
  result.has_stencil=format!=DXGI_FORMAT_D32_FLOAT;
  return result;
}
void ClearNativeDepthTarget(ID3D11DeviceContext& context,NativeDepthTarget& target,
                            bool depth,bool stencil,float value,uint8_t stencil_value) {
  if (!target.target || (!depth && !stencil) || (stencil && !target.has_stencil) ||
      (depth && (!std::isfinite(value) || value<0 || value>1)))
    throw std::runtime_error("invalid native depth/stencil clear");
  context.ClearDepthStencilView(target.target.Get(),(depth?D3D11_CLEAR_DEPTH:0)|(stencil?D3D11_CLEAR_STENCIL:0),value,stencil_value);
  if (depth) target.depth_valid=true;
  if (stencil) target.stencil_valid=true;
}
NativeRenderTarget CreateNativeRenderTarget(ID3D11Device& device, uint32_t width,
                                           uint32_t height, DXGI_FORMAT format,uint32_t samples) {
  if (!width || !height || width > 16384 || height > 16384)
    throw std::runtime_error("invalid native render target dimensions");
  switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM: break;
    default: throw std::runtime_error("unsupported native render target format");
  }
  ValidateSamples(device,format,samples);
  NativeRenderTarget result;
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = width; desc.Height = height;
  desc.MipLevels = desc.ArraySize = 1; desc.SampleDesc.Count=samples;
  desc.Format = format; desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(device.CreateTexture2D(&desc,nullptr,&result.surface)) ||
      FAILED(device.CreateRenderTargetView(result.surface.Get(),nullptr,&result.target)))
    throw std::runtime_error("native render surface creation failed");
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.SampleDesc.Count=1; // Shaders sample the explicit single-sample resolve.
  auto& sampled = result.sampled;
  if (FAILED(device.CreateTexture2D(&desc,nullptr,&sampled.resource)) ||
      FAILED(device.CreateShaderResourceView(sampled.resource.Get(),nullptr,&sampled.view)))
    throw std::runtime_error("native resolve texture creation failed");
  sampled.width = width; sampled.height = height; sampled.mip_count = 1;
  sampled.content_valid = false;
  return result;
}
static NativeRenderTarget CreateConvertedTarget(ID3D11Device& device,uint32_t width,uint32_t height,
  DXGI_FORMAT surface_format,DXGI_FORMAT sampled_format,std::string_view source,NativeRenderTarget::Conversion conversion) {
  auto result=CreateNativeRenderTarget(device,width,height,surface_format);
  result.conversion=conversion;
  D3D11_TEXTURE2D_DESC desc{};
  result.surface->GetDesc(&desc);
  desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  result.target.Reset(); result.surface.Reset();
  if (FAILED(device.CreateTexture2D(&desc,nullptr,&result.surface)) ||
      FAILED(device.CreateRenderTargetView(result.surface.Get(),nullptr,&result.target)) ||
      FAILED(device.CreateShaderResourceView(result.surface.Get(),nullptr,&result.resolve_source)))
    throw std::runtime_error("native converted render surface creation failed");
  desc.Format=sampled_format;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  result.sampled.view.Reset(); result.sampled.resource.Reset();
  if (FAILED(device.CreateTexture2D(&desc,nullptr,&result.sampled.resource)) ||
      FAILED(device.CreateShaderResourceView(result.sampled.resource.Get(),nullptr,&result.sampled.view)) ||
      FAILED(device.CreateUnorderedAccessView(result.sampled.resource.Get(),nullptr,&result.resolve_destination)))
    throw std::runtime_error("native converted sampled texture creation failed");
  Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
  if (FAILED(D3DCompile(source.data(),source.size(),"native-target-resolve",nullptr,nullptr,"CS","cs_5_0",
                        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors)) ||
      FAILED(device.CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&result.resolve_shader)))
    throw std::runtime_error("native target resolve shader creation failed");
  return result;
}
NativeRenderTarget CreateNativeLuminanceTarget(ID3D11Device& device,uint32_t width,uint32_t height) {
  return CreateConvertedTarget(device,width,height,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,R"(
Texture2D<float> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);
[numthreads(8,8,1)] void CS(uint3 at:SV_DispatchThreadID) {
  uint width,height; Destination.GetDimensions(width,height);
  if (at.x<width && at.y<height) Destination[at.xy]=float4(Source.Load(int3(at.xy,0)),1,1,1);
})",NativeRenderTarget::Conversion::luminance);
}
NativeRenderTarget CreateNativeBloomTarget(ID3D11Device& device,uint32_t width,uint32_t height) {
  // The guest swaps red/blue on resolve for the BGRA texture and swaps them
  // back on sampling. Native RGBA storage directly represents logical color.
  return CreateConvertedTarget(device,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,R"(
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);
[numthreads(8,8,1)] void CS(uint3 at:SV_DispatchThreadID) {
  uint width,height; Destination.GetDimensions(width,height);
  if (at.x<width && at.y<height) Destination[at.xy]=Source.Load(int3(at.xy,0));
})",NativeRenderTarget::Conversion::rgba8);
}
NativeRenderTarget CreateNativeOpaqueFrameTarget(ID3D11Device& device,uint32_t width,uint32_t height) {
  // XRGB backbuffers have constant-one sampled alpha. Logical RGB is already
  // channel-correct; no tone mapping or display gamma belongs in this resolve.
  return CreateConvertedTarget(device,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,R"(
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);
[numthreads(8,8,1)] void CS(uint3 at:SV_DispatchThreadID) {
  uint width,height; Destination.GetDimensions(width,height);
  if (at.x<width && at.y<height) Destination[at.xy]=float4(Source.Load(int3(at.xy,0)).rgb,1);
})",NativeRenderTarget::Conversion::rgba8);
}
bool ImportZeroLuminanceHistory(ID3D11DeviceContext& context,NativeRenderTarget& target,
                                std::span<const uint8_t> initial_page) {
  if (target.sampled.width!=1 || target.sampled.height!=1 || target.conversion!=NativeRenderTarget::Conversion::luminance ||
      !target.resolve_shader || !target.sampled.resource ||
      target.content_valid || target.sampled.content_valid || initial_page.size()!=4096 ||
      !std::all_of(initial_page.begin(),initial_page.end(),[](uint8_t value) { return value==0; }))
    return false;
  const uint16_t pixel[]{0,0x3c00,0x3c00,0x3c00};
  context.UpdateSubresource(target.sampled.resource.Get(),0,nullptr,pixel,sizeof(pixel),0);
  target.sampled.content_valid=true;
  // This is the initial sampled allocation, not a clear of the R32F surface.
  return true;
}
void ResolveNativeRgba8Frame(ID3D11DeviceContext& context,const NativeRenderTarget& source,
                             NativeRenderTarget& destination) {
  if(&source==&destination) throw std::runtime_error("direct frame resolve aliases source");
  destination.content_valid=destination.sampled.content_valid=false;
  if(!source.surface || !destination.surface ||
     destination.conversion!=NativeRenderTarget::Conversion::rgba8 || !destination.resolve_shader)
    throw std::runtime_error("invalid direct frame conversion pair");
  D3D11_TEXTURE2D_DESC in{},out{};
  source.surface->GetDesc(&in); destination.surface->GetDesc(&out);
  if(source.surface.Get()==destination.surface.Get() || in.Width!=out.Width || in.Height!=out.Height ||
     in.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT || out.Format!=in.Format ||
     in.MipLevels!=1 || in.ArraySize!=1 || out.SampleDesc.Count!=1 ||
     (in.SampleDesc.Count!=1 && in.SampleDesc.Count!=2 && in.SampleDesc.Count!=4))
    throw std::runtime_error("unsupported direct frame resolve source");
  Microsoft::WRL::ComPtr<ID3D11Device> device,source_device,destination_device;
  context.GetDevice(&device); source.surface->GetDevice(&source_device);
  destination.surface->GetDevice(&destination_device);
  if(device.Get()!=source_device.Get() || device.Get()!=destination_device.Get())
    throw std::runtime_error("direct frame resolve device mismatch");
  if(!source.content_valid) return;
  if(in.SampleDesc.Count>1)
    context.ResolveSubresource(destination.surface.Get(),0,source.surface.Get(),0,in.Format);
  else context.CopyResource(destination.surface.Get(),source.surface.Get());
  destination.content_valid=true;
  ResolveNativeRenderTarget(context,destination);
}
void ClearNativeColorTarget(ID3D11DeviceContext& context, NativeRenderTarget& target,
                           uint32_t argb) {
  if (!target.target) throw std::runtime_error("invalid native color clear target");
  const float color[]{float((argb>>16)&255)/255.0f,float((argb>>8)&255)/255.0f,
                      float(argb&255)/255.0f,float(argb>>24)/255.0f};
  context.ClearRenderTargetView(target.target.Get(),color);
  target.content_valid=true;
}
void ResolveNativeRenderTarget(ID3D11DeviceContext& context, NativeRenderTarget& target) {
  // The caller unbinds sampled views before resolving. Multisampled color
  // resolves natively; single-sample surfaces retain the copy boundary.
  if (target.content_valid && target.resolve_shader) {
    // This native context does not use dynamic shader linkage. Preserve its
    // compute slot0 and output-merger bindings across the conversion.
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> input;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> output;
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
    context.CSGetShader(&shader,nullptr,nullptr);
    context.CSGetShaderResources(0,1,&input);
    context.CSGetUnorderedAccessViews(0,1,&output);
    context.OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,&depth);
    context.OMSetRenderTargets(0,nullptr,nullptr);
    auto* source=target.resolve_source.Get(); auto* destination=target.resolve_destination.Get();
    context.CSSetShader(target.resolve_shader.Get(),nullptr,0);
    context.CSSetShaderResources(0,1,&source);
    context.CSSetUnorderedAccessViews(0,1,&destination,nullptr);
    context.Dispatch((target.sampled.width+7)/8,(target.sampled.height+7)/8,1);
    ID3D11ShaderResourceView* no_input=nullptr;
    ID3D11UnorderedAccessView* no_output=nullptr;
    context.CSSetShaderResources(0,1,&no_input);
    context.CSSetUnorderedAccessViews(0,1,&no_output,nullptr);
    source=input.Get(); destination=output.Get();
    context.CSSetShader(shader.Get(),nullptr,0);
    context.CSSetShaderResources(0,1,&source);
    context.CSSetUnorderedAccessViews(0,1,&destination,nullptr);
    context.OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,depth.Get());
    for (auto* view:targets) if (view) view->Release();
  } else if (target.content_valid) {
    D3D11_TEXTURE2D_DESC desc{}; target.surface->GetDesc(&desc);
    if(desc.SampleDesc.Count>1)
      context.ResolveSubresource(target.sampled.resource.Get(),0,target.surface.Get(),0,desc.Format);
    else context.CopyResource(target.sampled.resource.Get(),target.surface.Get());
  }
  target.sampled.content_valid = target.content_valid;
}
NativeTexture CreateNativeDdsTexture(ID3D11Device& device, std::span<const uint8_t> data) {
  if (data.size() < 128 || Word(data, 0) != 0x20534444 || Word(data, 4) != 124 || Word(data, 76) != 32)
    throw std::runtime_error("invalid DDS header");
  NativeTexture result;
  result.width = Word(data, 16); result.height = Word(data, 12);
  result.mip_count = (std::max)(1u, Word(data, 28));
  const uint32_t caps2 = Word(data, 112);
  result.cube = (caps2 & 0x200) != 0;
  if (!result.width || !result.height || result.width > 16384 || result.height > 16384 ||
      result.mip_count > std::bit_width((std::max)(result.width, result.height)) ||
      Word(data, 24) > 1 || (caps2 & 0x200000)) throw std::runtime_error("unsupported DDS dimensions");
  if (result.cube && ((caps2 & 0xfc00) != 0xfc00 || result.width != result.height))
    throw std::runtime_error("incomplete or nonsquare DDS cube");
  uint32_t block_bytes = 0, pixel_bytes = 0;
  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
  const auto flags = Word(data, 80);
  // Retail MapXX/Shadow*.dds use DDPF_ALPHA / A8, not RGB luminance.
  // Expand to (0,0,0,A); Common.fx consumes the sampled alpha component.
  const bool alpha_only=flags==2 && Word(data,88)==8 && Word(data,92)==0 &&
    Word(data,96)==0 && Word(data,100)==0 && Word(data,104)==255;
  if (flags & 4) {
    switch (Word(data, 84)) {
      case 0x31545844: block_bytes = 8; format = DXGI_FORMAT_BC1_UNORM; break;
      case 0x33545844: block_bytes = 16; format = DXGI_FORMAT_BC2_UNORM; break;
      case 0x35545844: block_bytes = 16; format = DXGI_FORMAT_BC3_UNORM; break;
      default: throw std::runtime_error("unsupported DDS FourCC: " + std::to_string(Word(data,84)));
    }
  } else {
    const auto bits = Word(data, 88);
    if ((!alpha_only && !(flags & 0x40)) || !bits || bits > 32 || bits % 8)
      throw std::runtime_error("unsupported DDS pixel format: flags=" + std::to_string(flags) +
        " bits=" + std::to_string(bits) + " masks=" + std::to_string(Word(data,92)) + "/" +
        std::to_string(Word(data,96)) + "/" + std::to_string(Word(data,100)) + "/" + std::to_string(Word(data,104)));
    pixel_bytes = bits / 8;
  }
  std::array<uint32_t,4> masks{Word(data,92), Word(data,96), Word(data,100), Word(data,104)};
  if (!block_bytes) {
    uint32_t used = 0;
    for (auto mask : masks) {
      if ((used & mask) || (pixel_bytes < 4 && (mask >> (pixel_bytes * 8))))
        throw std::runtime_error("overlapping or out-of-range DDS channel masks");
      if (mask) {
        const auto normalized = mask >> std::countr_zero(mask);
        if (normalized & (normalized + 1u)) throw std::runtime_error("noncontiguous DDS channel mask");
      }
      used |= mask;
    }
    if (!alpha_only && (!masks[0] || !masks[1] || !masks[2])) throw std::runtime_error("missing DDS RGB mask");
  }
  const uint32_t faces = result.cube ? 6 : 1;
  std::vector<std::vector<uint8_t>> converted(faces * result.mip_count);
  std::vector<D3D11_SUBRESOURCE_DATA> subresources(faces * result.mip_count);
  size_t cursor = 128, decoded_total = 0;
  for (uint32_t face = 0; face < faces; ++face) for (uint32_t mip = 0; mip < result.mip_count; ++mip) {
    const uint32_t width = (std::max)(1u, result.width >> mip), height = (std::max)(1u, result.height >> mip);
    const uint32_t rows = block_bytes ? (height + 3) / 4 : height;
    uint32_t pitch = block_bytes ? ((width + 3) / 4) * block_bytes : width * pixel_bytes;
    // For raw top levels DDSD_PITCH describes row padding; subsequent mip
    // rows in this importer use their tightly packed byte width.
    if (!block_bytes && mip == 0 && (Word(data, 8) & 8)) {
      const auto declared = Word(data, 20);
      if (declared < pitch) throw std::runtime_error("DDS row pitch is too small");
      pitch = declared;
    }
    const size_t size = size_t(pitch) * rows;
    if (cursor > data.size() || size > data.size() - cursor) throw std::runtime_error("truncated DDS mip payload");
    const size_t index = size_t(face) * result.mip_count + mip;
    if (block_bytes) {
      subresources[index] = {data.data() + cursor, pitch, static_cast<UINT>(size)};
    } else {
      const size_t decoded_size = size_t(width) * height * 4;
      decoded_total += decoded_size;
      if (decoded_total > 256 * 1024 * 1024) throw std::runtime_error("DDS decoded size limit exceeded");
      auto& pixels = converted[index]; pixels.resize(decoded_size);
      for (uint32_t y = 0; y < height; ++y) for (uint32_t x = 0; x < width; ++x) {
        const auto* source = data.data() + cursor + size_t(y) * pitch + size_t(x) * pixel_bytes;
        uint32_t value = 0;
        for (uint32_t b = 0; b < pixel_bytes; ++b) value |= uint32_t(source[b]) << (8 * b);
        auto* target = pixels.data() + (size_t(y) * width + x) * 4;
        for (size_t c = 0; c < 4; ++c) target[c] = Channel(value, masks[c], c == 3 ? 255 : 0);
      }
      subresources[index] = {pixels.data(), width * 4, static_cast<UINT>(decoded_size)};
    }
    cursor += size;
  }
  D3D11_TEXTURE2D_DESC desc{};
  desc.Width = result.width; desc.Height = result.height; desc.MipLevels = result.mip_count;
  desc.ArraySize = faces; desc.Format = format; desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = result.cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
  if (FAILED(device.CreateTexture2D(&desc, subresources.data(), &result.resource)))
    throw std::runtime_error("native DDS texture creation failed");
  if (FAILED(device.CreateShaderResourceView(result.resource.Get(), nullptr, &result.view)))
    throw std::runtime_error("native DDS view creation failed");
  return result;
}
NativeHdrRange InspectNativeHdrColor(ID3D11DeviceContext& context,ID3D11Texture2D& surface) {
  if(auto resolved=ResolveDiagnosticColor(context,surface))
    return InspectNativeHdrColor(context,*resolved.Get());
  D3D11_TEXTURE2D_DESC desc{}; surface.GetDesc(&desc);
  if ((desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) ||
      desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
      !desc.Width || !desc.Height || desc.Width>4096 || desc.Height>4096)
    throw std::runtime_error("unsupported diagnostic HDR range resource");
  desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) throw std::runtime_error("HDR range staging failed");
  context.CopyResource(staging.Get(),&surface);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context.Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("HDR range mapping failed");
  auto half=[](uint16_t bits) {
    const int exponent=(bits>>10)&31,mantissa=bits&1023;
    if (exponent==31) return mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
    const float value=exponent ? std::ldexp(float(1024+mantissa),exponent-25) : std::ldexp(float(mantissa),-24);
    return bits&0x8000 ? -value : value;
  };
  NativeHdrRange range;
  float worst=0;
  range.minimum.fill(std::numeric_limits<float>::infinity());
  range.maximum.fill(-std::numeric_limits<float>::infinity());
  std::array<double,3> totals{};
  for (UINT y=0;y<desc.Height;++y) {
    const auto* row=static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch;
    for (UINT x=0;x<desc.Width;++x) {
      std::array<float,3> rgb{};
      if (desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM)
        for (size_t i=0;i<3;++i) rgb[i]=row[size_t(x)*4+i]/255.0f;
      else for (size_t i=0;i<3;++i) rgb[i]=half(reinterpret_cast<const uint16_t*>(row)[size_t(x)*4+i]);
      if (!std::isfinite(rgb[0]) || !std::isfinite(rgb[1]) || !std::isfinite(rgb[2])) {
        ++range.nonfinite_pixels; continue;
      }
      ++range.pixels;
      float peak=rgb[0],trough=rgb[0];
      for (size_t i=0;i<3;++i) {
        range.minimum[i]=(std::min)(range.minimum[i],rgb[i]);
        range.maximum[i]=(std::max)(range.maximum[i],rgb[i]);
        totals[i]+=rgb[i];
        peak=(std::max)(peak,rgb[i]);
        trough=(std::min)(trough,rgb[i]);
      }
      if (trough<0) {
        ++range.negative_pixels;
        if (trough<=worst) { worst=trough; range.worst_x=x; range.worst_y=y; }
      }
      for (size_t threshold=0;threshold<range.above.size();++threshold)
        if (peak>float(1u<<threshold)) ++range.above[threshold];
    }
  }
  context.Unmap(staging.Get(),0);
  if (range.pixels) for (size_t i=0;i<3;++i) range.mean[i]=totals[i]/double(range.pixels);
  else { range.minimum={}; range.maximum={}; }
  return range;
}
std::array<float,4> ReadNativeColorPixel(ID3D11DeviceContext& context,ID3D11Texture2D& surface,uint32_t x,uint32_t y) {
  if(auto resolved=ResolveDiagnosticColor(context,surface))
    return ReadNativeColorPixel(context,*resolved.Get(),x,y);
  D3D11_TEXTURE2D_DESC desc{}; surface.GetDesc(&desc);
  if ((desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) ||
      desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 || x>=desc.Width || y>=desc.Height)
    throw std::runtime_error("unsupported diagnostic pixel resource/coordinates");
  desc.Width=desc.Height=1; desc.Usage=D3D11_USAGE_STAGING;
  desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) throw std::runtime_error("pixel staging allocation failed");
  const D3D11_BOX box{x,y,0,x+1,y+1,1};
  context.CopySubresourceRegion(staging.Get(),0,0,0,0,&surface,0,&box);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context.Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("pixel readback failed");
  std::array<float,4> result{};
  for (size_t i=0;i<4;++i) {
    if (desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM) result[i]=static_cast<const uint8_t*>(mapped.pData)[i]/255.0f;
    else {
      const auto bits=static_cast<const uint16_t*>(mapped.pData)[i];
      const int exponent=(bits>>10)&31,mantissa=bits&1023;
      const float value=exponent==31 ? (mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity()) :
        exponent ? std::ldexp(float(1024+mantissa),exponent-25) : std::ldexp(float(mantissa),-24);
      result[i]=bits&0x8000 ? -value : value;
    }
  }
  context.Unmap(staging.Get(),0);
  return result;
}
bool FindNativeInvalidColorPixel(ID3D11DeviceContext& context,ID3D11Texture2D& surface,
    uint32_t x,uint32_t y,uint32_t width,uint32_t height,uint32_t& found_x,uint32_t& found_y,
    bool include_negative) {
  if(auto resolved=ResolveDiagnosticColor(context,surface))
    return FindNativeInvalidColorPixel(context,*resolved.Get(),x,y,width,height,found_x,found_y,include_negative);
  D3D11_TEXTURE2D_DESC desc{}; surface.GetDesc(&desc);
  if((desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) ||
     desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
     !width || !height || width>4096 || height>4096 || x>=desc.Width || y>=desc.Height ||
     width>desc.Width-x || height>desc.Height-y)
    throw std::runtime_error("unsupported diagnostic color region");
  if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM) return false;
  desc.Width=width; desc.Height=height; desc.Usage=D3D11_USAGE_STAGING;
  desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging)))
    throw std::runtime_error("color region staging allocation failed");
  const D3D11_BOX box{x,y,0,x+width,y+height,1};
  context.CopySubresourceRegion(staging.Get(),0,0,0,0,&surface,0,&box);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if(FAILED(context.Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))
    throw std::runtime_error("color region readback failed");
  bool found=false;
  for(uint32_t row=0;row<height && !found;++row) {
    const auto* pixels=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+size_t(row)*mapped.RowPitch);
    for(uint32_t column=0;column<width;++column) {
      // Half-float bit patterns at or above 0xbc00 are negatives of magnitude
      // 1 or more, because sign-magnitude ordering is monotonic per sign.
      constexpr uint16_t negative_one=0xbc00;
      bool invalid=false;
      for(size_t channel=0;channel<3;++channel) {
        const auto bits=pixels[column*4+channel];
        invalid|=(bits&0x7c00)==0x7c00 || (include_negative && bits>=negative_one);
      }
      if(invalid) {
        found_x=x+column; found_y=y+row; found=true; break;
      }
    }
  }
  context.Unmap(staging.Get(),0);
  return found;
}
std::vector<uint8_t> CaptureNativeHdrBmp(ID3D11DeviceContext& context,ID3D11Texture2D& surface) {
  if(auto resolved=ResolveDiagnosticColor(context,surface))
    return CaptureNativeHdrBmp(context,*resolved.Get());
  D3D11_TEXTURE2D_DESC desc{}; surface.GetDesc(&desc);
  if ((desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) || desc.SampleDesc.Count!=1 ||
      desc.ArraySize!=1 || desc.MipLevels!=1 || !desc.Width || !desc.Height ||
      desc.Width>4096 || desc.Height>4096) throw std::runtime_error("unsupported native diagnostic surface");
  const uint32_t pitch=(desc.Width*3+3)&~3u;
  std::vector<uint8_t> bmp(54+size_t(pitch)*desc.Height,0);
  auto word=[&](size_t at,uint32_t value) {
    for (unsigned b=0;b<4;++b) bmp[at+b]=uint8_t(value>>(8*b));
  };
  bmp[0]='B'; bmp[1]='M'; word(2,static_cast<uint32_t>(bmp.size())); word(10,54); word(14,40);
  word(18,desc.Width); word(22,0u-desc.Height); bmp[26]=1; bmp[28]=24; word(34,pitch*desc.Height);
  desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) throw std::runtime_error("native capture staging failed");
  context.CopyResource(staging.Get(),&surface);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context.Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("native capture mapping failed");
  auto half=[](uint16_t bits) {
    const int exponent=(bits>>10)&31,mantissa=bits&1023;
    if (exponent==31) return std::numeric_limits<float>::quiet_NaN();
    const float value=exponent ? std::ldexp(float(1024+mantissa),exponent-25) : std::ldexp(float(mantissa),-24);
    return bits&0x8000?-value:value;
  };
  for (UINT y=0;y<desc.Height;++y) {
    const auto* row=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
    for (UINT x=0;x<desc.Width;++x) {
      if (desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM) {
        const auto* rgba=static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch+x*4;
        auto* bgr=bmp.data()+54+size_t(y)*pitch+x*3;
        bgr[0]=rgba[2]; bgr[1]=rgba[1]; bgr[2]=rgba[0];
        continue;
      }
      const float r=half(row[x*4]),g=half(row[x*4+1]),b=half(row[x*4+2]);
      auto* pixel=bmp.data()+54+size_t(y)*pitch+x*3;
      if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) { pixel[0]=255; pixel[1]=0; pixel[2]=255; }
      else {
        pixel[0]=uint8_t(std::clamp(b,0.0f,1.0f)*255+.5f);
        pixel[1]=uint8_t(std::clamp(g,0.0f,1.0f)*255+.5f);
        pixel[2]=uint8_t(std::clamp(r,0.0f,1.0f)*255+.5f);
      }
    }
  }
  context.Unmap(staging.Get(),0);
  return bmp;
}
NativeDepthCoverage InspectNativeDepth(ID3D11DeviceContext& context,ID3D11Texture2D& surface,float clear_depth) {
  D3D11_TEXTURE2D_DESC desc{}; surface.GetDesc(&desc);
  if ((desc.Format!=DXGI_FORMAT_D32_FLOAT && desc.Format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT) ||
      desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
      !desc.Width || !desc.Height || desc.Width>4096 || desc.Height>4096 || !std::isfinite(clear_depth))
    throw std::runtime_error("unsupported native depth diagnostic surface");
  const size_t pixel_bytes=desc.Format==DXGI_FORMAT_D32_FLOAT?4:8;
  desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) throw std::runtime_error("native depth staging failed");
  context.CopyResource(staging.Get(),&surface);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context.Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("native depth mapping failed");
  NativeDepthCoverage result;
  for (UINT y=0;y<desc.Height;++y) for (UINT x=0;x<desc.Width;++x) {
    const auto* pixel=static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch+size_t(x)*pixel_bytes;
    const float depth=std::bit_cast<float>(uint32_t(pixel[0])|(uint32_t(pixel[1])<<8)|(uint32_t(pixel[2])<<16)|(uint32_t(pixel[3])<<24));
    if (!std::isfinite(depth)) { ++result.nonfinite_pixels; continue; }
    result.minimum=(std::min)(result.minimum,depth); result.maximum=(std::max)(result.maximum,depth);
    if (depth!=clear_depth) { ++result.changed_pixels; ++result.vertical_bands[uint64_t(y)*3/desc.Height]; }
  }
  context.Unmap(staging.Get(),0);
  return result;
}
}
