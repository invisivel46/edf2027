#include "native_graphics/d3d11_quads.h"
#include "native_graphics/d3d11_texture.h"
#include "native_graphics/d3d11_render_state.h"
#include "native_graphics/d3d11_mesh.h"
#include "native_graphics/d3d11_backend.h"
#include "native_graphics/native_declarations.h"
#include "native_graphics/native_generated_indices.h"
#include "native_graphics/native_model_buffers.h"
#include "native_graphics/native_render_state_snapshot.h"
#include "native_graphics/d3d11_bindings.h"
#include "native_graphics/utility_layout.h"
#include "native_graphics/triangle_strip.h"
#include "native_graphics/immediate_mesh_key.h"
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <thread>
#include <cmath>
#include <limits>

using namespace edf::native;
using Microsoft::WRL::ComPtr;
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<uint8_t> Guest(std::span<const float> values) {
  std::vector<uint8_t> bytes;
  for (float value : values) {
    const auto word = std::bit_cast<uint32_t>(value);
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(uint8_t(word >> shift));
  }
  return bytes;
}
// What a GPU front end assembles from a recorded draw: each primitive's vertex
// bytes in assembly order. The bound index buffer's values are given by the
// test, which generated them; a backend buffer cannot be read back here.
struct AssemblyRecorder final : NativeBackendRecorder {
  std::vector<uint8_t> vertices;
  uint32_t stride=0,draws=0,indexed_draws=0;
  NativeBackendTopology topology=NativeBackendTopology::TriangleList;
  std::vector<uint32_t> index_values;
  std::vector<std::vector<uint8_t>> primitives;
  void Assemble(const std::vector<uint32_t>& order) {
    const size_t width=topology==NativeBackendTopology::LineList?2:3;
    if(topology!=NativeBackendTopology::LineList && topology!=NativeBackendTopology::TriangleList)
      throw std::runtime_error("assembly: list topologies only");
    for(size_t first=0;first+width<=order.size();first+=width) {
      std::vector<uint8_t> primitive{uint8_t(topology)};
      for(size_t i=first;i<first+width;++i) {
        const size_t at=size_t(order[i])*stride;
        if(at+stride>vertices.size()) throw std::runtime_error("assembly: vertex outside its stream");
        primitive.insert(primitive.end(),vertices.begin()+at,vertices.begin()+at+stride);
      }
      primitives.push_back(std::move(primitive));
    }
  }
  void SetTransientVertices(uint32_t slot,std::span<const uint8_t> bytes,uint32_t s) override {
    if(slot) throw std::runtime_error("assembly: slot 0 only");
    vertices.assign(bytes.begin(),bytes.end()); stride=s;
  }
  void SetTopology(NativeBackendTopology t) override { topology=t; }
  void Draw(uint32_t count,uint32_t first) override {
    ++draws; std::vector<uint32_t> order(count);
    for(uint32_t i=0;i<count;++i) order[i]=first+i;
    Assemble(order);
  }
  void DrawIndexed(uint32_t count,uint32_t first,int32_t base) override {
    ++indexed_draws; std::vector<uint32_t> order(count);
    for(uint32_t i=0;i<count;++i) order[i]=uint32_t(int64_t(index_values.at(size_t(first)+i))+base);
    Assemble(order);
  }
  void SetPipeline(NativeBackendPipeline&) override {}
  void SetVertexBuffer(uint32_t,NativeBackendBuffer&,uint32_t,uint32_t) override { throw std::runtime_error("assembly: transient only"); }
  void SetIndexBuffer(NativeBackendBuffer&,NativeBackendIndexFormat,uint32_t) override {}
  void SetBlendFactor(const std::array<float,4>&) override {}
  void SetConstants(NativeBackendStage,uint32_t,std::span<const uint8_t>) override {}
  void SetTexture(NativeBackendStage,uint32_t,NativeBackendTexture*) override {}
  void SetSampler(NativeBackendStage,uint32_t,NativeBackendSampler*) override {}
  void SetRenderTargets(std::span<NativeBackendRenderTarget* const>,NativeBackendRenderTarget*) override {}
  void SetViewport(const NativeBackendViewport&) override {}
  void SetScissor(const NativeBackendScissor&,bool) override {}
  void ClearColor(NativeBackendRenderTarget&,const std::array<float,4>&) override {}
  void ClearDepthStencil(NativeBackendRenderTarget&,bool,bool,float,uint8_t) override {}
  void DrawIndexedInstanced(uint32_t,uint32_t,uint32_t,int32_t,uint32_t) override { throw std::runtime_error("assembly: no instancing"); }
  void CopyTexture(NativeBackendTexture&,NativeBackendTexture&) override {}
  void CopyToShared(NativeBackendSharedSurface&,NativeBackendRenderTarget&) override {}
  void ResolveTarget(NativeBackendTexture&,NativeBackendRenderTarget&) override {}
  void UpdateBuffer(NativeBackendBuffer&,uint32_t,std::span<const uint8_t>) override { throw std::runtime_error("assembly: no buffer updates"); }
  void UpdateTexture(NativeBackendTexture&,std::span<const uint8_t>) override {}
  void BeginQuery(NativeBackendQuery&) override {}
  void EndQuery(NativeBackendQuery&) override {}
  void PushState() override {}
  void PopState() override {}
};
int main() {
  try {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)),"device creation");
    // Mesh storage comes from the backend now; adopting this device keeps it
    // usable by the direct D3D11 draws the rest of this test makes.
    std::shared_ptr<NativeRenderBackend> backend=AdoptNativeD3D11Backend(*device.Get(),*context.Get());
    Require(bool(backend),"adopted backend");
    Effect effect;
    effect.source = R"(
struct Varying { float4 position : SV_POSITION; float2 coord : TEXCOORD0; };
Varying VS(float2 position : POSITION0, float2 uv : TEXCOORD0) {
  Varying result; result.position=float4(position,0,1); result.coord=uv; return result;
}
float4 PS(Varying input) : SV_TARGET { return float4(input.coord,0,1); }
)";
    auto vs = CompileNativeShader(*device.Get(),effect,{false,"VS","vs_5_0"},"quad.fx");
    auto ps = CompileNativeShader(*device.Get(),effect,{true,"PS","ps_5_0"},"quad.fx");
    ValidateNativeShaderLink(vs,ps);
    Effect mismatched;
    mismatched.source = "float4 PS(float2 coord : TEXCOORD0) : SV_TARGET { return float4(coord,0,1); }";
    auto incompatible = CompileNativeShader(*device.Get(),mismatched,{true,"PS","ps_5_0"},"incompatible.fx");
    bool rejected_link = false;
    try { ValidateNativeShaderLink(vs,incompatible); }
    catch (const std::runtime_error&) { rejected_link = true; }
    Require(rejected_link,"mismatched varying registers accepted");
    QuadStream stream(*device.Get(),vs);
    auto target = CreateNativeRenderTarget(*backend,4,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto* rtv = target.target.Get();
    context->OMSetRenderTargets(1,&rtv,nullptr);
    context->VSSetShader(vs.vertex.Get(),nullptr,0);
    context->PSSetShader(ps.pixel.Get(),nullptr,0);
    const D3D11_VIEWPORT viewport{0,0,4,2,0,1}; context->RSSetViewports(1,&viewport);
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = true;
    ComPtr<ID3D11RasterizerState> raster_state;
    Require(SUCCEEDED(device->CreateRasterizerState(&raster,&raster_state)),"raster state");
    context->RSSetState(raster_state.Get());
    D3D11_TEXTURE2D_DESC desc{}; target.sampled.resource->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    Require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)),"readback creation");
    const std::array<float,16> quad{-1,1,0,0, 1,1,1,0, 1,-1,1,1, -1,-1,0,1};
    auto bytes = Guest(quad);
    auto reduction_vs=vs,reduction_ps=ps;
    reduction_vs.entry.name="VS_Main"; reduction_ps.entry.name="PS_Downsample";
    reduction_vs.source_fingerprint=reduction_ps.source_fingerprint=0x6b7926f9747c6933ull;
    reduction_vs.source_bytes=reduction_ps.source_bytes=6292;
    const RenderStateWords reduction_state{0x10001,0x700760,0x18000,0x87000006,15,0};
    const auto reduction_viewport=MakeNativeDrawViewport(0,0,4,2,0,1,false,{});
    auto initializes=[&](const NativeShader& v,const NativeShader& p,std::span<const uint8_t> data,
                         const NativeViewportState& view,const RenderStateWords& state,bool inputs=true) {
      return CanInitializeReductionTarget(v,p,data,view,state,4,2,inputs);
    };
    Require(initializes(reduction_vs,reduction_ps,bytes,reduction_viewport,reduction_state),"verified reduction overwrite rejected");
    for (const auto* entry:{"PS_Downsample_Mono","PS_Downsample_Tone","PS_Tone","PS_GaussBlur",
                            "PS_GaussBlur_Illuminance","PS_Base","PS_Bloom"}) {
      auto luminance_ps=reduction_ps; luminance_ps.entry.name=entry;
      Require(initializes(reduction_vs,luminance_ps,bytes,reduction_viewport,reduction_state),"verified luminance overwrite rejected");
      Require(!initializes(reduction_vs,luminance_ps,bytes,reduction_viewport,reduction_state,false),"uninitialized tone history accepted");
    }
    Require(!initializes(vs,reduction_ps,bytes,reduction_viewport,reduction_state),"unknown vertex contract accepted");
    Require(!initializes(reduction_vs,ps,bytes,reduction_viewport,reduction_state),"unknown pixel contract accepted");
    Require(!initializes(reduction_vs,reduction_ps,bytes,reduction_viewport,reduction_state,false),"missing reduction input accepted");
    auto modified=reduction_ps; ++modified.source_fingerprint;
    Require(!initializes(reduction_vs,modified,bytes,reduction_viewport,reduction_state),"modified shader accepted");
    modified=reduction_ps; ++modified.source_bytes;
    Require(!initializes(reduction_vs,modified,bytes,reduction_viewport,reduction_state),"different source length accepted");
    for (auto change:std::array<std::pair<size_t,uint32_t>,6>{{{0,0x1000101},{1,0x700762},{2,0x18002},{3,24},{4,7},{5,1}}}) {
      auto state=reduction_state; state[change.first]=change.second;
      Require(!initializes(reduction_vs,reduction_ps,bytes,reduction_viewport,state),"partial/conditional render state accepted");
    }
    auto partial_view=reduction_viewport; partial_view.viewport.Width=3;
    Require(!initializes(reduction_vs,reduction_ps,bytes,partial_view,reduction_state),"partial viewport accepted");
    partial_view=reduction_viewport; partial_view.reverse_depth=true;
    Require(!initializes(reduction_vs,reduction_ps,bytes,partial_view,reduction_state),"unverified reversed contract accepted");
    auto partial_quad=quad; partial_quad[0]=0;
    Require(!initializes(reduction_vs,reduction_ps,Guest(partial_quad),reduction_viewport,reduction_state),"partial quad accepted");
    Require(!initializes(reduction_vs,reduction_ps,std::span(bytes).first(63),reduction_viewport,reduction_state),"truncated quad contract accepted");
    ShaderBindings no_textures(*device.Get(),vs);
    Require(no_textures.HasAllTextureInputs(),"texture-free shader has missing inputs");
    std::function<void()> submit = [&] { stream.Draw(*context.Get(),bytes); };
    auto verify = [&](bool flipped,const D3D11_RECT* clip = nullptr) {
      const float clear[]{-2,-2,-2,-2}; context->ClearRenderTargetView(rtv,clear);
      submit();
      target.content_valid = true; ResolveNativeRenderTarget(*context.Get(),target);
      context->CopyResource(readback.Get(),target.sampled.resource.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      Require(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback mapping");
      const uint16_t xs[]{0x3000,0x3600,0x3900,0x3b00}, ys[]{0x3400,0x3a00};
      bool correct = true;
      for (unsigned y = 0; y < 2; ++y) {
        const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch);
        for (unsigned x = 0; x < 4; ++x) {
          const bool visible = !clip || (LONG(x) >= clip->left && LONG(x) < clip->right && LONG(y) >= clip->top && LONG(y) < clip->bottom);
          const bool pixel_ok = visible ?
            row[x*4] == xs[flipped ? 3-x : x] && row[x*4+1] == ys[flipped ? 1-y : y] && row[x*4+2] == 0 && row[x*4+3] == 0x3c00 :
            row[x*4] == 0xc000 && row[x*4+1] == 0xc000 && row[x*4+2] == 0xc000 && row[x*4+3] == 0xc000;
          if (!pixel_ok) std::cerr << "pixel " << x << ',' << y << " flipped=" << flipped << " actual=" << std::hex
                                  << row[x*4] << ',' << row[x*4+1] << ',' << row[x*4+2] << ',' << row[x*4+3] << std::dec << '\n';
          correct &= pixel_ok;
        }
      }
      context->Unmap(readback.Get(),0);
      Require(correct,"quad coverage or UV interpolation differs");
    };
    verify(false);
    const std::array<float,16> strip{-1,1,0,0, 1,1,1,0, -1,-1,0,1, 1,-1,1,1};
    auto strip_bytes=Guest(strip);
    submit=[&] { stream.DrawTriangleStrip(*context.Get(),strip_bytes); }; verify(false);
    bool rejected_strip=false;
    try { stream.DrawTriangleStrip(*context.Get(),std::span(strip_bytes).first(32)); }
    catch (const std::runtime_error&) { rejected_strip=true; }
    Require(rejected_strip,"two-vertex strip accepted");
    rejected_strip=false;
    try { stream.DrawTriangleStrip(*context.Get(),std::span(strip_bytes).first(63)); }
    catch (const std::runtime_error&) { rejected_strip=true; }
    Require(rejected_strip,"misaligned strip accepted");
    submit=[&] { stream.Draw(*context.Get(),bytes); }; verify(false);
    auto flipped = quad;
    for (unsigned vertex = 0; vertex < 4; ++vertex) {
      flipped[vertex*4+2] = 1-flipped[vertex*4+2]; flipped[vertex*4+3] = 1-flipped[vertex*4+3];
    }
    bytes = Guest(flipped); verify(true); // Reuses dynamic buffer with new data.
    const auto extra = bytes; bytes.insert(bytes.end(),extra.begin(),extra.end());
    verify(true); // Two quads grow the stream buffer.
    auto clipped_state = CreateNativeRenderState(*device.Get(),{0x10001,0,0,0,15,1});
    clipped_state.Bind(*context.Get());
    bytes = Guest(quad);
    auto clipped_view = MakeNativeViewport(0,0,4,2,0,1,true,{1,-10,3,1});
    Require(clipped_view.scissor.top == 0,"scissor not intersected with viewport");
    clipped_view.Bind(*context.Get()); verify(false,&clipped_view.scissor);
    auto empty_view = MakeNativeViewport(0,0,4,2,0,1,true,{8,0,9,2});
    empty_view.Bind(*context.Get()); verify(false,&empty_view.scissor);
    auto unclipped_state = CreateNativeRenderState(*device.Get(),{0x10001,0,0,0,15,0});
    unclipped_state.Bind(*context.Get());
    auto unclipped_view = MakeNativeViewport(0,0,4,2,0,1,false,{8,0,9,2});
    unclipped_view.Bind(*context.Get()); verify(false);
    {
      NativeRenderStateSnapshots owned;
      for(uint32_t factor_code:{12u,14u}) {
        auto constant_state=CreateNativeRenderState(*device.Get(),{factor_code|(14u<<16),0,0,0,15,0});
        Require(constant_state.requires_blend_factor,"constant blend fixture did not select factor path");
        for(const auto factor:{std::array<float,4>{.25f,.5f,.75f,.5f},
                               std::array<float,4>{.75f,.25f,.5f,.25f}}) {
          NativeRenderStateSnapshots::BlendWords words{};
          for(size_t i=0;i<4;++i) words[i]=std::bit_cast<uint32_t>(factor[i]);
          owned.PublishBlend(7,words,0x82135418);
          const auto published=owned.RequireBlend(7);
          std::array<float,4> native{};
          for(size_t i=0;i<4;++i) native[i]=std::bit_cast<float>(published[i]);
          constant_state.Bind(*context.Get(),native);
          const float clear[]{0,0,0,0}; context->ClearRenderTargetView(rtv,clear);
          stream.Draw(*context.Get(),bytes);
          target.content_valid=true; ResolveNativeRenderTarget(*context.Get(),target);
          const auto pixel=ReadNativeColorPixel(*context.Get(),*target.sampled.resource.Get(),0,0);
          const auto expected=factor_code==14?std::array<float,4>{factor[3],factor[3],factor[3],factor[3]}:factor;
          Require(std::abs(pixel[0]-.125f*expected[0])<.001f &&
            std::abs(pixel[1]-.25f*expected[1])<.001f && std::abs(pixel[2])<.001f &&
            std::abs(pixel[3]-expected[3])<.001f,"owned blend factor GPU output differs");
        }
      }
      unclipped_state.Bind(*context.Get());
    }
    bool rejected_viewport = false;
    try { MakeNativeViewport(0xffffffff,0,4,2,0,1,false,{}); }
    catch (const std::runtime_error&) { rejected_viewport = true; }
    Require(rejected_viewport,"overflowing viewport accepted");
    rejected_viewport = false;
    try { MakeNativeViewport(0,0,4,2,1,0,false,{}); }
    catch (const std::runtime_error&) { rejected_viewport = true; }
    Require(rejected_viewport,"inverted viewport depth range accepted");
    // A padded static mesh declaration differs from the immediate quad layout.
    // Its normal participates in the UV calculation so all float3 lanes matter.
    Effect mesh_effect;
    mesh_effect.source = R"(
#ifndef __DX__
#error Native compilation must enable the retail DirectX lighting path
#endif
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION0,float3 normal : NORMAL0,float2 uv : TEXCOORD0) {
  Varying result; result.position=float4(position,1); result.uv=uv*normal.xy+normal.z; return result;
}
float4 PS(Varying input) : SV_TARGET { return float4(input.uv,0,1); }
)";
    auto mesh_vs=CompileNativeShader(*device.Get(),mesh_effect,{false,"VS","vs_5_0"},"mesh.fx");
    Effect matrix_effect;
    matrix_effect.source=R"(
