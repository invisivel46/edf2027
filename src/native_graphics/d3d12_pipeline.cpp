#include "d3d12_pipeline.h"
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace edf::native {
namespace {
void Require(HRESULT result, const char* what) {
  if(SUCCEEDED(result)) return;
  char code[16];
  std::snprintf(code,sizeof(code),"0x%08lx",static_cast<unsigned long>(result));
  throw std::runtime_error(std::string("D3D12 ")+what+" failed: "+code);
}
}  // namespace

// The shared decode hands back neutral values; they are usable as D3D12
// enumerators only because they are numerically the same. Proved here, exactly
// as the D3D11 side proves it, so a divergence breaks the build instead of the
// picture.
static_assert(kNativeBlendZero==D3D12_BLEND_ZERO && kNativeBlendOne==D3D12_BLEND_ONE);
static_assert(kNativeBlendSrcColor==D3D12_BLEND_SRC_COLOR && kNativeBlendInvSrcColor==D3D12_BLEND_INV_SRC_COLOR);
static_assert(kNativeBlendSrcAlpha==D3D12_BLEND_SRC_ALPHA && kNativeBlendInvSrcAlpha==D3D12_BLEND_INV_SRC_ALPHA);
static_assert(kNativeBlendDestAlpha==D3D12_BLEND_DEST_ALPHA && kNativeBlendInvDestAlpha==D3D12_BLEND_INV_DEST_ALPHA);
static_assert(kNativeBlendDestColor==D3D12_BLEND_DEST_COLOR && kNativeBlendInvDestColor==D3D12_BLEND_INV_DEST_COLOR);
static_assert(kNativeBlendSrcAlphaSat==D3D12_BLEND_SRC_ALPHA_SAT);
static_assert(kNativeBlendFactor==D3D12_BLEND_BLEND_FACTOR && kNativeBlendInvFactor==D3D12_BLEND_INV_BLEND_FACTOR);
static_assert(kNativeBlendOpAdd==D3D12_BLEND_OP_ADD && kNativeBlendOpSubtract==D3D12_BLEND_OP_SUBTRACT);
static_assert(kNativeBlendOpRevSubtract==D3D12_BLEND_OP_REV_SUBTRACT);
static_assert(kNativeBlendOpMin==D3D12_BLEND_OP_MIN && kNativeBlendOpMax==D3D12_BLEND_OP_MAX);
static_assert(kNativeFillWireframe==D3D12_FILL_MODE_WIREFRAME && kNativeFillSolid==D3D12_FILL_MODE_SOLID);
static_assert(kNativeCullNone==D3D12_CULL_MODE_NONE && kNativeCullFront==D3D12_CULL_MODE_FRONT &&
              kNativeCullBack==D3D12_CULL_MODE_BACK);
static_assert(kNativeDepthWriteZero==D3D12_DEPTH_WRITE_MASK_ZERO && kNativeDepthWriteAll==D3D12_DEPTH_WRITE_MASK_ALL);

void ValidateAgainstRootLayout(std::span<const uint8_t> bytecode, bool pixel, const std::string& name) {
  ComPtr<ID3D12ShaderReflection> reflection;
  Require(D3DReflect(bytecode.data(),bytecode.size(),IID_PPV_ARGS(&reflection)),"shader reflection");
  D3D12_SHADER_DESC description{};
  Require(reflection->GetDesc(&description),"shader description");
  const uint32_t constant_buffers=pixel?NativeD3D12RootLayout::kPixelConstantBuffers
                                       :NativeD3D12RootLayout::kVertexConstantBuffers;
  const uint32_t textures=pixel?NativeD3D12RootLayout::kPixelTextures:0;
  const uint32_t samplers=pixel?NativeD3D12RootLayout::kPixelSamplers:0;
  const auto refuse=[&](const char* kind,UINT slot,uint32_t allowed) {
    throw std::runtime_error(name+" binds "+kind+" slot "+std::to_string(slot)+
                             " but the root signature declares "+std::to_string(allowed)+
                             ((pixel?" pixel ":" vertex ")+std::string(kind)+" slots"));
  };
  for(UINT index=0;index<description.BoundResources;++index) {
    D3D12_SHADER_INPUT_BIND_DESC binding{};
    Require(reflection->GetResourceBindingDesc(index,&binding),"resource binding");
    switch(binding.Type) {
      case D3D_SIT_CBUFFER:
        if(binding.BindPoint>=constant_buffers) refuse("constant buffer",binding.BindPoint,constant_buffers);
        break;
      case D3D_SIT_TEXTURE:
        if(binding.BindPoint>=textures) refuse("texture",binding.BindPoint,textures);
        break;
      case D3D_SIT_SAMPLER:
        if(binding.BindPoint>=samplers) refuse("sampler",binding.BindPoint,samplers);
        break;
      default:
        // Anything else needs a root parameter that does not exist. Better to
        // say so than to build a pipeline whose binding silently goes nowhere.
        throw std::runtime_error(name+" binds resource type "+std::to_string(binding.Type)+
                                 " at slot "+std::to_string(binding.BindPoint)+
                                 ", which this root signature does not declare");
    }
  }
}

ComPtr<ID3D12RootSignature> CreateNativeD3D12RootSignature(ID3D12Device& device) {
  D3D12_DESCRIPTOR_RANGE ranges[2]{};
  ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,NativeD3D12RootLayout::kPixelTextures,0,0,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
  ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,NativeD3D12RootLayout::kPixelSamplers,0,0,
             D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};

  D3D12_ROOT_PARAMETER parameters[NativeD3D12RootLayout::kParameterCount]{};
  const auto constant_buffer=[](UINT slot,D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor={slot,0};
    parameter.ShaderVisibility=visibility;
    return parameter;
  };
  parameters[NativeD3D12RootLayout::kVertexConstants0]=constant_buffer(0,D3D12_SHADER_VISIBILITY_VERTEX);
  parameters[NativeD3D12RootLayout::kVertexConstants1]=constant_buffer(1,D3D12_SHADER_VISIBILITY_VERTEX);
  parameters[NativeD3D12RootLayout::kPixelConstants0]=constant_buffer(0,D3D12_SHADER_VISIBILITY_PIXEL);
  for(uint32_t index=0;index<2;++index) {
    auto& parameter=parameters[NativeD3D12RootLayout::kPixelTextureTable+index];
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable={1,&ranges[index]};
    parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  }

  D3D12_ROOT_SIGNATURE_DESC desc{};
  desc.NumParameters=NativeD3D12RootLayout::kParameterCount;
  desc.pParameters=parameters;
  // Denying the stages this renderer does not use is not tidiness: it lets the
  // driver skip publishing root arguments to them on every draw.
  desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|
             D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|
             D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|
             D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

  ComPtr<ID3DBlob> serialized,errors;
  if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors))) {
    const std::string detail=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                                errors->GetBufferSize()):std::string("no detail");
    throw std::runtime_error("D3D12 root signature serialization failed: "+detail);
  }
  ComPtr<ID3D12RootSignature> signature;
  Require(device.CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),
                                     IID_PPV_ARGS(&signature)),"root signature creation");
  return signature;
}

