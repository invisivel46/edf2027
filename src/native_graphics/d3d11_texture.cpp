#include "d3d11_texture.h"
#include <cstring>
#include "native_dds_decode.h"
#include "d3d11_backend.h"
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
void ValidateSamples(NativeRenderBackend& backend,DXGI_FORMAT format,uint32_t samples) {
  if(samples!=1 && samples!=2 && samples!=4)
    throw std::runtime_error("unsupported native sample count");
  if(!backend.SupportsSamples(format,samples))
    throw std::runtime_error("native format/sample count unavailable");
}
// The D3D11 handles behind a target the backend made, for the paths that still
// bind, capture and hand off through the context. Null on a backend that is not
// D3D11, which every one of those paths then refuses rather than using nothing.
void AttachD3D11Handles(NativeRenderTarget& target) {
  target.surface=NativeD3D11RenderTargetResource(*target.backend_surface);
  target.target=NativeD3D11RenderTargetView(*target.backend_surface);
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
NativeDepthTarget CreateNativeDepthTarget(NativeRenderBackend& backend,uint32_t width,
                                          uint32_t height,DXGI_FORMAT format,uint32_t samples,
                                          float clear_depth,bool sampled) {
  if (!width || !height || width>16384 || height>16384 ||
      (format!=DXGI_FORMAT_D24_UNORM_S8_UINT && format!=DXGI_FORMAT_D32_FLOAT &&
       format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT))
    throw std::runtime_error("unsupported native depth target dimensions/format");
  ValidateSamples(backend,format,samples);
  NativeDepthTarget result;
  NativeBackendTextureDesc desc{};
  desc.width=width; desc.height=height; desc.levels=1;
  desc.format=format; desc.samples=samples; desc.depth=true;
  if (!std::isfinite(clear_depth) || clear_depth<0 || clear_depth>1)
    throw std::runtime_error("native depth target clear value outside [0,1]");
  desc.clear_depth=clear_depth;
  desc.sampled=sampled && samples<=1;
  result.backend_target=backend.CreateRenderTarget(desc);
  if(!result.backend_target) throw std::runtime_error("native depth target creation failed");
  result.surface=NativeD3D11RenderTargetResource(*result.backend_target);
  result.target=NativeD3D11DepthStencilView(*result.backend_target);
  result.width=width; result.height=height;
  result.format=format; result.samples=samples;
  result.has_stencil=format!=DXGI_FORMAT_D32_FLOAT;
  return result;
}
namespace {
void ValidateDepthClear(const NativeDepthTarget& target,bool depth,bool stencil,float value) {
  if ((!depth && !stencil) || (stencil && !target.has_stencil) ||
      (depth && (!std::isfinite(value) || value<0 || value>1)))
    throw std::runtime_error("invalid native depth/stencil clear");
}
}  // namespace
void ClearNativeDepthTarget(NativeBackendRecorder& recorder,NativeDepthTarget& target,
                            bool depth,bool stencil,float value,uint8_t stencil_value) {
  if(!target.backend_target) throw std::runtime_error("invalid native depth/stencil clear");
  ValidateDepthClear(target,depth,stencil,value);
  recorder.ClearDepthStencil(*target.backend_target,depth,stencil,value,stencil_value);
  if (depth) target.depth_valid=true;
  if (stencil) target.stencil_valid=true;
}
void ClearNativeDepthTarget(ID3D11DeviceContext& context,NativeDepthTarget& target,
                            bool depth,bool stencil,float value,uint8_t stencil_value) {
  if (!target.target) throw std::runtime_error("invalid native depth/stencil clear");
  ValidateDepthClear(target,depth,stencil,value);
  context.ClearDepthStencilView(target.target.Get(),(depth?D3D11_CLEAR_DEPTH:0)|(stencil?D3D11_CLEAR_STENCIL:0),value,stencil_value);
  if (depth) target.depth_valid=true;
  if (stencil) target.stencil_valid=true;
}
NativeRenderTarget CreateNativeRenderTarget(NativeRenderBackend& backend, uint32_t width,
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
  ValidateSamples(backend,format,samples);
  NativeRenderTarget result;
  NativeBackendTextureDesc desc{};
  desc.width=width; desc.height=height; desc.levels=1;
  desc.format=format; desc.samples=samples; desc.render_target=true;
  result.backend_surface=backend.CreateRenderTarget(desc);
  if(!result.backend_surface) throw std::runtime_error("native render surface creation failed");
  AttachD3D11Handles(result);
  result.format=format; result.samples=samples;
  // Shaders sample the explicit single-sample resolve, never the surface.
  NativeBackendTextureDesc sampled_desc{};
  sampled_desc.width=width; sampled_desc.height=height; sampled_desc.levels=1;
  sampled_desc.format=format;
  auto& sampled = result.sampled;
  sampled.backend=backend.CreateTexture(sampled_desc,{});
  if(!sampled.backend) throw std::runtime_error("native resolve texture creation failed");
  sampled.resource=NativeD3D11TextureResource(*sampled.backend);
  sampled.view=NativeD3D11TextureView(*sampled.backend);
  sampled.width = width; sampled.height = height; sampled.mip_count = 1;
  sampled.format = format;
  sampled.content_valid = false;
  return result;
}
namespace {
// A triangle that covers the target, in clip space. Three vertices rather than
// a quad's four: the extra area is clipped and it is one primitive instead of
// two, which also removes the diagonal seam a quad can show.
constexpr float kCoveringTriangle[]{-1.f,-1.f, 3.f,-1.f, -1.f,3.f};
constexpr char kConvertVertexShader[]=R"(
float4 VS(float2 position : POSITION) : SV_POSITION { return float4(position,0,1); }
)";
std::span<const uint8_t> BlobBytes(ID3DBlob& blob) {
  return {static_cast<const uint8_t*>(blob.GetBufferPointer()),blob.GetBufferSize()};
}
Microsoft::WRL::ComPtr<ID3DBlob> CompileConvertShader(std::string_view source,const char* entry,
                                                      const char* profile) {
  Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(source.data(),source.size(),"native-target-convert",nullptr,nullptr,entry,profile,
                       D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors)))
    throw std::runtime_error(std::string("native target conversion shader failed: ")+
      (errors?static_cast<const char*>(errors->GetBufferPointer()):"unknown"));
  return code;
}
}  // namespace
static NativeRenderTarget CreateConvertedTarget(NativeRenderBackend& backend,uint32_t width,uint32_t height,
  DXGI_FORMAT surface_format,DXGI_FORMAT sampled_format,std::string_view source,
  NativeRenderTarget::Conversion conversion,uint64_t shader_id) {
  // Both halves are sampled render targets: the surface because the conversion
  // reads it, and the converted result because the conversion draws into it and
  // everything downstream samples it.
  NativeRenderTarget result;
  result.conversion=conversion;
  NativeBackendTextureDesc desc{};
  desc.width=width; desc.height=height; desc.levels=1;
  desc.format=surface_format; desc.render_target=true; desc.sampled=true;
  result.backend_surface=backend.CreateRenderTarget(desc);
  if(!result.backend_surface) throw std::runtime_error("native converted render surface creation failed");
  AttachD3D11Handles(result);
  result.format=surface_format; result.samples=1;
  if(!result.backend_surface->texture())
    throw std::runtime_error("a converted target surface cannot be sampled");

  auto converted_desc=desc;
  converted_desc.format=sampled_format;
  result.converted_target=backend.CreateRenderTarget(converted_desc);
  if(!result.converted_target) throw std::runtime_error("native converted sampled texture creation failed");
  auto* converted_texture=result.converted_target->texture();
  if(!converted_texture) throw std::runtime_error("a converted target result cannot be sampled");
  result.sampled.backend=std::shared_ptr<NativeBackendTexture>(result.converted_target,converted_texture);
  result.sampled.resource=NativeD3D11TextureResource(*converted_texture);
  result.sampled.view=NativeD3D11TextureView(*converted_texture);
  result.sampled.width=width; result.sampled.height=height; result.sampled.mip_count=1;
  result.sampled.format=sampled_format;
  result.sampled.content_valid=false;

  NativeBackendBufferDesc vertices{};
  vertices.bytes=sizeof(kCoveringTriangle);
  vertices.vertex=true;
  result.convert_vertices=backend.CreateBuffer(vertices,
    {reinterpret_cast<const uint8_t*>(kCoveringTriangle),sizeof(kCoveringTriangle)});
  if(!result.convert_vertices) throw std::runtime_error("native conversion vertex buffer creation failed");

  const auto vertex_code=CompileConvertShader(kConvertVertexShader,"VS","vs_5_0");
  const auto pixel_code=CompileConvertShader(source,"PS","ps_5_0");
  const NativeBackendInputElement layout[]{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,false,0}};
  NativeBackendPipelineDesc pipeline{};
  pipeline.vertex=BlobBytes(*vertex_code.Get());
  pipeline.pixel=BlobBytes(*pixel_code.Get());
  // One vertex shader for every conversion, and one pixel shader per source
  // here, so the ids are constants rather than guest handles. The pixel id is
  // per shader and not per Conversion: two kinds share Conversion::rgba8 and
  // differ only in the shader, and keying the cache on the kind would hand one
  // of them the other's pipeline.
  pipeline.vertex_id=0x636f6e7600000001ull;
  pipeline.pixel_id=0x636f6e7600000010ull+shader_id;
  pipeline.input_layout=layout;
  pipeline.input_layout_id=0x636f6e7600000002ull;
  pipeline.state=kNativeOpaqueCopyState;
  pipeline.topology=NativeBackendTopology::TriangleList;
  pipeline.render_targets=1;
  pipeline.rtv_format[0]=sampled_format;
  pipeline.sample_count=1;
  result.convert_pipeline=&backend.CreatePipeline(pipeline);
  return result;
}
NativeRenderTarget CreateNativeLuminanceTarget(NativeRenderBackend& backend,uint32_t width,uint32_t height) {
  return CreateConvertedTarget(backend,width,height,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,R"(
Texture2D<float> Source : register(t0);
float4 PS(float4 position : SV_POSITION) : SV_TARGET {
  return float4(Source.Load(int3(position.xy,0)),1,1,1);
})",NativeRenderTarget::Conversion::luminance,0);
}
NativeRenderTarget CreateNativeBloomTarget(NativeRenderBackend& backend,uint32_t width,uint32_t height) {
  // The guest swaps red/blue on resolve for the BGRA texture and swaps them
  // back on sampling. Native RGBA storage directly represents logical color.
  return CreateConvertedTarget(backend,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,R"(
Texture2D<float4> Source : register(t0);
float4 PS(float4 position : SV_POSITION) : SV_TARGET {
  return Source.Load(int3(position.xy,0));
})",NativeRenderTarget::Conversion::rgba8,1);
}
NativeRenderTarget CreateNativeOpaqueFrameTarget(NativeRenderBackend& backend,uint32_t width,uint32_t height) {
  // XRGB backbuffers have constant-one sampled alpha. Logical RGB is already
  // channel-correct; no tone mapping or display gamma belongs in this resolve.
  return CreateConvertedTarget(backend,width,height,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,R"(
Texture2D<float4> Source : register(t0);
float4 PS(float4 position : SV_POSITION) : SV_TARGET {
  return float4(Source.Load(int3(position.xy,0)).rgb,1);
})",NativeRenderTarget::Conversion::rgba8,2);
}
bool ImportZeroLuminanceHistory(NativeBackendRecorder& recorder,NativeRenderTarget& target,
                                std::span<const uint8_t> initial_page) {
  if(target.sampled.width!=1 || target.sampled.height!=1 ||
     target.conversion!=NativeRenderTarget::Conversion::luminance ||
     !target.convert_pipeline || !target.sampled.backend || target.content_valid ||
     target.sampled.content_valid || initial_page.size()!=4096 ||
     !std::all_of(initial_page.begin(),initial_page.end(),[](uint8_t value) { return value==0; }))
    return false;
  const uint16_t pixel[]{0,0x3c00,0x3c00,0x3c00};
  recorder.UpdateTexture(*target.sampled.backend,
    {reinterpret_cast<const uint8_t*>(pixel),sizeof(pixel)});
  target.sampled.content_valid=true;
  return true;
}
bool ImportZeroLuminanceHistory(ID3D11DeviceContext& context,NativeRenderTarget& target,
                                std::span<const uint8_t> initial_page) {
  if (target.sampled.width!=1 || target.sampled.height!=1 || target.conversion!=NativeRenderTarget::Conversion::luminance ||
      !target.convert_pipeline || !target.sampled.resource ||
      target.content_valid || target.sampled.content_valid || initial_page.size()!=4096 ||
      !std::all_of(initial_page.begin(),initial_page.end(),[](uint8_t value) { return value==0; }))
    return false;
  const uint16_t pixel[]{0,0x3c00,0x3c00,0x3c00};
  context.UpdateSubresource(target.sampled.resource.Get(),0,nullptr,pixel,sizeof(pixel),0);
  target.sampled.content_valid=true;
  // This is the initial sampled allocation, not a clear of the R32F surface.
  return true;
}
// The scene's HDR surface, resolved and converted into an RGBA8 frame.
//
// Both halves are recorded: the multisample resolve into the destination's own
// surface, then the conversion draw that writes the RGBA8 result. It used to be
// a ResolveSubresource plus a compute dispatch, and the destination's device
// had to match the context's - a check that has no meaning once both come from
// one backend, which is the only way they can be used together at all.
void ResolveNativeRgba8Frame(NativeBackendRecorder& recorder,const NativeRenderTarget& source,
                             NativeRenderTarget& destination) {
  if(&source==&destination) throw std::runtime_error("direct frame resolve aliases source");
  destination.content_valid=destination.sampled.content_valid=false;
  if(!source.backend_surface || !destination.backend_surface ||
     destination.conversion!=NativeRenderTarget::Conversion::rgba8 || !destination.convert_pipeline)
    throw std::runtime_error("invalid direct frame conversion pair");
  auto* into=destination.backend_surface->texture();
  if(!into) throw std::runtime_error("a direct frame destination surface cannot be written");
  if(source.backend_surface->width()!=destination.backend_surface->width() ||
     source.backend_surface->height()!=destination.backend_surface->height() ||
     source.format!=DXGI_FORMAT_R16G16B16A16_FLOAT || destination.format!=source.format)
    throw std::runtime_error("unsupported direct frame resolve source");
  if(!source.content_valid) return;
  recorder.ResolveTarget(*into,*source.backend_surface);
  destination.content_valid=true;
  ResolveNativeRenderTarget(recorder,destination);
}
std::array<float,4> NativeClearColor(uint32_t argb) {
  return {float((argb>>16)&255)/255.0f,float((argb>>8)&255)/255.0f,
          float(argb&255)/255.0f,float(argb>>24)/255.0f};
}
void ClearNativeColorTarget(NativeBackendRecorder& recorder, NativeRenderTarget& target,
                           uint32_t argb) {
  if (!target.backend_surface) throw std::runtime_error("invalid native color clear target");
  recorder.ClearColor(*target.backend_surface,NativeClearColor(argb));
  target.content_valid=true;
}
void ClearNativeColorTarget(ID3D11DeviceContext& context, NativeRenderTarget& target,
                           uint32_t argb) {
  if (!target.target) throw std::runtime_error("invalid native color clear target");
  const auto color=NativeClearColor(argb);
  context.ClearRenderTargetView(target.target.Get(),color.data());
  target.content_valid=true;
}
// The conversion pass, recorded. Both overloads below end here for a
// converting target; only the plain resolve differs between them.
static void RecordConversion(NativeBackendRecorder& recorder, NativeRenderTarget& target) {
  NativeBackendRenderTarget* colors[]{target.converted_target.get()};
  recorder.SetRenderTargets(colors,nullptr);
  recorder.SetViewport({0,0,float(target.sampled.width),float(target.sampled.height),0,1});
  recorder.SetScissor({},false);
  recorder.SetPipeline(*target.convert_pipeline);
  recorder.SetTexture(NativeBackendStage::Pixel,0,target.backend_surface->texture());
  recorder.SetVertexBuffer(0,*target.convert_vertices,8,0);
  recorder.SetTopology(NativeBackendTopology::TriangleList);
  recorder.Draw(3,0);
}
void ResolveNativeRenderTarget(NativeBackendRecorder& recorder, NativeRenderTarget& target) {
  if (target.content_valid) {
    if (target.convert_pipeline) RecordConversion(recorder,target);
    else if (target.sampled.backend)
      recorder.ResolveTarget(*target.sampled.backend,*target.backend_surface);
  }
  target.sampled.content_valid=target.content_valid;
}
void ResolveNativeRenderTarget(ID3D11DeviceContext& context, NativeRenderTarget& target) {
  // The caller unbinds sampled views before resolving. Multisampled color
  // resolves natively; single-sample surfaces retain the copy boundary.
  if (target.content_valid && target.convert_pipeline) {
    throw std::runtime_error("a converting target's resolve is a draw and needs a recorder, not a context");
  } else if (target.content_valid) {
    D3D11_TEXTURE2D_DESC desc{}; target.surface->GetDesc(&desc);
    if(desc.SampleDesc.Count>1)
      context.ResolveSubresource(target.sampled.resource.Get(),0,target.surface.Get(),0,desc.Format);
    else context.CopyResource(target.sampled.resource.Get(),target.surface.Get());
  }
  target.sampled.content_valid = target.content_valid;
}
NativeTexture CreateNativeDdsTexture(NativeRenderBackend& backend, std::span<const uint8_t> data) {
  // Everything about what the file contains is decided by the shared decoder,
  // so a second backend creating the same texture cannot reach a different
  // answer. Nothing below is D3D11's any more either: the creation goes
  // through the seam, and the D3D11 handles kept at the end are the same
  // objects, for the paths that still sample them directly.
  const auto decoded = DecodeNativeDdsTexture(data);
  NativeTexture result;
  result.width = decoded.width;
  result.height = decoded.height;
  result.mip_count = decoded.mip_count;
  result.format = decoded.format;
  result.cube = decoded.cube;

  // The seam takes one tightly packed block in subresource order - every
  // level of face 0, then face 1 - which is the order the decode produces, so
  // the levels are simply concatenated. A block-compressed level points into
  // the source file and a converted one into the decode's own storage; both
  // outlive this copy.
  size_t total = 0;
  for (const auto& level : decoded.levels) total += level.bytes.size();
  std::vector<uint8_t> contents;
  contents.reserve(total);
  for (const auto& level : decoded.levels)
    contents.insert(contents.end(), level.bytes.begin(), level.bytes.end());

  NativeBackendTextureDesc desc{};
  desc.width = decoded.width;
  desc.height = decoded.height;
  desc.levels = decoded.mip_count;
  desc.array_size = decoded.faces;
  desc.cube = decoded.cube;
  desc.format = decoded.format;
  result.backend = backend.CreateTexture(desc, contents);
  if (!result.backend) throw std::runtime_error("native DDS texture creation failed");
  // The same objects, for the paths that still sample through D3D11. Null on a
  // backend that is not D3D11, which is not an error here - it is an error only
  // for a caller that then tries to bind the view, and those callers are what
  // the rest of this port removes.
  result.resource = NativeD3D11TextureResource(*result.backend);
  result.view = NativeD3D11TextureView(*result.backend);
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
std::array<float,4> ReadNativeColorPixel(NativeRenderBackend& backend,NativeBackendTexture& texture,
    uint32_t format,uint32_t x,uint32_t y) {
  if((format!=DXGI_FORMAT_R8G8B8A8_UNORM && format!=DXGI_FORMAT_R16G16B16A16_FLOAT) ||
     x>=texture.width() || y>=texture.height())
    throw std::runtime_error("unsupported backend diagnostic pixel format/coordinates");
  const auto pixels=backend.ReadTexture(texture);
  const size_t stride=format==DXGI_FORMAT_R8G8B8A8_UNORM?4:8;
  const size_t offset=(size_t(y)*texture.width()+x)*stride;
  if(offset+stride>pixels.size()) throw std::runtime_error("backend diagnostic pixel readback is incomplete");
  std::array<float,4> result{};
  for(size_t i=0;i<4;++i) {
    if(stride==4) result[i]=pixels[offset+i]/255.f;
    else {
      uint16_t bits=0; std::memcpy(&bits,pixels.data()+offset+i*2,2);
      const int exponent=(bits>>10)&31,mantissa=bits&1023;
      const float value=exponent==31 ? (mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity()) :
        exponent ? std::ldexp(float(1024+mantissa),exponent-25) : std::ldexp(float(mantissa),-24);
      result[i]=bits&0x8000 ? -value : value;
    }
  }
  return result;
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
// Packed pixels to a 24-bit BMP. Split out from the D3D11 capture because the
// encoding has nothing to do with how the bytes were fetched, and a scene drawn
// on another backend can only be fetched through the seam.
//
// Non-finite HDR is written as magenta rather than clamped: a NaN that comes
// out black looks like geometry that did not draw.
std::vector<uint8_t> EncodeNativeBmp(std::span<const uint8_t> pixels,uint32_t width,uint32_t height,
                                     uint32_t format) {
  if((format!=DXGI_FORMAT_R16G16B16A16_FLOAT && format!=DXGI_FORMAT_R8G8B8A8_UNORM) ||
     !width || !height || width>4096 || height>4096)
    throw std::runtime_error("unsupported native diagnostic format");
  const size_t source_pitch=size_t(width)*(format==DXGI_FORMAT_R8G8B8A8_UNORM?4:8);
  if(pixels.size()<source_pitch*height) throw std::runtime_error("short native diagnostic readback");
  const uint32_t pitch=(width*3+3)&~3u;
  std::vector<uint8_t> bmp(54+size_t(pitch)*height,0);
  auto word=[&](size_t at,uint32_t value) {
    for (unsigned b=0;b<4;++b) bmp[at+b]=uint8_t(value>>(8*b));
  };
  bmp[0]='B'; bmp[1]='M'; word(2,static_cast<uint32_t>(bmp.size())); word(10,54); word(14,40);
  word(18,width); word(22,0u-height); bmp[26]=1; bmp[28]=24; word(34,pitch*height);
  auto half=[](uint16_t bits) {
    const int exponent=(bits>>10)&31,mantissa=bits&1023;
    if (exponent==31) return std::numeric_limits<float>::quiet_NaN();
    const float value=exponent ? std::ldexp(float(1024+mantissa),exponent-25) : std::ldexp(float(mantissa),-24);
    return bits&0x8000?-value:value;
  };
  for (uint32_t y=0;y<height;++y) {
    const auto* source=pixels.data()+size_t(y)*source_pitch;
    for (uint32_t x=0;x<width;++x) {
      auto* bgr=bmp.data()+54+size_t(y)*pitch+size_t(x)*3;
      if (format==DXGI_FORMAT_R8G8B8A8_UNORM) {
        const auto* rgba=source+size_t(x)*4;
        bgr[0]=rgba[2]; bgr[1]=rgba[1]; bgr[2]=rgba[0];
        continue;
      }
      const auto* row=reinterpret_cast<const uint16_t*>(source);
      const float r=half(row[x*4]),g=half(row[x*4+1]),b=half(row[x*4+2]);
      if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) { bgr[0]=255; bgr[1]=0; bgr[2]=255; }
      else {
        bgr[0]=uint8_t(std::clamp(b,0.0f,1.0f)*255+.5f);
        bgr[1]=uint8_t(std::clamp(g,0.0f,1.0f)*255+.5f);
        bgr[2]=uint8_t(std::clamp(r,0.0f,1.0f)*255+.5f);
      }
    }
  }
  return bmp;
}
std::vector<uint8_t> CaptureNativeBmp(NativeRenderBackend& backend,NativeBackendRenderTarget& target,
                                      uint32_t format) {
  return EncodeNativeBmp(backend.ReadRenderTarget(target),target.width(),target.height(),format);
}
std::vector<uint8_t> CaptureNativeBmp(NativeRenderBackend& backend,NativeBackendTexture& texture,
                                      uint32_t format) {
  return EncodeNativeBmp(backend.ReadTexture(texture),texture.width(),texture.height(),format);
}
std::vector<uint8_t> CaptureNativeHdrBmp(ID3D11DeviceContext& context,ID3D11Texture2D& surface) {
  if(auto resolved=ResolveDiagnosticColor(context,surface))
    return CaptureNativeHdrBmp(context,*resolved.Get());
  D3D11_TEXTURE2D_DESC desc{}; surface.GetDesc(&desc);
  if ((desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) || desc.SampleDesc.Count!=1 ||
      desc.ArraySize!=1 || desc.MipLevels!=1 || !desc.Width || !desc.Height ||
      desc.Width>4096 || desc.Height>4096) throw std::runtime_error("unsupported native diagnostic surface");
  // Read here, encoded by the shared encoder: the fetch is what differs
  // between the two captures, not the picture.
  const size_t row_bytes=size_t(desc.Width)*(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM?4:8);
  desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  Microsoft::WRL::ComPtr<ID3D11Device> device; context.GetDevice(&device);
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  if (FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) throw std::runtime_error("native capture staging failed");
  context.CopyResource(staging.Get(),&surface);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (FAILED(context.Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) throw std::runtime_error("native capture mapping failed");
  std::vector<uint8_t> pixels(row_bytes*desc.Height);
  for (UINT y=0;y<desc.Height;++y)
    std::memcpy(pixels.data()+size_t(y)*row_bytes,
                static_cast<const uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch,row_bytes);
  context.Unmap(staging.Get(),0);
  return EncodeNativeBmp(pixels,desc.Width,desc.Height,desc.Format);
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