row_major float4x4 Rows;
column_major float4x4 Columns;
float Gain;
float4 VS(float4 position:POSITION):SV_POSITION {
  return Gain*(mul(position,Rows)+mul(position,Columns));
})";
    ShaderBindings matrix_bindings(*device.Get(),CompileNativeShader(*device.Get(),matrix_effect,
      {false,"VS","vs_5_0"},"matrix_diagnostic.fx"));
    std::array<float,16> logical_matrix{},column_storage{};
    for(size_t r=0;r<4;++r) for(size_t c=0;c<4;++c) {
      logical_matrix[r*4+c]=float(r*4+c+1);
      column_storage[c*4+r]=logical_matrix[r*4+c];
    }
    matrix_bindings.SetGuestFloatRegisters("Rows",Guest(logical_matrix));
    matrix_bindings.SetGuestFloatRegisters("Columns",Guest(column_storage));
    Require(matrix_bindings.ReadFloat4x4("Rows")==logical_matrix &&
      matrix_bindings.ReadFloat4x4("Columns")==logical_matrix,"diagnostic matrix storage layout mismatch");
    Require(!matrix_bindings.ReadFloat4x4("missing"),"missing diagnostic matrix not empty");
    bool rejected_matrix_type=false;
    try { matrix_bindings.ReadFloat4x4("Gain"); }
    catch(const std::exception&) {rejected_matrix_type=true;}
      Require(rejected_matrix_type,"diagnostic matrix accepted scalar");
      const std::array<float,4> replacement_column{101,102,103,104};
      matrix_bindings.PatchGuestFloatRegisters("Columns",1,Guest(replacement_column));
      auto patched_matrix=logical_matrix;
      for(size_t row=0;row<4;++row) patched_matrix[row*4+1]=replacement_column[row];
      Require(matrix_bindings.ReadFloat4x4("Columns")==patched_matrix,
        "partial column matrix update clobbered untouched registers");
      matrix_bindings.PatchGuestFloatRegisters("Rows",2,Guest(replacement_column));
      patched_matrix=logical_matrix;
      for(size_t column=0;column<4;++column) patched_matrix[8+column]=replacement_column[column];
      Require(matrix_bindings.ReadFloat4x4("Rows")==patched_matrix,
        "partial row matrix update clobbered untouched registers");
      bool rejected_patch=false;
      try { matrix_bindings.PatchGuestFloatRegisters("Rows",4,Guest(replacement_column)); }
      catch(const std::exception&) {rejected_patch=true;}
      Require(rejected_patch,"out of range register patch accepted");
      Require(matrix_bindings.ReadFloat4x4("Rows")==patched_matrix,"failed patch mutated matrix");
      Require(!matrix_bindings.PatchGuestFloatRegisters("missing",0,Guest(replacement_column)),
        "optimized-out patch was not ignored");
    auto mesh_ps=CompileNativeShader(*device.Get(),mesh_effect,{true,"PS","ps_5_0"},"mesh.fx");
    auto reversed_vs=CompileNativeShader(*device.Get(),mesh_effect,{false,"VS","vs_5_0"},"mesh.fx",true);
    ValidateNativeShaderLink(reversed_vs,mesh_ps);
    ValidateNativeShaderLink(mesh_vs,mesh_ps);
    auto word=[](std::vector<uint8_t>& output,size_t at,uint32_t value) {
      for (unsigned b=0;b<4;++b) output.at(at+b)=uint8_t(value>>(24-b*8));
    };
    std::vector<uint8_t> declaration(36,0),mesh_vertices(160,0xa5);
    word(declaration,0,4); word(declaration,4,0x2a23b9);
    word(declaration,12,16); word(declaration,16,0x2a23b9); declaration[21]=3;
    word(declaration,24,28); word(declaration,28,0x2c23a5); declaration[33]=5;
    for (size_t v=0;v<4;++v) {
      const std::array<float,8> attributes{quad[v*4],quad[v*4+1],0,1,1,0,quad[v*4+2],quad[v*4+3]};
      for (size_t c=0;c<8;++c) word(mesh_vertices,v*40+4+c*4,std::bit_cast<uint32_t>(attributes[c]));
    }
    std::vector<uint8_t> indices16,indices32;
    for (uint32_t index : {1,2,3,1,3,4}) {
      indices16.push_back(uint8_t(index>>8)); indices16.push_back(uint8_t(index));
      const auto at=indices32.size(); indices32.resize(at+4); word(indices32,at,index);
    }
    NativeIndexedMesh mesh16(*backend,mesh_vs,declaration,40,mesh_vertices,indices16,2);
    NativeIndexedMesh mesh32(*backend,mesh_vs,declaration,40,mesh_vertices,indices32,4);
    const auto source_probe=mesh16.CaptureSourceFloat3(0,6,-1,4);
    Require(source_probe==mesh32.CaptureSourceFloat3(0,6,-1,4) &&
      source_probe[0]==std::array<float,3>{quad[0],quad[1],0} &&
      source_probe[5]==std::array<float,3>{quad[12],quad[13],0},
      "native float3 probe lost index width, signed base or attribute offset");
    auto probe_vertices=mesh_vertices,probe_indices=indices16;
    NativeIndexedMesh probe_mesh(*backend,mesh_vs,declaration,40,probe_vertices,probe_indices,2);
    std::fill(probe_vertices.begin(),probe_vertices.end(),0);
    std::fill(probe_indices.begin(),probe_indices.end(),255);
    Require(probe_mesh.CaptureSourceFloat3(0,6,-1,4)==source_probe,
      "native float3 probe reread mutated guest inputs");
    Require(probe_mesh.CaptureSourceFloat3(1,1,-1,4)==
      std::vector<std::array<float,3>>{source_probe[1]},
      "native attribute probe incorrectly required triangle-aligned samples");
    std::vector<uint8_t> many_indices(65538*2);
    for(size_t i=0;i<65538;++i) many_indices[i*2+1]=1;
    NativeIndexedMesh large_probe(*backend,mesh_vs,declaration,40,mesh_vertices,many_indices,2);
    const auto many_samples=large_probe.CaptureSourceFloat3(1,65536,-1,4);
    Require(many_samples.size()==65536 &&
      std::all_of(many_samples.begin(),many_samples.end(),[&](const auto& value) { return value==source_probe[0]; }),
      "native attribute probe lost full brightness diagnostic sample budget");
    bool oversized_probe=false;
    try { large_probe.CaptureSourceFloat3(0,65537,-1,4); }
    catch(const std::runtime_error&) { oversized_probe=true; }
    Require(oversized_probe,"native attribute probe exceeded bounded sample budget");
    auto prefixed=std::make_shared<std::vector<uint8_t>>(40,0xcc);
    prefixed->insert(prefixed->end(),mesh_vertices.begin(),mesh_vertices.end());
    NativeIndexedMesh offset_probe(*backend,mesh_vs,declaration,40,std::span<const uint8_t>(*prefixed).subspan(40),
      indices16,2,false,{},{},NativeIndexedMesh::IndexReuse::RequireMatch,prefixed,40);
    Require(offset_probe.CaptureSourceFloat3(3,3,-1,4)==
      std::vector<std::array<float,3>>(source_probe.begin()+3,source_probe.end()),
      "native float3 probe ignored retained source offset or first index");
    for(const auto args:std::array<std::array<int32_t,4>,4>{{{0,6,-2,4},{5,3,-1,4},{0,6,-1,29},{0,6,-1,-1}}}) {
      bool rejected=false;
      try { mesh16.CaptureSourceFloat3(uint32_t(args[0]),uint32_t(args[1]),args[2],uint32_t(args[3])); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"native float3 probe accepted invalid index/base/attribute range");
    }
    mesh16.ValidateLineDraw(0,2,-1); mesh32.ValidateLineDraw(2,4,-1);
    for(const auto* line_mesh:{&mesh16,&mesh32}) {
      for(const auto range:std::array<std::array<int32_t,3>,4>{{{0,3,-1},{5,2,-1},{0,2,-2},{0,2,INT32_MAX}}}) {
        bool rejected_line=false;
        try {line_mesh->ValidateLineDraw(uint32_t(range[0]),uint32_t(range[1]),range[2]);}
        catch(const std::runtime_error&) {rejected_line=true;}
        Require(rejected_line,"invalid line topology/range/base accepted");
      }
    }
    const std::array<uint32_t,6> reference_indices{1,2,3,1,3,4};
    for(uint32_t first=0;first<8;++first) for(uint32_t count=0;count<9;++count)
      for(int32_t base:{INT32_MIN,-2,-1,0,1,INT32_MAX}) {
        bool expected=count && count%3==0 && first<=6 && count<=6-first;
        if(expected) for(size_t i=first;i<size_t(first)+count;++i)
          expected=expected && int64_t(reference_indices[i])+base>=0 && int64_t(reference_indices[i])+base<4;
        for(const auto* checked:{&mesh16,&mesh32}) {
          bool accepted=true;
          try { checked->ValidateDraw(first,count,base); } catch(const std::runtime_error&) {accepted=false;}
          Require(accepted==expected,"mesh range fast path differs from exact index validation");
        }
      }
    auto partial_indices=indices32; word(partial_indices,20,UINT32_MAX);
    NativeIndexedMesh partial_mesh(*backend,mesh_vs,declaration,40,mesh_vertices,partial_indices,4);
    partial_mesh.ValidateDraw(0,3,-1); // Invalid unused index must not reject this subset.
    bool rejected_partial=false;
    try {partial_mesh.ValidateDraw(3,3,-1);} catch(const std::runtime_error&) {rejected_partial=true;}
    Require(rejected_partial,"wide unsigned index wrapped through signed base");
    context->VSSetShader(mesh_vs.vertex.Get(),nullptr,0); context->PSSetShader(mesh_ps.pixel.Get(),nullptr,0);
    submit=[&] { mesh16.Draw(*context.Get(),0,3,-1); mesh16.Draw(*context.Get(),3,3,-1); };
    verify(false);
    submit=[&] { mesh32.Draw(*context.Get(),0,6,-1); }; verify(false);
    // Retail pole material normalizes TANGENT0 even when the mesh omits it.
    // The guest input default is zero; legacy zero multiplication keeps the
    // normalized result zero rather than propagating 0 * infinity as NaN.
    for(bool supplied:{false,true}) {
      Effect tangent_effect;
      tangent_effect.source=std::string(R"(
struct Varying { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
Varying VS(float3 position:POSITION0,float2 uv:TEXCOORD0,float3 tangent:TANGENT0) {
  Varying result; result.position=float4(position,1);
  result.uv=uv+normalize(tangent).xy
)")+(supplied?"-float2(0.7071067811865475,0.7071067811865475)":"")+R"(;
  return result;
})";
      auto tangent_vs=CompileNativeShader(*device.Get(),tangent_effect,{false,"VS","vs_5_0"},"tangent.fx");
      auto tangent_decl=declaration;
      if(supplied) tangent_decl[21]=6; // Existing finite (1,1,0) NORMAL0 bytes become TANGENT0.
      NativeIndexedMesh tangent_mesh(*backend,tangent_vs,tangent_decl,40,mesh_vertices,indices16,2);
      context->VSSetShader(tangent_vs.vertex.Get(),nullptr,0);
      submit=[&]{tangent_mesh.Draw(*context.Get(),0,6,-1);}; verify(false);
    }
    context->VSSetShader(mesh_vs.vertex.Get(),nullptr,0);
    const float capture_clear[]{-.25f,-.5f,-.75f,-1};
    context->ClearRenderTargetView(rtv,capture_clear);
    const auto before_clip_capture=CaptureNativeHdrBmp(*context.Get(),*target.surface.Get());
    for (const auto* capture_mesh:{&mesh16,&mesh32}) {
      const auto positions=capture_mesh->CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1);
      const std::array<size_t,6> expected_indices{0,1,2,0,2,3};
      Require(positions.size()==6,"clip capture vertex count");
      for(size_t i=0;i<6;++i) {
        const auto vertex=expected_indices[i];
        Require(positions[i]==std::array<float,4>{quad[vertex*4],quad[vertex*4+1],0,1},"clip capture position/index/base mismatch");
      }
    }
    context->VSSetShader(reversed_vs.vertex.Get(),nullptr,0);
    const auto reversed_positions=mesh16.CaptureClipPositions(*context.Get(),reversed_vs,3,3,-1);
    Require(reversed_positions[0][2]==1 && reversed_positions[0][3]==1,"clip capture did not use bound reversed VS");
    context->VSSetShader(mesh_vs.vertex.Get(),nullptr,0);
    bool rejected_clip_shader=false;
    try { mesh16.CaptureClipPositions(*context.Get(),reversed_vs,0,3,-1); }
    catch (const std::exception&) {rejected_clip_shader=true;}
    Require(rejected_clip_shader,"clip capture accepted mismatched VS");
    Require(CaptureNativeHdrBmp(*context.Get(),*target.surface.Get())==before_clip_capture,
      "clip capture replay changed framebuffer");
    submit=[&] {mesh16.Draw(*context.Get(),0,6,-1);}; verify(false);
    ComPtr<ID3D11Query> visibility;
    const D3D11_QUERY_DESC visibility_desc{D3D11_QUERY_OCCLUSION,0};
    Require(SUCCEEDED(device->CreateQuery(&visibility_desc,&visibility)),"visibility query creation");
    submit=[&] {
      context->Begin(visibility.Get()); mesh32.Draw(*context.Get(),0,6,-1); context->End(visibility.Get());
    };
    verify(false); // Pixel readback also completes preceding query commands.
    uint64_t passed_samples=0;
    Require(context->GetData(visibility.Get(),&passed_samples,sizeof(passed_samples),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
      passed_samples==8,"native full-quad visibility samples");
    context->Begin(visibility.Get()); context->End(visibility.Get());
    submit=[&] { mesh32.Draw(*context.Get(),0,6,-1); }; verify(false);
    Require(context->GetData(visibility.Get(),&passed_samples,sizeof(passed_samples),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK &&
      passed_samples==0,"native empty visibility query");
    NativeMeshCache mesh_cache;
    const NativeMeshCache::Key cache_key{101,102,103,104,0};
    {
      NativeDeclarations declarations;
      auto source=declaration;
      declarations.Publish(103,source);
      const auto first=declarations.Get(103);
      Require(first->count()==3 && first->Words<3>()==std::array<uint32_t,3>{4,0x2a23b9,0} &&
        first->Words<3>(24)==std::array<uint32_t,3>{28,0x2c23a5,0x50000},
        "native declaration words/count decoding differs");
      for(size_t offset:{size_t(25),size_t(-1)}) {
        bool rejected=false;
        try { first->Words<3>(offset); } catch(const std::runtime_error&) { rejected=true; }
        Require(rejected,"out-of-range native declaration word read accepted");
      }
      const auto empty=NativeDeclaration::Create({});
      Require(empty->count()==0 && empty->Words<0>().empty(),"empty native declaration mishandled");
      bool empty_rejected=false;
      try { empty->Words<1>(); } catch(const std::runtime_error&) { empty_rejected=true; }
      Require(empty_rejected,"empty native declaration read accepted");
      source[0]^=0xff;
      Require(std::equal(first->bytes().begin(),first->bytes().end(),declaration.begin()),
        "native declaration retained guest storage");
      NativeMeshCache owned_cache;
      auto acquire_owned=[&](auto identity)->NativeIndexedMesh& {
        return owned_cache.Acquire(*backend,mesh_vs,cache_key,identity->bytes(),40,mesh_vertices,indices16,2,identity);
      };
      acquire_owned(first);
      submit=[&] { acquire_owned(first).Draw(*context.Get(),0,6,-1); }; verify(false);
      Require(owned_cache.builds()==1 && owned_cache.hits()==1,"owned declaration identity failed reuse");
      Require(owned_cache.source_checks().vertex_checks==1 && owned_cache.source_checks().index_checks==1 &&
        owned_cache.source_checks().vertex_candidate_bytes==mesh_vertices.size() &&
        owned_cache.source_checks().index_candidate_bytes==indices16.size(),"source-check counters include construction or omit cache hit");
      bool rejected=false;
      try { owned_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2,first); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected,"declaration token accepted unrelated storage");
      {
        NativeMeshCache consecutive;
        auto& mesh=consecutive.Acquire(*backend,mesh_vs,cache_key,first->bytes(),40,mesh_vertices,indices16,2,first);
        const auto vertices=mesh.VertexStorage()->SourceSnapshot();
        const auto indices=mesh.IndexStorage()->SourceSnapshot();
        auto hit=[&](const auto& key,const NativeDeclaration& identity,uint32_t stride,
                     std::span<const uint8_t> v,std::span<const uint8_t> i,uint32_t width) {
          return consecutive.TryAcquireOwned(*backend,mesh_vs,key,identity,stride,v,i,width);
        };
        Require(hit(cache_key,*first,40,*vertices,*indices,2)==&mesh,"owned mesh hit missed identical sources");
        Require(consecutive.owned_hits()==1 && consecutive.hits()==1,"owned mesh hit accounting");
        auto other_key=cache_key; ++other_key[0];
        const auto other_declaration=NativeDeclaration::Create(first->bytes());
        Require(!hit(other_key,*first,40,*vertices,*indices,2),"owned hit reused another resource");
        Require(!hit(cache_key,*other_declaration,40,*vertices,*indices,2),"owned hit reused replaced declaration");
        Require(!hit(cache_key,*first,44,*vertices,*indices,2),"owned hit ignored stride change");
        Require(!hit(cache_key,*first,40,*vertices,*indices,4),"owned hit ignored index width");
        Require(!hit(cache_key,*first,40,mesh_vertices,*indices,2),"owned hit accepted unowned vertex bytes");
        Require(!hit(cache_key,*first,40,*vertices,indices16,2),"owned hit accepted unowned index bytes");
        Require(!hit(cache_key,*first,40,std::span<const uint8_t>(*vertices).subspan(4),*indices,2),
          "owned hit ignored source offset/extent");
        submit=[&] { hit(cache_key,*first,40,*vertices,*indices,2)->Draw(*context.Get(),0,6,-1); }; verify(false);
        const auto& prepared=mesh.PrepareDraw(0,6,-1);
        Require(mesh.FindPreparedDraw(0,6,-1)==&prepared && !mesh.FindPreparedDraw(0,6,0) &&
          !mesh.FindPreparedDraw(3,3,-1),"prepared draw ignored range/base identity");
        bool invalid_preparation=false;
        try { mesh.PrepareDraw(0,6,-100); } catch(const std::runtime_error&) { invalid_preparation=true; }
        Require(invalid_preparation && mesh.FindPreparedDraw(0,6,-1)==&prepared,
          "invalid range was prepared or displaced valid preparation");
        submit=[&] { backend->BeginFrame(); prepared.Draw(backend->Recorder()); backend->Submit(); }; verify(false);
        const auto retained=mesh.RetainDraw(backend,0,6,-1);
        const auto retained_layout=mesh.input_layout().fingerprint();
        bool invalid_retention=false;
        try { mesh.RetainDraw(backend,0,6,-100); } catch(const std::runtime_error&) { invalid_retention=true; }
        Require(invalid_retention,"scene geometry retained an invalid range");
        invalid_retention=false;
        try { mesh.RetainDraw({},0,6,-1); } catch(const std::runtime_error&) { invalid_retention=true; }
        Require(invalid_retention,"scene geometry omitted backend ownership");
        consecutive.Invalidate(cache_key[0]);
        Require(!hit(cache_key,*first,40,*vertices,*indices,2),"owned hit survived resource invalidation");
        consecutive.Clear();
        Require(!hit(cache_key,*first,40,*vertices,*indices,2),"owned hit survived clear");
        Require(retained.input_layout().fingerprint()==retained_layout,
          "retained scene layout borrowed destroyed mesh semantics");
        submit=[&] { backend->BeginFrame(); retained.Draw(backend->Recorder()); backend->Submit(); }; verify(false);
        // Reusing the guest resource identity must not replace an old scene's GPU generation.
        auto replacement_vertices=mesh_vertices;
        std::fill(replacement_vertices.begin(),replacement_vertices.end(),0);
        consecutive.Acquire(*backend,mesh_vs,cache_key,first->bytes(),40,replacement_vertices,indices16,2,first);
        verify(false);
        NativeIndexedMesh dynamic(*backend,mesh_vs,first->bytes(),40,mesh_vertices,indices16,2,true);
        invalid_retention=false;
        try { dynamic.RetainDraw(backend,0,6,-1); } catch(const std::runtime_error&) { invalid_retention=true; }
        Require(invalid_retention,"scene geometry retained mutable vertex storage");
      }
      rejected=false;
      try { declarations.Publish(103,std::span<const uint8_t>{declaration}.first(1)); }
      catch(const std::runtime_error&) { rejected=true; }
      Require(rejected && declarations.Get(103)==first,"malformed publication replaced live declaration");
      declarations.Retire(103);
      rejected=false;
      try { declarations.Get(103); } catch(const std::exception&) { rejected=true; }
      Require(rejected,"retired declaration remained discoverable");
      declarations.Publish(103,declaration);
      const auto replacement=declarations.Get(103);
      Require(replacement!=first,"reused handle retained declaration identity");
      acquire_owned(replacement);
      Require(owned_cache.builds()==2,"new declaration generation reused old cache entry");
      submit=[&] { acquire_owned(first).Draw(*context.Get(),0,6,-1); }; verify(false);
      Require(owned_cache.builds()==3,"old owned generation lost independent lifetime");
    }
    auto acquire=[&](std::span<const uint8_t> data)->NativeIndexedMesh& {
      return mesh_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,data,indices16,2);
    };
    acquire(mesh_vertices);
    submit=[&] { acquire(mesh_vertices).Draw(*context.Get(),0,6,-1); }; verify(false);
    Require(mesh_cache.builds()==1 && mesh_cache.hits()==1,"unchanged mesh rebuilt");
    const auto payload_bytes=mesh_cache.bytes();
    auto updated_vertices=mesh_vertices;
    for (size_t v=0;v<4;++v) for (size_t c=0;c<2;++c)
      word(updated_vertices,v*40+28+c*4,std::bit_cast<uint32_t>(1-quad[v*4+2+c]));
    submit=[&] { acquire(updated_vertices).Draw(*context.Get(),0,6,-1); }; verify(true);
    Require(mesh_cache.builds()==2,"updated vertices not rebuilt");
    {
      // Model-owned native index storage exists before any shader/mesh variant.
      auto loaded=std::make_shared<const NativeIndexBuffer>(*backend,indices16,2);
      for(const uint32_t index_stride:{2u,4u}) {
        const auto& source=index_stride==2?indices16:indices32;
        auto tracked_storage=std::make_shared<const NativeIndexBuffer>(*backend,source,index_stride);
        NativeBufferWrites writes;
        NativeModelBuffers tracked(&writes);
        tracked.Publish(1,NativeModelBuffers::Kind::Index,0xa0001000,index_stride,uint32_t(source.size()/index_stride),0x1000);
        const auto generation=tracked.Find(1,NativeModelBuffers::Kind::Index)->generation;
        const auto version=*writes.Version(1);
        writes.Record(0x1800,4,true); // Same page, outside index data.
        Require(tracked.CommitObservedIndex(1,generation,version,tracked_storage),"stable index snapshot rejected");
        NativeMeshCache tracked_cache;
        auto& tracked_mesh=tracked_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,source,index_stride,{}, {},
          tracked.Find(1,NativeModelBuffers::Kind::Index)->index_storage);
        Require(tracked_mesh.IndexStorage()==tracked_storage,"registered index format was not reused by mesh");
        submit=[&] { tracked_mesh.Draw(*context.Get(),0,6,-1); }; verify(false);
        writes.Record(0x1000,2,true);
        Require(!tracked.CommitObservedIndex(1,generation,version,tracked_storage),"changed snapshot accepted by version handshake");
        tracked.Publish(1,NativeModelBuffers::Kind::Index,0xa0001000,index_stride,uint32_t(source.size()/index_stride),0x1000);
        Require(!tracked.CommitObservedIndex(1,generation,version,tracked_storage),"reused owner accepted old snapshot");
        Require(!tracked.Find(1,NativeModelBuffers::Kind::Index)->index_storage,"rejected snapshot became retained");
        tracked.Retire(1);
        Require(!tracked.CommitObservedIndex(1,generation,version,tracked_storage),"retired owner accepted snapshot");
      }
      NativeModelBuffers owners;
      owners.Publish(cache_key[1],NativeModelBuffers::Kind::Index,0xa0001000,2,uint32_t(indices16.size()/2),0x1000,loaded);
      NativeMeshCache seeded_cache;
      auto& seeded=seeded_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2,{}, {},
        owners.Find(cache_key[1],NativeModelBuffers::Kind::Index)->index_storage);
      Require(seeded.IndexStorage()==loaded,"load-time index storage was not consumed by first mesh");
      Require(seeded_cache.published_index_reuses()==1 && seeded_cache.published_index_rejections()==0,"published index reuse accounting");
      const auto retained_vertices=seeded.VertexStorage();
      {
        NativeBufferWrites writes;
        NativeModelBuffers tracked(&writes);
        tracked.Publish(2,NativeModelBuffers::Kind::Vertex,0xa0002000,40,uint32_t(mesh_vertices.size()/40),0x2000);
        const auto generation=tracked.Find(2,NativeModelBuffers::Kind::Vertex)->generation;
        const auto version=*writes.Version(2);
        writes.Record(0x2800,4,true);
        Require(tracked.CommitObservedVertex(2,generation,version,retained_vertices),"stable vertex snapshot rejected");
        writes.Record(0x2000,4,true);
        Require(!tracked.CommitObservedVertex(2,generation,version,retained_vertices),"changed vertex snapshot accepted");
        tracked.Publish(2,NativeModelBuffers::Kind::Vertex,0xa0002000,40,uint32_t(mesh_vertices.size()/40),0x2000);
        Require(!tracked.CommitObservedVertex(2,generation,version,retained_vertices),"reused vertex owner accepted old snapshot");
        Require(!tracked.Find(2,NativeModelBuffers::Kind::Vertex)->vertex_storage,"rejected vertex snapshot retained");
        tracked.Retire(2);
        Require(!tracked.CommitObservedVertex(2,generation,version,retained_vertices),"retired vertex owner accepted snapshot");
      }
      {
        NativeMeshCache observed_cache;
        uint32_t snapshots=0;
        NativeSnapshotObserver observer{&snapshots,[](void* value) { ++*static_cast<uint32_t*>(value); }};
        auto acquire=[&](std::span<const uint8_t> vertices) -> NativeIndexedMesh& {
          return observed_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,vertices,indices16,2,{}, {},{}, {},observer);
        };
        acquire(mesh_vertices);
        Require(snapshots==1,"first construction did not notify snapshot observer");
        acquire(mesh_vertices);
        Require(snapshots==1,"unchanged cache hit invoked snapshot observer");
        acquire(updated_vertices);
        Require(snapshots==2,"changed source rebuild did not notify snapshot observer");
        acquire(updated_vertices);
        Require(snapshots==2,"rebuilt cache hit invoked snapshot observer");
      }
      {
        // A notified producer completes after the version is captured but
        // before the new mesh copies its source. The snapshot is usable by
        // the validated mesh, but must not become a certified owner payload.
        NativeBufferWrites writes;
        NativeModelBuffers tracked(&writes);
        tracked.Publish(2,NativeModelBuffers::Kind::Vertex,0xa0002000,40,uint32_t(mesh_vertices.size()/40),0x2000);
        const auto generation=tracked.Find(2,NativeModelBuffers::Kind::Vertex)->generation;
        auto source=mesh_vertices;
        std::optional<NativeBufferWrites::ObservedVersion> version;
        auto producer=[&] {
          version=writes.Version(2);
          std::copy(updated_vertices.begin(),updated_vertices.end(),source.begin());
          writes.Record(0x2000,uint32_t(source.size()),true);
        };
        NativeMeshCache observed_cache;
        auto& mesh=observed_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,source,indices16,2,{}, {},{}, {},
          {&producer,[](void* value) { (*static_cast<decltype(producer)*>(value))(); }});
        Require(mesh.VertexStorage()->MatchesSource(updated_vertices),"snapshot observer ran after construction read");
        Require(version && !tracked.CommitObservedVertex(2,generation,*version,mesh.VertexStorage()),"construction-time write escaped retention handshake");
        Require(!tracked.Find(2,NativeModelBuffers::Kind::Vertex)->vertex_storage,"construction-time write retained uncertified vertex payload");
      }
      owners.Publish(cache_key[0],NativeModelBuffers::Kind::Vertex,0xa0002000,40,uint32_t(mesh_vertices.size()/40),0x2000);
      const auto vertex_generation=owners.Find(cache_key[0],NativeModelBuffers::Kind::Vertex)->generation;
      Require(owners.RetainVertexStorage(cache_key[0],vertex_generation,retained_vertices),"model did not retain first-use vertices");
      NativeMeshCache retained_cache;
      Require(retained_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2,{}, {},loaded,
        owners.Find(cache_key[0],NativeModelBuffers::Kind::Vertex)->vertex_storage).VertexStorage()==retained_vertices,
        "independent mesh cache did not reuse model vertex allocation");
      retained_cache.Clear();
      Require(retained_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,updated_vertices,indices16,2,{}, {},loaded,
        retained_vertices).VertexStorage()!=retained_vertices,"model vertex ownership bypassed source validation");
      Require(retained_vertices->MatchesSource(mesh_vertices),"vertex update mutated retained model generation");
      Require(retained_cache.retained_vertex_reuses()==1 && retained_cache.retained_vertex_replacements()==1,
        "retained vertex reuse/replacement accounting");
      owners.NotifyUpdate(cache_key[0]);
      Require(!owners.Find(cache_key[0],NativeModelBuffers::Kind::Vertex)->vertex_storage,"completed update retained old model vertices");
      owners.Publish(cache_key[0],NativeModelBuffers::Kind::Vertex,0xa0002000,40,uint32_t(mesh_vertices.size()/40),0x2000);
      Require(!owners.RetainVertexStorage(cache_key[0],vertex_generation,retained_vertices),"reused owner accepted stale vertex generation");
      Require(!owners.RetainVertexStorage(cache_key[1],vertex_generation,retained_vertices),"index owner accepted vertex allocation");
      seeded_cache.Clear();
      Require(seeded_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2,{}, {},loaded).IndexStorage()==loaded,
        "mesh cache clear discarded model-owned index storage");
      auto mutated_indices=indices16; std::swap(mutated_indices[1],mutated_indices[3]);
      Require(seeded_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,mutated_indices,2,{}, {},loaded).IndexStorage()!=loaded,
        "load-time index storage bypassed live source validation");
      Require(loaded->Matches(*backend,indices16,2),"source update mutated retained load-time generation");
      Require(seeded_cache.published_index_reuses()==2 && seeded_cache.published_index_rejections()==1,"published stale index accounting");
      owners.NotifyUpdate(cache_key[1]);
      Require(!owners.Find(cache_key[1],NativeModelBuffers::Kind::Index)->index_storage,"unlock retained old native index ownership");
      const auto index_generation=owners.Find(cache_key[1],NativeModelBuffers::Kind::Index)->generation;
      const auto updated_index=seeded_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,mutated_indices,2).IndexStorage();
      Require(owners.RetainIndexStorage(cache_key[1],index_generation,updated_index),"validated update was not retained by model index owner");
      seeded_cache.Clear();
      Require(seeded_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,mutated_indices,2,{}, {},
        owners.Find(cache_key[1],NativeModelBuffers::Kind::Index)->index_storage).IndexStorage()==updated_index,
        "updated index allocation did not survive mesh-cache eviction");
      Require(!owners.RetainIndexStorage(cache_key[0],index_generation,updated_index),"vertex owner accepted index storage");
      owners.Publish(cache_key[1],NativeModelBuffers::Kind::Index,0xa0001000,2,uint32_t(indices16.size()/2),0x1000,loaded);
      Require(!owners.RetainIndexStorage(cache_key[1],index_generation,updated_index),"reused index owner accepted retired lifetime");
      NativeBufferWrites completed; completed.Record(0x1000,1);
      owners.ApplyWrites(completed.Drain(),[](uint32_t){});
      Require(!owners.Find(cache_key[1],NativeModelBuffers::Kind::Index)->index_storage,"completed write retained old native index ownership");
      owners.Retire(cache_key[1]);
      Require(!owners.Find(cache_key[1],NativeModelBuffers::Kind::Index),"model retirement retained index owner");
      NativeMeshCache shared_cache;
      auto variant_key=cache_key; variant_key[4]=1;
      const auto original=shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2).IndexStorage();
      const auto original_vertices=shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2).VertexStorage();
      const auto variant=shared_cache.Acquire(*backend,mesh_vs,variant_key,declaration,40,mesh_vertices,indices16,2).IndexStorage();
      Require(original==variant,"shader variant duplicated native index resource");
      Require(shared_cache.vertex_mismatches()==0 && shared_cache.index_mismatches()==0,
        "cold builds and cache hits counted as geometry mismatches");
      Require(shared_cache.Acquire(*backend,mesh_vs,variant_key,declaration,40,mesh_vertices,indices16,2).VertexStorage()==original_vertices,
        "compatible variant duplicated native vertex storage");
      const auto vertex_update=shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,updated_vertices,indices16,2).IndexStorage();
      Require(original==vertex_update,"vertex update duplicated unchanged index storage");
      Require(shared_cache.vertex_mismatches()==1 && shared_cache.index_mismatches()==0,
        "vertex mismatch classification");
      Require(shared_cache.last_vertex_mismatch()==cache_key,
        "vertex mismatch lost resource/shader identity");
      Require(original_vertices->MatchesSource(mesh_vertices) &&
        shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,updated_vertices,indices16,2).VertexStorage()!=original_vertices,
        "vertex replacement mutated an earlier generation");
      const auto wider=shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices32,4).IndexStorage();
      Require(wider!=original && wider->Matches(*backend,indices32,4),"index format change reused old generation");
      Require(shared_cache.vertex_mismatches()==1 && shared_cache.index_mismatches()==0,
        "layout change counted as source mismatch");
      Require(original->Matches(*backend,indices16,2),"index replacement mutated an earlier generation");
      // Earlier generation remains drawable after owner retirement; D3D draws
      // and native references retain its immutable storage independently.
      shared_cache.Invalidate(cache_key[1]);
      NativeIndexedMesh retained(*backend,mesh_vs,declaration,40,mesh_vertices,indices16,2,false,original,original_vertices);
      Require(retained.VertexStorage()==original_vertices,"retained immutable vertex resource was duplicated");
      {
        NativeBufferWrites writes;
        NativeModelBuffers registry(&writes);
        const auto kind=NativeModelBuffers::Kind::Vertex;
        registry.Publish(77,kind,0xa0002000,40,uint32_t(mesh_vertices.size()/40),0x2000);
        const auto generation=registry.Find(77,kind)->generation;
        {
          NativeBufferWrites::WriterScope writer(&writes);
          Require(!writes.CopyObserved(77,0x2000,mesh_vertices),"publication read through active provider");
        }
        auto snapshot=writes.CopyObserved(77,0x2000,mesh_vertices);
        Require(snapshot.has_value(),"guarded publication copy failed");
        auto contents=snapshot->contents;
        Require(!registry.RetainVertexContents(77,generation,contents),"physical snapshot accepted without observation");
        Require(registry.RetainVertexContents(77,generation,contents,snapshot->version),"publication snapshot rejected");
        registry.Publish(78,NativeModelBuffers::Kind::Index,0xa0008000,2,uint32_t(indices16.size()/2),0x8000);
        auto index_snapshot=writes.CopyObserved(78,0x8000,indices16);
        Require(index_snapshot.has_value(),"guarded index publication copy failed");
        auto index_storage=std::make_shared<const NativeIndexBuffer>(*backend,
          std::span<const uint8_t>(*index_snapshot->contents),2,index_snapshot->contents);
        Require(index_storage->SourceSnapshot()==index_snapshot->contents,
          "index publication duplicated immutable CPU contents");
        bool wrong_identity=false;
        try { NativeIndexBuffer invalid(*backend,indices16,2,index_snapshot->contents); }
        catch(const std::runtime_error&) { wrong_identity=true; }
        Require(wrong_identity,"index contents accepted unrelated source identity");
        Require(registry.CommitObservedIndex(78,registry.Find(78,NativeModelBuffers::Kind::Index)->generation,
          index_snapshot->version,index_storage) && index_storage->Matches(*backend,indices16,2),
          "guarded index snapshot failed GPU creation/attachment");
        auto live_vertices=mesh_vertices,live_indices=indices16;
        const auto pair=writes.CopyObservedSet(std::array<NativeBufferWrites::SnapshotSource,2>{{
          {77,0x2000,live_vertices},{78,0x8000,live_indices}}});
        Require(pair.has_value(),"guarded draw pair copy failed");
        {
          NativeBufferWrites::WriterScope writer(&writes);
          live_vertices[0]^=1; live_indices[0]^=1;
          writes.Record(0x2000,1); writes.Record(0x8000,1);
        }
        NativeIndexedMesh paired(*backend,mesh_vs,declaration,40,*(*pair)[0].contents,
          *(*pair)[1].contents,2,false,index_storage,{},NativeIndexedMesh::IndexReuse::RequireMatch,
          (*pair)[0].contents);
        paired.ValidateDraw(0,3,0);
        NativeMeshCache snapshot_cache;
        {
          NativeMeshCache cpu_owned_cache;
          const NativeMeshCache::Key key{277,278,279,280,0};
          auto contents=std::make_shared<const std::vector<uint8_t>>(indices16);
          auto acquire=[&](std::span<const uint8_t> bytes) -> NativeIndexedMesh& {
            return cpu_owned_cache.Acquire(*backend,mesh_vs,key,declaration,40,
              mesh_vertices,bytes,2,{},{},{},{},{},{},0,contents);
          };
          auto first=acquire(*contents).IndexStorage();
          Require(first->SourceSnapshot()==contents,"cold mesh copied canonical CPU index contents");
          Require(acquire(*contents).IndexStorage()==first &&
            cpu_owned_cache.source_checks().index_checks==0 &&
            cpu_owned_cache.source_checks().index_identity_hits==1,
            "canonical CPU index cache hit compared bytes");
          bool rejected=false;
          try { acquire(indices16); } catch(const std::runtime_error&) { rejected=true; }
          Require(rejected,"cache hit accepted unrelated index snapshot identity");
          auto changed=indices16;
          changed[1]^=1;
          contents=std::make_shared<const std::vector<uint8_t>>(changed);
          auto replacement=acquire(*contents).IndexStorage();
          Require(replacement!=first && replacement->SourceSnapshot()==contents &&
            *first->SourceSnapshot()==indices16,
            "index replacement copied canonical contents or mutated prior generation");
          cpu_owned_cache.Clear();
          Require(acquire(*contents).IndexStorage()->SourceSnapshot()==contents,
            "cache recreation lost canonical CPU index contents");
        }
        const NativeMeshCache::Key snapshot_key{177,178,179,180,0};
        const auto index_contents=paired.IndexStorage()->SourceSnapshot();
        auto& first_snapshot=snapshot_cache.Acquire(*backend,mesh_vs,snapshot_key,declaration,40,
          *(*pair)[0].contents,*index_contents,2,{},{},paired.IndexStorage(),paired.VertexStorage());
        auto& next_snapshot=snapshot_cache.Acquire(*backend,mesh_vs,snapshot_key,declaration,40,
          *(*pair)[0].contents,*index_contents,2,{},{},paired.IndexStorage(),paired.VertexStorage());
        Require(&first_snapshot==&next_snapshot && snapshot_cache.source_checks().vertex_checks==0 &&
          snapshot_cache.source_checks().index_checks==0 && snapshot_cache.source_checks().vertex_identity_hits==1 &&
          snapshot_cache.source_checks().index_identity_hits==1,
          "native snapshot cache hit scanned owned geometry bytes");
        Require(!paired.VertexStorage()->OwnsSource(live_vertices) &&
          !paired.VertexStorage()->OwnsSource(std::span(*(*pair)[0].contents).subspan(1)) &&
          !paired.IndexStorage()->OwnsSource(*backend,*index_contents,4) &&
          !paired.IndexStorage()->OwnsSource(*backend,std::span(*index_contents).subspan(2),2),
          "owned geometry identity ignored pointer, extent or index width");
        Require(paired.VertexStorage()->SourceSnapshot()==(*pair)[0].contents &&
          paired.VertexStorage()->MatchesSource(mesh_vertices) &&
          paired.IndexStorage()->Matches(*backend,indices16,2),
          "GPU geometry construction reread changed pair sources");
        Require(!registry.CommitObservedIndex(78,registry.Find(78,NativeModelBuffers::Kind::Index)->generation,
          (*pair)[1].version,paired.IndexStorage()),"old draw pair attached after a later writer");
        const auto old_vb=registry.Find(77,kind)->vertex_storage;
        const auto old_contents=registry.Find(77,kind)->vertex_contents;
        const auto old_ib=registry.Find(78,NativeModelBuffers::Kind::Index)->index_storage;
        const NativeModelBuffers::GeometryOwner vertex_owner{77,generation,(*pair)[0].version};
        const NativeModelBuffers::GeometryOwner index_owner{78,
          registry.Find(78,NativeModelBuffers::Kind::Index)->generation,(*pair)[1].version};
        Require(!registry.CommitObservedGeometry(vertex_owner,index_owner,paired.VertexStorage(),
          (*pair)[0].contents,paired.IndexStorage(),(*pair)[1].contents) && registry.Find(77,kind)->vertex_storage==old_vb &&
          registry.Find(77,kind)->vertex_contents==old_contents &&
          registry.Find(78,NativeModelBuffers::Kind::Index)->index_storage==old_ib,
          "stale pair partially replaced retained native geometry");
        // A fresh acquisition validates the original fixture bytes anew; never
        // retag the earlier pair with later observation versions.
        const auto fresh=writes.CopyObservedSet(std::array<NativeBufferWrites::SnapshotSource,2>{{
          {77,0x2000,mesh_vertices},{78,0x8000,indices16}}});
        Require(fresh.has_value(),"fresh geometry attachment observation failed");
        NativeIndexedMesh fresh_mesh(*backend,mesh_vs,declaration,40,*(*fresh)[0].contents,
          *(*fresh)[1].contents,2,false,{},{},NativeIndexedMesh::IndexReuse::RequireMatch,(*fresh)[0].contents);
        const NativeModelBuffers::GeometryOwner fresh_vb{77,generation,(*fresh)[0].version};
        const NativeModelBuffers::GeometryOwner fresh_ib{78,index_owner.generation,(*fresh)[1].version};
        Require(!registry.CommitObservedGeometry({77,generation+1,(*fresh)[0].version},fresh_ib,
          fresh_mesh.VertexStorage(),(*fresh)[0].contents,fresh_mesh.IndexStorage(),(*fresh)[1].contents),
          "paired attachment accepted wrong model lifetime");
        Require(!registry.CommitObservedGeometry(fresh_vb,fresh_ib,fresh_mesh.VertexStorage(),
          std::make_shared<const std::vector<uint8_t>>(4),fresh_mesh.IndexStorage(),(*fresh)[1].contents),
          "paired attachment accepted partial canonical vertex contents");
        Require(registry.CommitObservedGeometry(fresh_vb,fresh_ib,fresh_mesh.VertexStorage(),
          (*fresh)[0].contents,fresh_mesh.IndexStorage(),(*fresh)[1].contents) &&
          registry.Find(77,kind)->vertex_storage==fresh_mesh.VertexStorage() &&
          registry.Find(77,kind)->vertex_contents==(*fresh)[0].contents &&
          registry.Find(78,NativeModelBuffers::Kind::Index)->index_storage==fresh_mesh.IndexStorage() &&
          registry.Find(78,NativeModelBuffers::Kind::Index)->index_contents==(*fresh)[1].contents,
          "paired native GPU/canonical contents attachment failed");
        // Preserve the original publication-reuse fixture below.
        Require(registry.RetainVertexContents(77,generation,contents,writes.Version(77)),
          "could not restore publication fixture after paired attachment");
        writes.Drain();
        registry.Retire(78);
        NativeMeshCache published_cache;
        const NativeMeshCache::Key key{77,78,79,80,0};
        auto& published=published_cache.Acquire(*backend,mesh_vs,key,declaration,40,mesh_vertices,indices16,2,
          {},{},{},{},{},registry.Find(77,kind)->vertex_contents);
        Require(published.VertexStorage()->SourceSnapshot()==contents,"first GPU conversion did not consume published CPU contents");
        auto changed=mesh_vertices; changed[0]^=1;
        NativeIndexedMesh stale(*backend,mesh_vs,declaration,40,changed,indices16,2,false,{},{},
          NativeIndexedMesh::IndexReuse::RequireMatch,contents);
        Require(stale.VertexStorage()->SourceSnapshot()!=contents && stale.VertexStorage()->MatchesSource(changed),
          "unreported write reused stale publication contents");
        registry.NotifyUpdate(77);
        Require(!registry.Find(77,kind)->vertex_contents,"notified update retained CPU contents");
        Require(*contents==mesh_vertices,"invalidation mutated retained CPU generation");
        const auto before_write=writes.Version(77);
        writes.Record(0x2000,4);
        Require(!registry.RetainVertexContents(77,generation,contents,before_write),"completed write accepted stale content observation");
        auto short_contents=std::make_shared<const std::vector<uint8_t>>(4);
        Require(!registry.RetainVertexContents(77,generation,short_contents,writes.Version(77)),"wrong extent accepted as canonical contents");
        for(unsigned mode=0;mode<3;++mode) {
          Require(registry.RetainVertexContents(77,generation,contents,writes.Version(77)),"could not restore contents for invalidation test");
          NativeBufferWrites::Batch batch;
          if(mode==0) { batch.count=1; batch.ranges[0]={0x2000,4}; }
          if(mode==1) {
            auto pages=std::make_shared<NativeBufferWrites::PageMask>();
            pages->set(0x2000/NativeBufferWrites::kPageBytes); batch.pages=pages;
          }
          if(mode==2) batch.all=true;
          unsigned retired=0;
          registry.ApplyWrites(batch,[&](uint32_t owner) { Require(owner==77,"wrong invalidated owner"); ++retired; });
          Require(retired==1 && !registry.Find(77,kind)->vertex_contents,"write batch retained published CPU contents");
        }
        registry.Publish(77,kind,0xa0002000,40,uint32_t(mesh_vertices.size()/40),0x2000);
        Require(!registry.RetainVertexContents(77,generation,contents,writes.Version(77)),"new lifetime accepted old CPU contents");
      }
      auto shifted_declaration=declaration;
      word(shifted_declaration,0,0); // Different non-overlapping POSITION range, same source allocation.
      NativeIndexedMesh shifted(*backend,mesh_vs,shifted_declaration,40,mesh_vertices,indices16,2,false,original,original_vertices);
      Require(shifted.VertexStorage()!=original_vertices,"different layout reused converted GPU vertices");
      Require(shifted.VertexStorage()->SourceSnapshot()==original_vertices->SourceSnapshot(),
        "different layout duplicated validated immutable CPU vertex contents");
      NativeIndexedMesh shifted_reference(*backend,mesh_vs,shifted_declaration,40,mesh_vertices,indices16,2);
      {
        std::vector<uint8_t> prefixed(40,0xff);
        prefixed.insert(prefixed.end(),mesh_vertices.begin(),mesh_vertices.end());
        const auto contents=std::make_shared<const std::vector<uint8_t>>(prefixed);
        NativeMeshCache ranged_cache;
        const NativeMeshCache::Key key{177,178,179,180,0};
        auto& ranged=ranged_cache.Acquire(*backend,mesh_vs,key,declaration,40,mesh_vertices,indices16,2,
          {},{},{},{},{},contents,40);
        const auto storage=ranged.VertexStorage();
        Require(storage->SourceSnapshot()==contents && storage->SourceOffset()==40 &&
          storage->SourceBytes()==mesh_vertices.size() && storage->MatchesSource(mesh_vertices),
          "offset stream did not retain its range of publication contents");
        NativeIndexedMesh reference(*backend,mesh_vs,declaration,40,mesh_vertices,indices16,2);
        Require(ranged.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1)==
          reference.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1),"offset snapshot converted prefix bytes");
        auto layout_key=key; ++layout_key[2];
        auto& alternate=ranged_cache.Acquire(*backend,mesh_vs,layout_key,shifted_declaration,40,mesh_vertices,indices16,2);
        Require(alternate.VertexStorage()!=storage && alternate.VertexStorage()->SourceSnapshot()==contents &&
          alternate.VertexStorage()->SourceOffset()==40,"layout conversion lost retained source offset");
        Require(alternate.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1)==
          shifted_reference.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1),"ranged layout conversion output differs");
        Require(ranged_cache.Acquire(*backend,mesh_vs,key,declaration,40,mesh_vertices,indices16,2,
          {},{},{},{},{},contents,40).VertexStorage()==storage,"unchanged ranged cache hit rebuilt storage");
        const auto changed=ranged_cache.Acquire(*backend,mesh_vs,key,declaration,40,updated_vertices,indices16,2,
          {},{},{},{},{},contents,40).VertexStorage();
        Require(changed->SourceSnapshot()!=contents && changed->SourceOffset()==0 && changed->MatchesSource(updated_vertices),
          "changed offset stream revived stale contents");
        for(const auto invalid_offset:{contents->size(),SIZE_MAX}) {
          NativeIndexedMesh invalid(*backend,mesh_vs,declaration,40,mesh_vertices,indices16,2,false,{},{},
            NativeIndexedMesh::IndexReuse::RequireMatch,contents,invalid_offset);
          Require(invalid.VertexStorage()->SourceSnapshot()!=contents && invalid.VertexStorage()->SourceOffset()==0 &&
            invalid.VertexStorage()->MatchesSource(mesh_vertices),"invalid snapshot range did not fall back to live contents");
        }
        ranged_cache.Invalidate(key[0]);
        Require(ranged_cache.entries()==0 && *contents==prefixed,"range invalidation mutated retained generation");
      }
      Require(shifted.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1)==
        shifted_reference.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1),
        "shared CPU snapshot used the old GPU conversion layout");
      NativeIndexedMesh shifted_changed(*backend,mesh_vs,shifted_declaration,40,updated_vertices,indices16,2,false,original,original_vertices);
      Require(shifted_changed.VertexStorage()->SourceSnapshot()!=original_vertices->SourceSnapshot() &&
        shifted_changed.VertexStorage()->MatchesSource(updated_vertices),
        "layout change bypassed live source validation");
      {
        NativeMeshCache layout_cache;
        auto layout_key=cache_key; layout_key[2]+=100;
        const auto first_layout=layout_cache.Acquire(*backend,mesh_vs,cache_key,
          declaration,40,mesh_vertices,indices16,2).VertexStorage();
        auto& second_mesh=layout_cache.Acquire(*backend,mesh_vs,layout_key,
          shifted_declaration,40,mesh_vertices,indices16,2);
        const auto second_layout=second_mesh.VertexStorage();
        const auto shared_source=first_layout->SourceSnapshot();
        Require(first_layout!=second_layout && shared_source==second_layout->SourceSnapshot(),
          "cache layout variants did not share immutable source independently of GPU layout");
        Require(second_mesh.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1)==
          shifted_reference.CaptureClipPositions(*context.Get(),mesh_vs,0,6,-1),
          "cached source sharing changed converted clip positions");
        const auto changed_layout=layout_cache.Acquire(*backend,mesh_vs,layout_key,
          shifted_declaration,40,updated_vertices,indices16,2).VertexStorage();
        Require(changed_layout->SourceSnapshot()!=shared_source && *shared_source==mesh_vertices &&
          changed_layout->MatchesSource(updated_vertices),"cache update mutated a shared CPU generation");
        const auto first_updated=layout_cache.Acquire(*backend,mesh_vs,cache_key,
          declaration,40,updated_vertices,indices16,2).VertexStorage();
        Require(first_updated!=first_layout && first_updated->MatchesSource(updated_vertices),
          "other cached layout missed changed source contents");
        layout_cache.Invalidate(cache_key[0]);
        Require(layout_cache.entries()==0,"vertex retirement retained a cached layout");
        const auto after_retirement=layout_cache.Acquire(*backend,mesh_vs,cache_key,
          declaration,40,mesh_vertices,indices16,2).VertexStorage();
        Require(after_retirement->SourceSnapshot()!=shared_source &&
          *shared_source==mesh_vertices,"retirement revived or mutated an earlier source generation");
      }
      submit=[&] { retained.Draw(*context.Get(),0,6,-1); }; verify(false);
      const auto recreated=shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2).IndexStorage();
      Require(recreated!=original,"retired index owner reused prior native generation");
      auto changed=indices16; std::swap(changed[1],changed[3]);
      const auto changed_generation=shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,changed,2).IndexStorage();
      Require(changed_generation!=recreated && recreated->Matches(*backend,indices16,2),"index byte mutation lost immutable generation");
      Require(shared_cache.vertex_mismatches()==1 && shared_cache.index_mismatches()==1,
        "index mismatch classification");
      Require(shared_cache.last_index_mismatch()==cache_key && shared_cache.last_vertex_mismatch()==cache_key,
        "index mismatch lost identity or overwrote vertex diagnostic");
      // A rejected candidate must not be reconsidered in construction. Restore
      // the bytes at the snapshot boundary to expose a second comparison: that
      // comparison would now match and incorrectly revive the rejected storage.
      NativeMeshCache rejection_cache;
      auto mutable_indices=indices16;
      auto mutable_vertices=mesh_vertices;
      auto& first_rejection_mesh=rejection_cache.Acquire(*backend,mesh_vs,cache_key,
        declaration,40,mutable_vertices,mutable_indices,2);
      const auto rejected_ib=first_rejection_mesh.IndexStorage();
      const auto rejected_vb=first_rejection_mesh.VertexStorage();
      mutable_indices=changed;
      auto restore_indices=[&] { std::copy(indices16.begin(),indices16.end(),mutable_indices.begin()); };
      auto& after_index_rejection=rejection_cache.Acquire(*backend,mesh_vs,cache_key,
        declaration,40,mutable_vertices,mutable_indices,2,{},{},rejected_ib,rejected_vb,
        {&restore_indices,[](void* p) { (*static_cast<decltype(restore_indices)*>(p))(); }});
      Require(after_index_rejection.IndexStorage()!=rejected_ib &&
        after_index_rejection.IndexStorage()->Matches(*backend,indices16,2),
        "construction reconsidered rejected index storage or missed snapshot bytes");
      mutable_vertices=updated_vertices;
      auto restore_vertices=[&] { std::copy(mesh_vertices.begin(),mesh_vertices.end(),mutable_vertices.begin()); };
      auto& after_vertex_rejection=rejection_cache.Acquire(*backend,mesh_vs,cache_key,
        declaration,40,mutable_vertices,mutable_indices,2,{},{},{},rejected_vb,
        {&restore_vertices,[](void* p) { (*static_cast<decltype(restore_vertices)*>(p))(); }});
      Require(after_vertex_rejection.VertexStorage()!=rejected_vb &&
        after_vertex_rejection.VertexStorage()->MatchesSource(mesh_vertices),
        "construction reconsidered rejected vertex storage or missed snapshot bytes");
      bool rejected_generation=false;
      try { NativeIndexedMesh invalid(*backend,mesh_vs,declaration,40,mesh_vertices,changed,2,false,original); }
      catch(const std::runtime_error&) { rejected_generation=true; }
      Require(rejected_generation,"mesh accepted mismatched shared index generation");
      NativeIndexedMesh replaced(*backend,mesh_vs,declaration,40,mesh_vertices,changed,2,false,original,{},
        NativeIndexedMesh::IndexReuse::ReplaceStale);
      Require(replaced.IndexStorage()!=original && replaced.IndexStorage()->Matches(*backend,changed,2) &&
        original->Matches(*backend,indices16,2),"single-validation replacement lost old/new index contents");
      NativeIndexedMesh reused(*backend,mesh_vs,declaration,40,mesh_vertices,indices16,2,false,original,{},
        NativeIndexedMesh::IndexReuse::ReplaceStale);
      Require(reused.IndexStorage()==original,"single-validation path failed to retain matching index storage");
      shared_cache.Invalidate(cache_key[0]);
      Require(shared_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2).VertexStorage()!=original_vertices,
        "retired vertex owner reused prior generation");
      NativeIndexedMesh dynamic_a(*backend,mesh_vs,declaration,40,mesh_vertices,indices16,2,true,original,original_vertices);
      NativeIndexedMesh dynamic_b(*backend,mesh_vs,declaration,40,mesh_vertices,indices16,2,true,original,dynamic_a.VertexStorage());
      Require(dynamic_a.VertexStorage()!=original_vertices && dynamic_a.VertexStorage()!=dynamic_b.VertexStorage(),
        "dynamic vertex allocations shared mutable ownership");
      const auto previous_dynamic_source=dynamic_a.VertexStorage()->SourceSnapshot();
      dynamic_a.UpdateVertices(*context.Get(),updated_vertices);
      Require(dynamic_a.VertexStorage()->SourceSnapshot()!=previous_dynamic_source &&
        *previous_dynamic_source==mesh_vertices &&
        *dynamic_a.VertexStorage()->SourceSnapshot()==updated_vertices,
        "dynamic update mutated an earlier CPU source generation");
      submit=[&] { dynamic_b.Draw(*context.Get(),0,6,-1); }; verify(false);
    }
    NativeMeshCache streaming_cache(4*1024*1024,256,true);
    auto stream_acquire=[&](std::span<const uint8_t> data)->NativeIndexedMesh& {
      return streaming_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,data,indices16,2);
    };
    // Updating after a queued draw must preserve its old vertices until the GPU
    // consumes them. Readback here tests DISCARD, not only the latest upload.
    submit=[&] {
      stream_acquire(mesh_vertices).Draw(*context.Get(),0,6,-1);
      stream_acquire(updated_vertices);
    }; verify(false);
    ComPtr<ID3D11Buffer> original_stream;
    UINT stream_stride=0,stream_offset=0;
    context->IAGetVertexBuffers(0,1,&original_stream,&stream_stride,&stream_offset);
    D3D11_BUFFER_DESC stream_desc{}; original_stream->GetDesc(&stream_desc);
    Require(stream_desc.Usage==D3D11_USAGE_DYNAMIC && stream_desc.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE,
      "streaming mesh does not use writable dynamic vertices");
    submit=[&] {
      stream_acquire(updated_vertices).Draw(*context.Get(),0,6,-1);
      stream_acquire(mesh_vertices);
    }; verify(true);
    ComPtr<ID3D11Buffer> reused_stream;
    context->IAGetVertexBuffers(0,1,&reused_stream,&stream_stride,&stream_offset);
    Require(original_stream.Get()==reused_stream.Get() && streaming_cache.builds()==1 &&
      streaming_cache.updates()==2 && streaming_cache.hits()==1,"dynamic vertex update rebuilt GPU resources");
    submit=[&] { stream_acquire(mesh_vertices).Draw(*context.Get(),0,6,-1); }; verify(false);
    bool rejected_stream_size=false;
    try { stream_acquire(mesh_vertices).UpdateVertices(*context.Get(),std::span(mesh_vertices).first(1)); }
    catch(const std::runtime_error&) { rejected_stream_size=true; }
    Require(rejected_stream_size,"dynamic update accepted changed vertex count");
    bool rejected_immutable_update=false;
    try { mesh16.UpdateVertices(*context.Get(),mesh_vertices); }
    catch(const std::runtime_error&) { rejected_immutable_update=true; }
    Require(rejected_immutable_update,"immutable mesh accepted vertex update");
    auto updated_indices=indices16; updated_indices[1]=100;
    bool rejected_cached_index=false;
    try {
      mesh_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,updated_vertices,updated_indices,2)
        .ValidateDraw(0,6,-1);
    } catch (const std::runtime_error&) { rejected_cached_index=true; }
    Require(rejected_cached_index && mesh_cache.builds()==3,"updated indices reused stale cache");
    auto malformed_decl=declaration; word(malformed_decl,4,0xffffffff);
    bool rejected_cached_decl=false;
    try { mesh_cache.Acquire(*backend,mesh_vs,cache_key,malformed_decl,40,mesh_vertices,indices16,2); }
    catch (const std::runtime_error&) { rejected_cached_decl=true; }
    Require(rejected_cached_decl && mesh_cache.bytes()==0,"failed declaration update retained cache entry");
    acquire(mesh_vertices);
    auto reloaded_vs=CompileNativeShader(*device.Get(),mesh_effect,{false,"VS","vs_5_0"},"mesh.fx");
    bool rejected_stream_decl=false;
    try { streaming_cache.Acquire(*backend,mesh_vs,cache_key,malformed_decl,40,mesh_vertices,indices16,2); }
    catch(const std::runtime_error&) { rejected_stream_decl=true; }
    Require(rejected_stream_decl && streaming_cache.bytes()==0,"failed dynamic layout retained stale geometry");
    stream_acquire(mesh_vertices);
    const auto stream_builds=streaming_cache.builds();
    streaming_cache.Acquire(*backend,reloaded_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
    Require(streaming_cache.builds()==stream_builds+1,"dynamic mesh ignored shader reload");
    streaming_cache.Acquire(*backend,reloaded_vs,cache_key,declaration,40,mesh_vertices,indices32,4);
    Require(streaming_cache.builds()==stream_builds+2,"dynamic mesh ignored changed index format");
    streaming_cache.Invalidate(102);
    Require(streaming_cache.bytes()==0,"dynamic resource invalidation retained geometry");
    const auto before_reload=mesh_cache.builds();
    mesh_cache.Acquire(*backend,reloaded_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
    Require(mesh_cache.builds()==before_reload+1,"shader reload reused old layout");
    mesh_cache.Invalidate(102);
    Require(mesh_cache.bytes()==0,"destroyed index resource retained cache entry");
    // Embedded model owners can release/recreate without final COM destruction.
    // Identical bytes and handle values still belong to a new native lifetime.
    for (const auto resource : {cache_key[0],cache_key[1]}) {
      mesh_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
      const auto before_reuse=mesh_cache.builds();
      mesh_cache.Invalidate(resource);
      Require(mesh_cache.entries()==0 && mesh_cache.bytes()==0,"model cleanup retained native mesh");
      mesh_cache.Invalidate(resource); // cleanup of an already empty owner
      mesh_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
      Require(mesh_cache.builds()==before_reuse+1,"recreated model owner reused retired mesh");
    }
    NativeMeshCache bounded_cache(payload_bytes);
    auto second_key=cache_key; second_key[0]=201;
    bounded_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
    bounded_cache.Acquire(*backend,mesh_vs,second_key,declaration,40,mesh_vertices,indices16,2);
    bounded_cache.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
    Require(bounded_cache.builds()==3 && bounded_cache.bytes()==payload_bytes,"mesh cache budget eviction failed");
    Require(bounded_cache.budget_evictions()==2 && bounded_cache.entry_evictions()==0,
      "mesh budget eviction attribution");
    NativeMeshCache entry_bounded(payload_bytes*4,2);
    auto third_key=cache_key; third_key[0]=301;
    for(const auto& key:{cache_key,second_key,cache_key,third_key,cache_key})
      entry_bounded.Acquire(*backend,mesh_vs,key,declaration,40,mesh_vertices,indices16,2);
    Require(entry_bounded.builds()==3 && entry_bounded.hits()==2 && entry_bounded.entries()==2 &&
      entry_bounded.entry_evictions()==1 && entry_bounded.budget_evictions()==0,
      "mesh entry cap preserves recently used entry");
    NativeMeshCache more_entries(payload_bytes*300);
    for(uint32_t i=0;i<257;++i) {
      auto key=cache_key; key[0]=1000+i;
      more_entries.Acquire(*backend,mesh_vs,key,declaration,40,mesh_vertices,indices16,2);
    }
    auto first_key=cache_key; first_key[0]=1000;
    more_entries.Acquire(*backend,mesh_vs,first_key,declaration,40,mesh_vertices,indices16,2);
    Require(more_entries.builds()==257 && more_entries.hits()==1 && more_entries.entries()==257 &&
      more_entries.bytes()<=payload_bytes*300,"mesh cache still thrashes at former256 entry limit");
    NativeMeshCache zero_entries(payload_bytes,0);
    zero_entries.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
    Require(zero_entries.entries()==0 && zero_entries.bytes()==0,"zero entry limit must not retain meshes");
    NativeMeshCache uncached(1);
    for (int i=0;i<2;++i) uncached.Acquire(*backend,mesh_vs,cache_key,declaration,40,mesh_vertices,indices16,2);
    Require(uncached.builds()==2 && uncached.bytes()==0,"oversized mesh retained in cache");
    // The farther mesh has inverted UVs. Its color must be rejected after a
    // nearer draw, but must appear when depth writes or depth testing are off.
    auto far_vertices=mesh_vertices;
    for (size_t v=0;v<4;++v) {
      word(far_vertices,v*40+12,std::bit_cast<uint32_t>(.5f));
      word(far_vertices,v*40+28,std::bit_cast<uint32_t>(1-quad[v*4+2]));
      word(far_vertices,v*40+32,std::bit_cast<uint32_t>(1-quad[v*4+3]));
    }
    NativeIndexedMesh far_mesh(*backend,mesh_vs,declaration,40,far_vertices,indices16,2);
    for (auto format : {DXGI_FORMAT_D24_UNORM_S8_UINT,DXGI_FORMAT_D32_FLOAT,DXGI_FORMAT_D32_FLOAT_S8X24_UINT}) {
      auto depth=CreateNativeDepthTarget(*backend,4,2,format);
      Require(!depth.depth_valid && !depth.stencil_valid,"new depth storage marked valid");
      context->OMSetRenderTargets(1,&rtv,depth.target.Get());
      auto depth_write=CreateNativeRenderState(*device.Get(),{0x10001,0x16,0,0,15,0});
      auto depth_read=CreateNativeRenderState(*device.Get(),{0x10001,0x12,0,0,15,0});
      depth_write.Bind(*context.Get()); // LESS, depth test and writes enabled.
      submit=[&] {
        ClearNativeDepthTarget(*context.Get(),depth,true,false,1,0);
        mesh16.Draw(*context.Get(),0,6,-1); far_mesh.Draw(*context.Get(),0,6,-1);
      };
      verify(false);
      Require(depth.depth_valid && !depth.stencil_valid,"depth-only clear initialized stencil");
      depth_read.Bind(*context.Get()); verify(true);
      unclipped_state.Bind(*context.Get()); verify(true);
      // D3D11 rejects MinDepth > MaxDepth. Keep the legal 0..1 viewport
      // and reverse clip-space depth in the native vertex entry wrapper.
      const auto reverse_view=MakeNativeDrawViewport(0,0,4,2,1,0,false,{});
      Require(reverse_view.reverse_depth && reverse_view.viewport.MinDepth==0 && reverse_view.viewport.MaxDepth==1,
        "reversed viewport not normalized with shader selection");
      reverse_view.Bind(*context.Get());
      context->VSSetShader(reversed_vs.vertex.Get(),nullptr,0);
      auto reverse_write=CreateNativeRenderState(*device.Get(),{0x10001,0x46,0,0,15,0});
      reverse_write.Bind(*context.Get()); // GREATER with reversed viewport.
      submit=[&] {
        ClearNativeDepthTarget(*context.Get(),depth,true,false,0,0);
        mesh16.Draw(*context.Get(),0,6,-1); far_mesh.Draw(*context.Get(),0,6,-1);
      };
      verify(false);
      const auto partial_reverse=MakeNativeDrawViewport(0,0,4,2,.75f,.25f,false,{});
      Require(partial_reverse.reverse_depth,"partial reversed range lost");
      partial_reverse.Bind(*context.Get()); verify(false);
      context->VSSetShader(mesh_vs.vertex.Get(),nullptr,0);
      context->RSSetViewports(1,&viewport);
    }
    context->OMSetRenderTargets(1,&rtv,nullptr);
    Effect packed_effect;
    packed_effect.source=R"(
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION0,float3 normal : NORMAL0,float2 uv : TEXCOORD0,
           float4 indices : BLENDINDICES0,float4 color : COLOR0) {
  Varying result; result.position=float4(position,1);
  result.uv=(uv*normal.xy+normal.z)*dot(indices,float4(1,2,4,8))/49*dot(color,float4(1,2,4,8))/9;
  return result;
}
)";
    auto packed_vs=CompileNativeShader(*device.Get(),packed_effect,{false,"VS","vs_5_0"},"packed.fx");
    ValidateNativeShaderLink(packed_vs,mesh_ps);
    auto packed_decl=declaration; packed_decl.resize(60);
    word(packed_decl,36,40); word(packed_decl,40,0x1a2286); packed_decl[45]=2;
    word(packed_decl,48,44); word(packed_decl,52,0x182886); packed_decl[57]=10;
    std::vector<uint8_t> packed_vertices(4*48);
    for (size_t v=0;v<4;++v) {
      for (size_t b=0;b<40;++b) packed_vertices[v*48+b]=mesh_vertices[v*40+b];
      word(packed_vertices,v*48+40,0x04030201); // UBYTE4 -> 1,2,3,4, not normalized.
      word(packed_vertices,v*48+44,0xffff0000); // ARGB -> red, alpha one.
    }
    NativeIndexedMesh packed_mesh(*backend,packed_vs,packed_decl,48,packed_vertices,indices16,2);
    unclipped_state.Bind(*context.Get());
    context->VSSetShader(packed_vs.vertex.Get(),nullptr,0);
    submit=[&] { packed_mesh.Draw(*context.Get(),0,6,-1); }; verify(false);
    // Retail envelope shaders use int4 indices into float4x3 bone palettes.
    // Exercise real dynamic matrix indexing, not just float attribute arithmetic.
    auto skin_decl=packed_decl; skin_decl.resize(72);
    word(skin_decl,60,48); word(skin_decl,64,0x1a23a6); skin_decl[69]=1;
    std::vector<uint8_t> skin_vertices(4*64);
    const std::array<float,4> weights{.125f,.125f,.25f,.5f};
    for (size_t v=0;v<4;++v) {
      for (size_t b=0;b<48;++b) skin_vertices[v*64+b]=packed_vertices[v*48+b];
      for (size_t c=0;c<4;++c) word(skin_vertices,v*64+48+c*4,std::bit_cast<uint32_t>(weights[c]));
    }
    std::vector<float> palette(32*12,0);
    for (size_t bone=0;bone<32;++bone)
      for (size_t axis=0;axis<3;++axis) palette[bone*12+axis*4+axis]=float(bone);
    for (const auto* index_type : {"int4","uint4"}) for (bool reversed : {false,true}) {
      Effect skin_effect;
      skin_effect.source=std::string(R"(
float4x3 bones[32];
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION0,float3 normal : NORMAL0,float2 uv : TEXCOORD0,
)")+index_type+R"( indices : BLENDINDICES0,float4 color : COLOR0,float4 weights : BLENDWEIGHT0) {
  float3 p=0;
  for (int i=0;i<4;++i) p+=mul(float4(position,1),bones[indices[i]])*weights[i];
  Varying result; result.position=float4(p/3.125,1);
  result.uv=(uv*normal.xy+normal.z)*dot(color,float4(1,2,4,8))/9;
  return result;
}
)";
      ShaderBindings skin_vs(*device.Get(),CompileNativeShader(*device.Get(),skin_effect,
        {false,"VS","vs_5_0"},"skin.fx",reversed));
      ValidateNativeShaderLink(skin_vs.shader(),mesh_ps);
      Require(skin_vs.GuestFloatRegisterBytes("bones")==palette.size()*4,"bone palette register size");
      Require(skin_vs.SetGuestFloatRegisters("bones",Guest(palette)),"bone palette upload");
      // Simulate per-instance palette replacement: corrupt one live bone, then
      // restore only its three column registers. The following GPU draw also
      // verifies that neighboring bones retained their original transforms.
      const std::array<float,12> empty_bone{};
      skin_vs.PatchGuestFloatRegisters("bones",6,Guest(empty_bone));
      const auto palette_bytes=Guest(palette);
      skin_vs.PatchGuestFloatRegisters("bones",6,
        std::span<const uint8_t>(palette_bytes).subspan(6*16,3*16));
      NativeIndexedMesh skin_mesh(*backend,skin_vs.shader(),skin_decl,64,skin_vertices,indices16,2);
      skin_vs.Bind(*context.Get());
      submit=[&] { skin_mesh.Draw(*context.Get(),0,6,-1); }; verify(false);
    }
    bool rejected_index=false;
    try { mesh16.Draw(*context.Get(),0,6,0); } catch (const std::runtime_error&) { rejected_index=true; }
    Require(rejected_index,"out-of-range mesh vertex index accepted");
    rejected_index=false;
    try { mesh16.Draw(*context.Get(),5,3,-1); } catch (const std::runtime_error&) { rejected_index=true; }
    Require(rejected_index,"out-of-range mesh index span accepted");
    auto invalid_decl=declaration; word(invalid_decl,4,0xffffffff);
    bool rejected_format=false;
    try { NativeIndexedMesh invalid(*backend,mesh_vs,invalid_decl,40,mesh_vertices,indices16,2); }
    catch (const std::runtime_error&) { rejected_format=true; }
    Require(rejected_format,"unsupported packed mesh attribute accepted");
    std::shared_ptr<NativeVertexBuffer> previous_defaults;
    bool previous_integer=false;
    for (const auto* component_type : {"float4","int4","uint4"}) {
      Effect defaults;
      defaults.source=std::string(R"(
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION0,float3 normal : NORMAL0,float2 uv : TEXCOORD0,
)")+component_type+R"( missing : TANGENT0) {
  Varying result; result.position=float4(position,1);
  result.uv=(uv*normal.xy+normal.z)*float(missing.w)+float2(missing.x+missing.z,missing.y+missing.z);
  return result;
}
)";
      for (bool reversed : {false,true}) {
        auto defaults_vs=CompileNativeShader(*device.Get(),defaults,{false,"VS","vs_5_0"},"defaults.fx",reversed);
        NativeIndexedMesh defaults_mesh(*backend,defaults_vs,declaration,40,mesh_vertices,indices16,2,false,{},previous_defaults);
        const bool integer=std::string_view(component_type)!="float4";
        if(previous_defaults) Require((defaults_mesh.VertexStorage()==previous_defaults)==(integer==previous_integer),
          "vertex resource sharing ignored default-component conversion contract");
        previous_defaults=defaults_mesh.VertexStorage(); previous_integer=integer;
        context->VSSetShader(defaults_vs.vertex.Get(),nullptr,0);
        submit=[&] { defaults_mesh.Draw(*context.Get(),0,6,-1); }; verify(false);
      }
    }
    // Cover every normalization overload in the pixel stage as well. Real NaN
    // inputs must remain visible to diagnostics; this is not a blanket scrub.
    Effect normalization_effect;
    normalization_effect.source=R"(