NativeD3D12PipelineCache::NativeD3D12PipelineCache(ID3D12Device& device, ID3D12RootSignature& signature)
    : device_(&device),signature_(&signature) {}

ID3D12PipelineState& NativeD3D12PipelineCache::Get(const Request& request) {
  std::string key(reinterpret_cast<const char*>(&request.key),sizeof(request.key));
  if(const auto found=pipelines_.find(key);found!=pipelines_.end()) { ++hits_; return *found->second.Get(); }

  const auto decoded=DecodeNativeRenderState(request.state);
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
  desc.pRootSignature=signature_;
  desc.VS=request.vertex;
  desc.PS=request.pixel;
  desc.SampleMask=UINT_MAX;
  desc.PrimitiveTopologyType=static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(request.key.topology);
  desc.NumRenderTargets=request.key.render_targets;
  for(uint32_t index=0;index<request.key.render_targets && index<8;++index)
    desc.RTVFormats[index]=static_cast<DXGI_FORMAT>(request.key.rtv_format[index]);
  desc.DSVFormat=static_cast<DXGI_FORMAT>(request.key.dsv_format);
  desc.SampleDesc={request.key.sample_count,request.key.sample_quality};
  desc.InputLayout={request.input_layout.data(),static_cast<UINT>(request.input_layout.size())};

  auto& target=desc.BlendState.RenderTarget[0];
  target.BlendEnable=decoded.blend_enable;
  target.SrcBlend=static_cast<D3D12_BLEND>(decoded.src_color);
  target.DestBlend=static_cast<D3D12_BLEND>(decoded.dst_color);
  target.BlendOp=static_cast<D3D12_BLEND_OP>(decoded.color_op);
  target.SrcBlendAlpha=static_cast<D3D12_BLEND>(decoded.src_alpha);
  target.DestBlendAlpha=static_cast<D3D12_BLEND>(decoded.dst_alpha);
  target.BlendOpAlpha=static_cast<D3D12_BLEND_OP>(decoded.alpha_op);
  target.LogicOp=D3D12_LOGIC_OP_NOOP;
  target.RenderTargetWriteMask=decoded.write_mask;

  desc.DepthStencilState.DepthEnable=decoded.depth_enable;
  desc.DepthStencilState.DepthWriteMask=decoded.depth_write?D3D12_DEPTH_WRITE_MASK_ALL
                                                           :D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthStencilState.DepthFunc=static_cast<D3D12_COMPARISON_FUNC>(decoded.depth_func);

  desc.RasterizerState.FillMode=static_cast<D3D12_FILL_MODE>(decoded.fill);
  desc.RasterizerState.CullMode=static_cast<D3D12_CULL_MODE>(decoded.cull);
  desc.RasterizerState.FrontCounterClockwise=decoded.front_counter_clockwise;
  desc.RasterizerState.DepthClipEnable=decoded.depth_clip;
  // decoded.scissor is intentionally unused here: in D3D12 the scissor
  // rectangle is a command, not pipeline state, so the recorder applies it.

  ComPtr<ID3D12PipelineState> pipeline;
  Require(device_->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline)),"pipeline state creation");
  ++misses_;
  auto& stored=pipelines_.emplace(std::move(key),std::move(pipeline)).first->second;
  return *stored.Get();
}
}  // namespace edf::native