float4 InputValue;
float4 PS():SV_TARGET {
  return float4(normalize(InputValue.x),normalize(InputValue.xy).x,
                normalize(InputValue.xyz).x,normalize(InputValue).x);
})";
    ShaderBindings normalization_ps(*device.Get(),CompileNativeShader(*device.Get(),normalization_effect,
      {true,"PS","ps_5_0"},"normalization.fx"));
    context->OMSetRenderTargets(1,&rtv,nullptr);
    context->OMSetBlendState(nullptr,nullptr,0xffffffff);
    context->OMSetDepthStencilState(nullptr,0);
    context->RSSetState(raster_state.Get());
    context->RSSetViewports(1,&viewport);
    context->VSSetShader(vs.vertex.Get(),nullptr,0);
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    const std::array<std::array<float,4>,4> normalize_inputs{{
      {-0.0f,-0.0f,-0.0f,-0.0f},{3,4,0,0},{-3,-4,0,0},{nan,nan,nan,nan}}};
    for(size_t sample=0;sample<normalize_inputs.size();++sample) {
      normalization_ps.SetGuestFloatRegisters("InputValue",Guest(normalize_inputs[sample]));
      normalization_ps.Bind(*context.Get());
      stream.Draw(*context.Get(),bytes);
      const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),1,1);
      if(sample==0 || sample==3) {
        // Paired scene/output captures must distinguish real black from an
        // invalid shader result rather than silently quantizing both to black.
        const auto diagnostic=CaptureNativeHdrBmp(*context.Get(),*target.surface.Get());
        const size_t pixel=54+12+3; // 4x2 RGB24: row1, column1, padded pitch12.
        Require(diagnostic.size()>pixel+2,"normalization diagnostic BMP bounds");
        Require(diagnostic[pixel]==(sample==3?255:0) && diagnostic[pixel+1]==0 &&
                diagnostic[pixel+2]==(sample==3?255:0),"diagnostic confused NaN with real black");
        const auto retained=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),1,1);
        Require(sample==3?std::isnan(retained[0]):retained[0]==0,
          "diagnostic capture changed shader output");
      }
      for(size_t lane=0;lane<4;++lane) {
        if(sample==0) Require(actual[lane]==0 && !std::signbit(actual[lane]),"normalize zero not positive zero");
        else if(sample==3) Require(std::isnan(actual[lane]),"normalize hid a NaN input");
        else {
          const float expected=(sample==1?1.f:-1.f)*(lane==0?1.f:.6f);
          Require(std::abs(actual[lane]-expected)<.001f,"nonzero normalize changed");
        }
      }
    }
    // c_Mech01 uses Common.fx's DXT5 normal reconstruction. Quantization can
    // put decoded XY just outside the unit disk; Z must not become NaN.
    {
      Effect normal_effect;
      normal_effect.source=R"(
texture NormalTexture;
sampler NormalSampler=sampler_state { Texture=(NormalTexture); };
float3 tex2D_DXT5N_xGxR(sampler i_Sampler,float2 i_UV) {
  float4 N=2.0f*tex2D(i_Sampler,i_UV)-1.0f;
  N.x=N.w;
  N.z = sqrt (1-dot(N.xy, N.xy));
  return N.xyz;
}
float4 PS():SV_TARGET {return float4(tex2D_DXT5N_xGxR(NormalSampler,float2(.5,.5)),1);}
)";
      ShaderBindings normal_ps(*device.Get(),CompileNativeShader(*device.Get(),normal_effect,
        {true,"PS","ps_5_0"},"normal_decode.fx"));
      auto normal_texture=CreateNativeRenderTarget(*backend,1,1,DXGI_FORMAT_R16G16B16A16_FLOAT);
      D3D11_SAMPLER_DESC desc{}; desc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
      desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.MaxLOD=D3D11_FLOAT32_MAX;
      NativeBackendSamplerDesc normal_desc{};
      normal_desc.min=normal_desc.mag=normal_desc.mip=NativeBackendFilter::Point;
      normal_desc.u=normal_desc.v=normal_desc.w=NativeBackendAddress::Clamp;
      normal_ps.SetSampler("NormalSampler",&backend->CreateSampler(normal_desc));
      {
        // The shadow render saves every sampler slot before its guest route
        // and puts them back after it (native_shadow_render.h), so the
        // full-frame post, which reads its samplers back from the bindings,
        // posts after a shadow frame exactly as before it.
        NativeBackendSamplerDesc other_desc=normal_desc;
        other_desc.min=other_desc.mag=NativeBackendFilter::Linear;
        auto* original=normal_ps.ReadSampler("NormalSampler");
        const auto saved=normal_ps.SamplerValues();
        const auto generation=normal_ps.resource_generation();
        Require(saved.size()==1 && normal_ps.RestoreSamplerValues(saved)==0 && normal_ps.resource_generation()==generation,
                "restoring unchanged samplers changed the bindings");
        normal_ps.SetSampler("NormalSampler",&backend->CreateSampler(other_desc));
        Require(normal_ps.ReadSampler("NormalSampler")!=original,"the test sampler was not replaced");
        Require(normal_ps.RestoreSamplerValues(saved)==1 && normal_ps.ReadSampler("NormalSampler")==original &&
                normal_ps.resource_generation()!=generation,"sampler restore did not put the saved sampler back");
        Require(normal_ps.SamplerImages().size()==1 && normal_ps.SamplerImages()[0].sampler==original,
                "the restored sampler is not what a recorder is told to bind");
        const std::map<UINT,NativeBackendSampler*> foreign{{15,nullptr}};
        Require(normal_ps.RestoreSamplerValues(foreign)==0,"a slot the bindings do not reflect was restored");
      }
      for(const auto xy:std::array<std::array<float,2>,6>{{{.5f,.5f},{.75f,.5f},{1,1},{0,0},{1,128.f/255},{.5f,nan}}}) {
        const float encoded[]{0,xy[1],0,xy[0]};
        context->ClearRenderTargetView(normal_texture.target.Get(),encoded);
        normal_texture.content_valid=true;
        normal_ps.ClearTextures(); normal_ps.Bind(*context.Get());
        ResolveNativeRenderTarget(*context.Get(),normal_texture);
        normal_ps.SetTexture("NormalSampler",normal_texture.sampled.backend);
        normal_ps.Bind(*context.Get()); stream.Draw(*context.Get(),bytes);
        const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),1,1);
        if(std::isnan(xy[1])) {
          Require(std::isnan(actual[2]),"normal decoder hid a genuine NaN input");
          continue;
        }
        Require(std::isfinite(actual[0]) && std::isfinite(actual[1]) && std::isfinite(actual[2]),
          "DXT5 normal reconstruction produced nonfinite RGB");
        const float expected_x=xy[0]*2-1,expected_y=xy[1]*2-1;
        const float expected_z=std::sqrt((std::max)(0.f,1-expected_x*expected_x-expected_y*expected_y));
        Require(std::abs(actual[0]-expected_x)<.002f && std::abs(actual[1]-expected_y)<.002f &&
          std::abs(actual[2]-expected_z)<.002f,"DXT5 normal reconstruction differs");
      }
    }
    // Utility.dxsl's 2D position/color[/UV] contract, with retail12/20-byte
    // vertices and the same indexed quad conversion used by the live adapter.
    Effect utility;
    utility.source=R"(
cbuffer Transform { float2 _g_DX2DScale; float2 _g_DX2DOffset; };
struct U { float4 position:SV_POSITION; float4 color:COLOR0; float2 uv:TEXCOORD0; };
U VS_2DTex(float2 p:POSITION0,float4 color:COLOR0,float2 uv:TEXCOORD0) {
  U o; o.position=float4(p*_g_DX2DScale+_g_DX2DOffset,0,1); o.color=color; o.uv=uv; return o;
}
U VS_2D(float2 p:POSITION0,float4 color:COLOR0) { return VS_2DTex(p,color,float2(0,0)); }
Texture2D<float4> m_Texture; SamplerState m_Sampler;
float4 PS_Main(U i):SV_TARGET { return i.color; }
float4 PS_Tex(U i):SV_TARGET { return m_Texture.Sample(m_Sampler,i.uv)*i.color; }
)";
    D3D11_TEXTURE2D_DESC utility_desc{}; utility_desc.Width=2; utility_desc.Height=1;
    utility_desc.MipLevels=utility_desc.ArraySize=utility_desc.SampleDesc.Count=1;
    utility_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; utility_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const std::array<uint8_t,8> utility_pixels{255,0,0,255,0,255,0,128};
    const D3D11_SUBRESOURCE_DATA utility_data{utility_pixels.data(),8,0};
    ComPtr<ID3D11Texture2D> utility_texture; ComPtr<ID3D11ShaderResourceView> utility_view;
    Require(SUCCEEDED(device->CreateTexture2D(&utility_desc,&utility_data,&utility_texture)),"Utility texture");
    Require(SUCCEEDED(device->CreateShaderResourceView(utility_texture.Get(),nullptr,&utility_view)),"Utility texture view");
    // The same texture and sampler as backend handles, which is what a
    // material binding holds; the D3D11 views above stay for the direct calls.
    NativeBackendTextureDesc utility_backend_desc{};
    utility_backend_desc.width=2; utility_backend_desc.height=1; utility_backend_desc.levels=1;
    utility_backend_desc.format=DXGI_FORMAT_R8G8B8A8_UNORM;
    std::shared_ptr<NativeBackendTexture> utility_backend=
      backend->CreateTexture(utility_backend_desc,utility_pixels);
    Require(bool(utility_backend),"Utility backend texture");
    NativeBackendSamplerDesc utility_sampler_desc{};
    utility_sampler_desc.min=utility_sampler_desc.mag=utility_sampler_desc.mip=NativeBackendFilter::Point;
    utility_sampler_desc.u=utility_sampler_desc.v=utility_sampler_desc.w=NativeBackendAddress::Clamp;
    auto* utility_sampler=&backend->CreateSampler(utility_sampler_desc);
    context->OMSetRenderTargets(1,&rtv,nullptr); context->OMSetBlendState(nullptr,nullptr,UINT32_MAX);
    context->OMSetDepthStencilState(nullptr,0); context->RSSetState(raster_state.Get()); context->RSSetViewports(1,&viewport);
    const std::array<uint8_t,12> utility_indices{0,0,0,1,0,2,0,0,0,2,0,3};
    NativeGeneratedIndexCache generated_indices;
    const auto generated_quad=generated_indices.Get(NativeIndexPattern::Quads,4);
    Require(std::equal(generated_quad->bytes().begin(),generated_quad->bytes().end(),utility_indices.begin(),utility_indices.end()),
      "generated quad topology differs");
    Require(generated_indices.Get(NativeIndexPattern::Quads,4)==generated_quad,"generated index cache lost identity");
    for(auto pattern:{NativeIndexPattern::Quads,NativeIndexPattern::Lines,NativeIndexPattern::Strip}) {
      for(uint32_t count:{0u,65537u}) {
        bool rejected=false;
        try { generated_indices.Get(pattern,count); } catch(const std::runtime_error&) { rejected=true; }
        Require(rejected,"invalid generated index count accepted");
      }
      const auto maximum=generated_indices.Get(pattern,65536);
      Require(maximum->bytes()[maximum->bytes().size()-2]==255 && maximum->bytes().back()==255,
        "generated indices lost uint16 upper boundary");
    }
    const auto strip_indices=TriangleStripIndices16(5);
    const auto generated_strip=generated_indices.Get(NativeIndexPattern::Strip,5);
    Require(std::equal(generated_strip->bytes().begin(),generated_strip->bytes().end(),strip_indices.begin(),strip_indices.end()),
      "generated strip winding differs");
    NativeGeneratedIndexCache tiny_patterns(1,12);
    const auto retained=tiny_patterns.Get(NativeIndexPattern::Quads,4);
    tiny_patterns.Get(NativeIndexPattern::Lines,2);
    Require(tiny_patterns.Get(NativeIndexPattern::Quads,4)!=retained && retained->bytes().size()==12,
      "pattern eviction changed retained generation");
    const auto oversized=tiny_patterns.Get(NativeIndexPattern::Quads,8);
    Require(tiny_patterns.Get(NativeIndexPattern::Quads,8)!=oversized,"over-budget pattern retained in cache");
    for(bool textured:{false,true}) {
      ShaderBindings utility_vs(*device.Get(),CompileNativeShader(*device.Get(),utility,
        {false,textured?"VS_2DTex":"VS_2D","vs_5_0"},"utility_contract.fx"));
      ShaderBindings utility_ps(*device.Get(),CompileNativeShader(*device.Get(),utility,
        {true,textured?"PS_Tex":"PS_Main","ps_5_0"},"utility_contract.fx"));
      ValidateNativeShaderLink(utility_vs.shader(),utility_ps.shader());
      utility_vs.SetGuestFloatRegisters("_g_DX2DScale",Guest(std::array<float,4>{.5f,-1,0,0}));
      utility_vs.SetGuestFloatRegisters("_g_DX2DOffset",Guest(std::array<float,4>{-1,1,0,0}));
      if(textured) { utility_ps.SetTexture("m_Texture",utility_backend); utility_ps.SetSampler("m_Sampler",utility_sampler); }
      // Asked of the backend handle the binding actually holds. It used to be
      // asked of a D3D11 resource, which is no longer what is bound.
      Require(utility_ps.UsesTexture(*utility_backend)==textured &&
        !utility_ps.UsesTextureResource(*target.surface.Get()),"Utility texture resource identity");
      const size_t stride=textured?20:12;
      std::vector<uint8_t> utility_decl(textured?36:24),utility_vertices(stride*4);
      word(utility_decl,4,textured?0x2c23a5:0x2a23b9);
      const size_t color_element=textured?24:12,color_offset=textured?16:8;
      word(utility_decl,color_element,uint32_t(color_offset)); word(utility_decl,color_element+4,0x182886); word(utility_decl,color_element+8,0xa0000);
      if(textured) {word(utility_decl,12,8); word(utility_decl,16,0x2c23a5); word(utility_decl,20,0x50000);}
      for(size_t v=0;v<4;++v) {
        word(utility_vertices,v*stride,std::bit_cast<uint32_t>((quad[v*4]+1)*2));
        word(utility_vertices,v*stride+4,std::bit_cast<uint32_t>(1-quad[v*4+1]));
        word(utility_vertices,v*stride+color_offset,0x80402010);
        if(textured) for(size_t c=0;c<2;++c) word(utility_vertices,v*stride+8+c*4,std::bit_cast<uint32_t>(quad[v*4+2+c]));
      }
      const auto native_utility_decl=Utility2DDeclaration(utility_decl,textured);
      auto source_declaration=NativeDeclaration::Create(utility_decl);
      std::array<std::shared_ptr<const NativeDeclaration>,4> converted;
      std::vector<std::thread> conversion_threads;
      for(size_t i=0;i<converted.size();++i)
        conversion_threads.emplace_back([&,i] { converted[i]=source_declaration->Utility2D(textured); });
      for(auto& worker:conversion_threads) worker.join();
      const auto converted_declaration=converted[0];
      for(const auto& result:converted) Require(result==converted_declaration,"Utility conversion published multiple identities");
      Require(std::equal(converted_declaration->bytes().begin(),converted_declaration->bytes().end(),
        native_utility_decl.begin(),native_utility_decl.end()),"owned Utility conversion differs");
      for(int attempt=0;attempt<2;++attempt) {
        bool rejected=false;
        try { source_declaration->Utility2D(!textured); } catch(const std::runtime_error&) { rejected=true; }
        Require(rejected && source_declaration->Utility2D(textured)==converted_declaration,
          "invalid Utility variant poisoned valid cached layout");
      }
      source_declaration.reset();
      NativeMeshCache utility_cache;
      auto acquire_utility=[&]()->NativeIndexedMesh& {
        return utility_cache.Acquire(*backend,utility_vs.shader(),{51,52,53,54,0},
          converted_declaration->bytes(),uint32_t(stride),utility_vertices,generated_quad->bytes(),2,converted_declaration,generated_quad);
      };
      acquire_utility();
      acquire_utility();
      Require(utility_cache.builds()==1 && utility_cache.hits()==1,"Utility identity cache did not reuse mesh");
      Require(utility_cache.source_checks().index_checks==0 && utility_cache.source_checks().index_candidate_bytes==0 &&
        utility_cache.source_checks().vertex_checks==1 &&
        utility_cache.source_checks().vertex_candidate_bytes==utility_vertices.size(),
        "native generated indices were counted as guest source checks");
      for(uint32_t width:{2u,4u}) {
        bool rejected=false;
        try { utility_cache.Acquire(*backend,utility_vs.shader(),{51,52,53,54,0},
          converted_declaration->bytes(),uint32_t(stride),utility_vertices,utility_indices,width,converted_declaration,generated_quad); }
        catch(const std::runtime_error&) { rejected=true; }
        Require(rejected,"generated index identity accepted foreign bytes/width");
      }
      Require(GuestBlockWord(utility_decl.data()+4)==(textured?0x2c23a5u:0x2a23b9u),"Utility conversion changed guest declaration");
      Require(GuestBlockWord(native_utility_decl.data()+4)==0x2c23a5,"Utility only consumes position XY");
      for(size_t bad_offset:{size_t(0),size_t(4),size_t(8),color_element,color_element+4,color_element+8}) {
        auto bad=utility_decl; bad[bad_offset]^=1;
        bool rejected=false;
        try { Utility2DDeclaration(bad,textured); } catch(const std::runtime_error&) {rejected=true;}
        Require(rejected,"Utility accepted unsupported layout");
      }
      auto padded=utility_decl; padded[11]=0xff; padded[color_element+11]=0x35;
      Require(Utility2DDeclaration(padded,textured).size()==utility_decl.size(),"Utility declaration padding");
      auto& utility_mesh=acquire_utility();
      utility_vs.Bind(*context.Get()); utility_ps.Bind(*context.Get()); utility_mesh.Draw(*context.Get(),0,6);
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
        const std::array<float,4> tint{64.f/255,32.f/255,16.f/255,128.f/255};
        for(size_t c=0;c<4;++c) {
          const float factor=textured?utility_pixels[(x/2)*4+c]/255.f:1.f;
          Require(std::abs(actual[c]-tint[c]*factor)<.001f,"Utility packed color/UV/position mismatch");
        }
      }
      utility_mesh.DrawLines(*context.Get(),0,2);
      D3D11_PRIMITIVE_TOPOLOGY utility_topology{}; context->IAGetPrimitiveTopology(&utility_topology);
      Require(utility_topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Utility line draw used triangle topology");
      // The recorded Utility draw as the bridge makes it with transient
      // batching on (DrawTransientExpanded: the converted vertices written out
      // in index order, drawn non-indexed) against the indexed transient draw
      // it replaces: the same primitives from the same converted vertices, in
      // the same order, for quads and for lines, over two quads' vertices.
      {
        NativeMeshCache dynamic_meshes(4*1024*1024,256,true);
        // The pipelines the bridge binds: batchable (decided, no SV_VertexID),
        // one that numbers its vertices, and one not yet decided.
        NativeBackendPipeline batchable,numbered,undecided;
        batchable.transient_batchable=batchable.transient_batchable_known=true;
        numbered.transient_batchable_known=true;
        auto two_quads=utility_vertices;
        two_quads.insert(two_quads.end(),utility_vertices.rbegin(),utility_vertices.rend());
        for(const bool lines:{false,true}) {
          const auto pattern=generated_indices.Get(lines?NativeIndexPattern::Lines:NativeIndexPattern::Quads,8);
          AssemblyRecorder indexed,expanded;
          for(size_t at=0;at<pattern->bytes().size();at+=2)
            indexed.index_values.push_back((uint32_t(pattern->bytes()[at])<<8)|pattern->bytes()[at+1]);
          auto& mesh=dynamic_meshes.Acquire(*backend,utility_vs.shader(),{61,62,63,lines?2u:13u,0},
            converted_declaration->bytes(),uint32_t(stride),two_quads,pattern->bytes(),2,converted_declaration,pattern,
            {},{},{},{},0,{},&indexed);
          const auto count=uint32_t(pattern->bytes().size()/2);
          if(lines) mesh.DrawLinesTransient(indexed,two_quads,0,count);
          else mesh.DrawTransient(indexed,two_quads,0,count);
          const auto topology=lines?NativeBackendTopology::LineList:NativeBackendTopology::TriangleList;
          Require(mesh.DrawTransientExpanded(expanded,batchable,two_quads,0,count,topology),
            "a batchable pipeline's Utility draw was not expanded");
          Require(indexed.indexed_draws==1 && indexed.draws==0 && expanded.draws==1 && expanded.indexed_draws==0 &&
            expanded.stride==indexed.stride,"expanded Utility draw was not one non-indexed draw of the same stride");
          Require(indexed.primitives.size()==4 && indexed.primitives==expanded.primitives,
            "expanded Utility draw assembles different primitives than the indexed one");
          // A pipeline that is not batchable (reads SV_VertexID, or is not yet
          // decided) takes the indexed draw instead: the same primitives,
          // and the vertex numbering the shader expects.
          for(const auto* pipeline:{&numbered,&undecided}) {
            AssemblyRecorder fallback;
            fallback.index_values=indexed.index_values;
            Require(!mesh.DrawTransientExpanded(fallback,*pipeline,two_quads,0,count,topology),
              "a non-batchable pipeline's Utility draw was expanded");
            Require(fallback.indexed_draws==1 && fallback.draws==0 && fallback.primitives==indexed.primitives,
              "a non-batchable pipeline's Utility draw was not the indexed draw");
          }
          bool refused=false;
          try { mesh.DrawTransientExpanded(expanded,batchable,two_quads,0,count,NativeBackendTopology::TriangleStrip); }
          catch(const std::runtime_error&) { refused=true; }
          Require(refused,"an expanded transient draw accepted a strip");
        }
        // A bucketed effect draw (DrawTransientPrefix, edf_native_effect_mesh_buckets):
        // a mesh built from zeros for 16 vertices, drawn over two quads'
        // vertices with those 8 vertices' own index range, assembles the
        // primitives of the exact 8-vertex mesh's transient draw; an index past
        // the vertices given is refused.
        {
          const auto exact=generated_indices.Get(NativeIndexPattern::Quads,8);
          const auto bucket=generated_indices.Get(NativeIndexPattern::Quads,16);
          AssemblyRecorder reference,prefix;
          const auto values=[](const std::shared_ptr<const NativeGeneratedIndices>& pattern) {
            std::vector<uint32_t> out;
            for(size_t at=0;at<pattern->bytes().size();at+=2) out.push_back((uint32_t(pattern->bytes()[at])<<8)|pattern->bytes()[at+1]);
            return out;
          };
          reference.index_values=values(exact); prefix.index_values=values(bucket);
          auto& exact_mesh=dynamic_meshes.Acquire(*backend,utility_vs.shader(),{71,72,73,13,0},
            converted_declaration->bytes(),uint32_t(stride),two_quads,exact->bytes(),2,converted_declaration,exact,
            {},{},{},{},0,{},&reference);
          exact_mesh.DrawTransient(reference,two_quads,0,12);
          const std::vector<uint8_t> zeros(two_quads.size()*2,0);
          auto& bucket_mesh=dynamic_meshes.Acquire(*backend,utility_vs.shader(),{74,72,73,13,0},
            converted_declaration->bytes(),uint32_t(stride),zeros,bucket->bytes(),2,converted_declaration,bucket,
            {},{},{},{},0,{},&prefix);
          bucket_mesh.DrawTransientPrefix(prefix,two_quads,0,12);
          Require(reference.indexed_draws==1 && prefix.indexed_draws==1 && prefix.stride==reference.stride &&
            reference.primitives.size()==4 && prefix.primitives==reference.primitives,
            "a prefix draw of a bucketed mesh assembles different primitives than the exact mesh");
          bool refused=false;
          try { bucket_mesh.DrawTransientPrefix(prefix,two_quads,0,18); }
          catch(const std::runtime_error&) { refused=true; }
          Require(refused,"a prefix draw accepted an index past its vertices");
        }
      }
      // Scene overlays have a real depth attachment, unlike ordinary output.
      // Exercise both clip-space variants with enabled depth testing, including
      // an opposite comparison that must reject every fragment.
      for(bool reversed:{false,true}) {
        ShaderBindings scene_vs(*device.Get(),CompileNativeShader(*device.Get(),utility,
          {false,textured?"VS_2DTex":"VS_2D","vs_5_0"},"utility_contract.fx",reversed));
        scene_vs.SetGuestFloatRegisters("_g_DX2DScale",Guest(std::array<float,4>{.5f,-1,0,0}));
        scene_vs.SetGuestFloatRegisters("_g_DX2DOffset",Guest(std::array<float,4>{-1,1,0,0}));
        NativeIndexedMesh scene_mesh(*backend,scene_vs.shader(),native_utility_decl,
          uint32_t(stride),utility_vertices,utility_indices,2);
        auto scene_depth=CreateNativeDepthTarget(*backend,4,2,DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
        context->OMSetRenderTargets(1,&rtv,scene_depth.target.Get());
        MakeNativeDrawViewport(0,0,4,2,reversed?1.f:0.f,reversed?0.f:1.f,false,{}).Bind(*context.Get());
        scene_vs.Bind(*context.Get()); utility_ps.Bind(*context.Get());
        for(bool visible:{true,false}) {
          const bool greater=reversed==visible;
          auto scene_state=CreateNativeRenderState(*device.Get(),{0x10001,greater?0x42u:0x12u,0,0,15,0});
          scene_state.Bind(*context.Get());
          ClearNativeDepthTarget(*context.Get(),scene_depth,true,false,.5f,0);
          const float clear[]{-2,-2,-2,-2}; context->ClearRenderTargetView(rtv,clear);
          scene_mesh.Draw(*context.Get(),0,6);
          for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
            const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
            const std::array<float,4> tint{64.f/255,32.f/255,16.f/255,128.f/255};
            for(size_t c=0;c<4;++c) {
              const float factor=textured?utility_pixels[(x/2)*4+c]/255.f:1.f;
              Require(std::abs(actual[c]-(visible?tint[c]*factor:-2.f))<.001f,
                "Utility scene depth/clip variant mismatch");
            }
          }
        }
      }
      context->OMSetRenderTargets(1,&rtv,nullptr);
      context->OMSetDepthStencilState(nullptr,0); context->RSSetState(raster_state.Get());
      context->RSSetViewports(1,&viewport);
      utility_vs.Bind(*context.Get()); utility_ps.Bind(*context.Get());
      // Reorder the perimeter quad into a strip and exercise the converted
      // indices through the real mesh/input-layout/DrawIndexed path.
      auto strip_vertices=utility_vertices;
      for(size_t destination=0;destination<4;++destination) {
        constexpr size_t order[]{0,1,3,2};
        std::copy_n(utility_vertices.begin()+order[destination]*stride,stride,
          strip_vertices.begin()+destination*stride);
      }
      const auto strip_indices=TriangleStripIndices16(4);
      NativeIndexedMesh strip_mesh(*backend,utility_vs.shader(),native_utility_decl,uint32_t(stride),strip_vertices,strip_indices,2);
      ClearNativeColorTarget(*context.Get(),target,0xff000000);
      strip_mesh.Draw(*context.Get(),0,6);
      for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
        const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
        const std::array<float,4> tint{64.f/255,32.f/255,16.f/255,128.f/255};
        for(size_t c=0;c<4;++c) {
          const float factor=textured?utility_pixels[(x/2)*4+c]/255.f:1.f;
          Require(std::abs(actual[c]-tint[c]*factor)<.001f,"triangle-strip mesh pixels differ from quad");
        }
      }
    }
    // Recovered particle contract: POSITION is the center, TEXCOORD1 contains
    // angle/radius, and COLOR is float4 (not packed ARGB). Test both PS variants
    // and reversed clip depth through the actual indexed mesh pipeline.
    Effect particle;
    particle.source=R"(
float4x4 g_mView; float4x4 g_mProjection;
struct P { float4 position:SV_POSITION; float2 uv:TEXCOORD0; float4 color:COLOR0; };
P Vs_Particle(float3 center:POSITION0,float2 uv:TEXCOORD0,float2 param:TEXCOORD1,float4 color:COLOR0) {
  float2 sc; sincos(param.x,sc.x,sc.y); sc*=param.y;
  float3 p=mul(float4(center,1),(float4x3)g_mView); p+=float3(sc,0);
  P o; o.position=mul(float4(p,1),g_mProjection); o.uv=uv; o.color=color; return o;
}
P VS_3DTex(float3 position:POSITION0,float2 uv:TEXCOORD0,float4 color:COLOR0) {
  float3 p=mul(float4(position,1),(float4x3)g_mView);
  P o; o.position=mul(float4(p,1),g_mProjection); o.uv=uv; o.color=color; return o;
}
Texture2D<float4> m_Texture; SamplerState m_Sampler;
float4 Ps_Particle(P i):SV_TARGET { return m_Texture.Sample(m_Sampler,i.uv)*i.color; }
float4 Ps_ZParticle(P i):SV_TARGET {
  float4 c=m_Texture.Sample(m_Sampler,i.uv); clip(c.w-.5f); c.w=1; return c*i.color;
}
)";
    const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const std::array<uint8_t,8> particle_pixels{255,64,0,255,0,255,128,64};
    const D3D11_SUBRESOURCE_DATA particle_data{particle_pixels.data(),8,0};
    ComPtr<ID3D11Texture2D> particle_texture; ComPtr<ID3D11ShaderResourceView> particle_view;
    Require(SUCCEEDED(device->CreateTexture2D(&utility_desc,&particle_data,&particle_texture)),"particle texture");
    Require(SUCCEEDED(device->CreateShaderResourceView(particle_texture.Get(),nullptr,&particle_view)),"particle texture view");
    std::shared_ptr<NativeBackendTexture> particle_backend=
      backend->CreateTexture(utility_backend_desc,particle_pixels);
    Require(bool(particle_backend),"particle backend texture");
    std::vector<uint8_t> particle_decl(48);
    for(size_t e=0;e<4;++e) {
      constexpr uint32_t offsets[]{0,12,20,28},types[]{0x2a23b9,0x2c23a5,0x2c23a5,0x1a23a6},semantics[]{0,0x50000,0x50100,0xa0000};
      word(particle_decl,e*12,offsets[e]); word(particle_decl,e*12+4,types[e]); word(particle_decl,e*12+8,semantics[e]);
    }
    std::vector<float> particle_floats;
    const std::array<float,4> particle_tint{.25f,.5f,.75f,.8f};
    for(size_t v=0;v<4;++v) {
      const float x=quad[v*4],y=quad[v*4+1];
      const std::array<float,11> vertex{0,0,.25f,quad[v*4+2],quad[v*4+3],
        std::atan2(x,y),std::sqrt(2.f),particle_tint[0],particle_tint[1],particle_tint[2],particle_tint[3]};
      particle_floats.insert(particle_floats.end(),vertex.begin(),vertex.end());
    }
    const auto particle_vertices=Guest(particle_floats);
    for(bool billboard:{false,true}) for(bool reversed:{false,true}) for(bool strip:{false,true}) {
      if(billboard && strip) continue; // Observed particles use quads only.
      ShaderBindings particle_vs(*device.Get(),CompileNativeShader(*device.Get(),particle,
        {false,billboard?"Vs_Particle":"VS_3DTex","vs_5_0"},"particle_contract.fx",reversed));
      particle_vs.SetGuestFloatRegisters("g_mView",Guest(identity));
      particle_vs.SetGuestFloatRegisters("g_mProjection",Guest(identity));
      auto scene_decl=particle_decl,scene_vertices=particle_vertices;
      if(!billboard) {
        scene_decl.resize(36);
        word(scene_decl,24,20); word(scene_decl,28,0x1a23a6); word(scene_decl,32,0xa0000);
        std::vector<float> textured_vertices;
        for(size_t v=0;v<4;++v) {
          const std::array<float,9> vertex{quad[v*4],quad[v*4+1],.25f,quad[v*4+2],quad[v*4+3],
            particle_tint[0],particle_tint[1],particle_tint[2],particle_tint[3]};
          textured_vertices.insert(textured_vertices.end(),vertex.begin(),vertex.end());
        }
        scene_vertices=Guest(textured_vertices);
      }
      std::vector<uint8_t> scene_indices(utility_indices.begin(),utility_indices.end());
      if(strip) {
        const auto perimeter=scene_vertices;
        constexpr size_t order[]{0,1,3,2};
        for(size_t v=0;v<4;++v)
          std::copy_n(perimeter.begin()+order[v]*36,36,scene_vertices.begin()+v*36);
        scene_indices=TriangleStripIndices16(4);
      }
      NativeMeshCache immediate_cache(4*1024*1024,256,true);
      const auto content_key=ImmediateMeshKey(scene_vertices,2,3,strip?6:13,reversed);
      const auto immediate_key=ImmediateStreamKey(scene_vertices.size(),2,3,strip?6:13,reversed);
      Require(ImmediateStreamKey(scene_vertices.size()+4,2,3,strip?6:13,reversed)!=immediate_key &&
        ImmediateStreamKey(scene_vertices.size(),2,3,strip?6:13,!reversed)!=immediate_key,
        "dynamic stream key ignores vertex size/depth variant");
      const auto copied_vertices=scene_vertices;
      Require(ImmediateMeshKey(copied_vertices,2,3,strip?6:13,reversed)==content_key,
        "identical immediate bytes at different addresses use different keys");
      auto changed_vertices=scene_vertices; changed_vertices.back()^=1;
      Require(ImmediateMeshKey(changed_vertices,2,3,strip?6:13,reversed)!=content_key &&
        ImmediateMeshKey(scene_vertices,2,3,strip?6:13,!reversed)!=content_key,
        "immediate key ignores contents/depth variant");
      immediate_cache.Acquire(*backend,particle_vs.shader(),immediate_key,scene_decl,billboard?44:36,
        scene_vertices,scene_indices,2);
      auto& particle_mesh=immediate_cache.Acquire(*backend,particle_vs.shader(),immediate_key,
        scene_decl,billboard?44:36,scene_vertices,scene_indices,2);
      Require(immediate_cache.builds()==1 && immediate_cache.hits()==1,"immediate mesh exact-byte reuse");
      // A/B/A changes reuse one layout and buffer, uploading both mutations.
      const auto alternate_key=ImmediateStreamKey(changed_vertices.size(),2,3,strip?6:13,reversed);
      immediate_cache.Acquire(*backend,particle_vs.shader(),alternate_key,scene_decl,billboard?44:36,
        changed_vertices,scene_indices,2);
      auto& restored_mesh=immediate_cache.Acquire(*backend,particle_vs.shader(),immediate_key,
        scene_decl,billboard?44:36,scene_vertices,scene_indices,2);
      Require(&restored_mesh==&particle_mesh && immediate_cache.builds()==1 && immediate_cache.hits()==1 &&
        immediate_cache.updates()==2 && immediate_cache.entries()==1,"alternating dynamic contents rebuilt geometry");
      particle_vs.Bind(*context.Get());
      const auto positions=particle_mesh.CaptureClipPositions(*context.Get(),particle_vs.shader(),0,6,0);
      const std::array<size_t,6> lanes=strip?std::array<size_t,6>{0,1,3,3,1,2}:std::array<size_t,6>{0,1,2,0,2,3};
      for(size_t i=0;i<6;++i) {
        Require(std::abs(positions[i][0]-quad[lanes[i]*4])<.001f &&
          std::abs(positions[i][1]-quad[lanes[i]*4+1])<.001f &&
          std::abs(positions[i][2]-(reversed?.75f:.25f))<.001f &&
          std::abs(positions[i][3]-1.f)<.001f,"particle billboard clip position");
      }
      for(bool clipped:{false,true}) {
        ShaderBindings particle_ps(*device.Get(),CompileNativeShader(*device.Get(),particle,
          {true,clipped?"Ps_ZParticle":"Ps_Particle","ps_5_0"},"particle_contract.fx"));
        particle_ps.SetTexture("m_Texture",particle_backend); particle_ps.SetSampler("m_Sampler",utility_sampler);
        context->OMSetRenderTargets(1,&rtv,nullptr); context->OMSetBlendState(nullptr,nullptr,UINT32_MAX);
        context->OMSetDepthStencilState(nullptr,0); context->RSSetState(raster_state.Get()); context->RSSetViewports(1,&viewport);
        const float clear[]{-2,-2,-2,-2}; context->ClearRenderTargetView(rtv,clear);
        particle_vs.Bind(*context.Get()); particle_ps.Bind(*context.Get()); particle_mesh.Draw(*context.Get(),0,6);
        for(uint32_t y=0;y<2;++y) for(uint32_t x=0;x<4;++x) {
          const auto actual=ReadNativeColorPixel(*context.Get(),*target.surface.Get(),x,y);
          for(size_t c=0;c<4;++c) {
            const float sample=clipped && c==3?1.f:particle_pixels[(x/2)*4+c]/255.f;
            const float expected=clipped && x>=2?-2.f:sample*particle_tint[c];
            Require(std::abs(actual[c]-expected)<.001f,"particle alpha clip/color/UV mismatch");
          }
        }
      }
    }
    bool rejected = false;
    try { stream.Draw(*context.Get(),std::span(bytes).first(63)); }
    catch (const std::runtime_error&) { rejected = true; }
    Require(rejected,"incomplete guest quad accepted");
    std::cout << "Native guest quad conversion, UV interpolation, stream reuse and growth passed\n";
    std::cout << "Native indexed static mesh, padded declarations, subsets and 16/32-bit indices passed\n";
    std::cout << "Native mesh cache reuse, byte updates, shader reload, destruction and budget eviction passed\n";
    std::cout << "Native D24S8/D32/D32S8 depth visibility, read-only and disabled depth passed\n";
    std::cout << "Native UBYTE4 indices and normalized D3DCOLOR expansion passed\n";
    std::cout << "Native signed/unsigned bone indices, weighted matrix palettes and reversed shaders passed\n";
    std::cout << "Native absent-semantic (0,0,0,1) defaults for float/int/uint and reversed shaders passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
