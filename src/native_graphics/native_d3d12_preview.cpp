#include "native_d3d12_preview.h"
#include "guest_shader_bridge.h"
#include "native_backend_frame.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <rex/logging.h>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace edf::native {
namespace {
// Compiled here rather than loaded from the disc: this composite is the
// renderer's own, not the game's, exactly like the D3D11 compositor it mirrors.
const char* kComposite=R"(
cbuffer Fit : register(b0) { float2 scale; float2 pad; };
Texture2D image : register(t0);
SamplerState filtering : register(s0);
struct Varying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Varying VS(float3 position : POSITION) {
  Varying output;
  output.position = float4(position.xy * scale, 0, 1);
  output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
  return output;
}
float4 PS(Varying input) : SV_TARGET { return float4(image.Sample(filtering, input.uv).rgb, 1); }
)";

ComPtr<ID3DBlob> Compile(const char* entry,const char* profile) {
  ComPtr<ID3DBlob> code,errors;
  if(FAILED(D3DCompile(kComposite,std::strlen(kComposite),nullptr,nullptr,nullptr,entry,profile,0,0,
                       &code,&errors)))
    throw std::runtime_error(std::string("D3D12 preview shader compile failed: ")+
                             (errors?static_cast<const char*>(errors->GetBufferPointer()):"?"));
  return code;
}
}  // namespace

LRESULT CALLBACK NativeD3D12Preview::WindowProcedure(HWND window,UINT message,WPARAM w,LPARAM l) {
  if(message==WM_CLOSE) {
    if(auto* self=reinterpret_cast<NativeD3D12Preview*>(GetWindowLongPtrW(window,GWLP_USERDATA)))
      self->closed_=true;
    return 0;
  }
  return DefWindowProcW(window,message,w,l);
}

NativeD3D12Preview::NativeD3D12Preview(const std::string& backend_name) {
  if(backend_name=="d3d11")
    throw std::runtime_error("the backend preview cannot use the adopted d3d11 backend: it shares the "
                             "renderer's immediate context, which only one thread may drive");
  owned_backend_=CreateNativeRenderBackend(backend_name);
  backend_=owned_backend_.get();
  thread_=std::thread([this] {
    try { Run(); }
    catch(const std::exception& error) {
      // A preview failure must not take the game with it. It is a second
      // window; the renderer it is previewing is unaffected.
      REXLOG_ERROR("D3D12 preview stopped: {}",error.what());
    }
  });
}

NativeD3D12Preview::~NativeD3D12Preview() {
  stop_.store(true,std::memory_order_relaxed);
  if(thread_.joinable()) thread_.join();
}

void NativeD3D12Preview::Run() {
  WNDCLASSW window_class{};
  window_class.lpfnWndProc=WindowProcedure;
  window_class.hInstance=GetModuleHandleW(nullptr);
  window_class.lpszClassName=L"EDF2027NativeD3D12Preview";
  window_class.hCursor=LoadCursor(nullptr,IDC_ARROW);
  window_class.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  if(!RegisterClassW(&window_class) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
    throw std::runtime_error("D3D12 preview window class registration failed");
  // Created on this thread because this thread pumps its messages.
  window_=CreateWindowExW(WS_EX_NOACTIVATE,window_class.lpszClassName,
    L"EDF2027 - drawn and presented by the D3D12 backend",WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,CW_USEDEFAULT,960,580,nullptr,nullptr,window_class.hInstance,nullptr);
  if(!window_) throw std::runtime_error("D3D12 preview window creation failed");
  SetWindowLongPtrW(window_,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(this));
  ShowWindow(window_,SW_SHOWNOACTIVATE);
  CreateResources();

  while(!stop_.load(std::memory_order_relaxed) && Tick())
    Sleep(8);

  if(window_) { DestroyWindow(window_); window_=nullptr; }
}

void NativeD3D12Preview::CreateResources() {
  // A triangle that covers the whole clip space; the vertex shader scales it
  // to preserve the frame's aspect inside whatever the window happens to be.
  const float cover[]={-1.0f,-1.0f,0.0f, 3.0f,-1.0f,0.0f, -1.0f,3.0f,0.0f};
  NativeBackendBufferDesc vertex_desc{};
  vertex_desc.bytes=sizeof(cover);
  vertex_desc.vertex=true;
  vertices_=backend_->CreateBuffer(vertex_desc,
    {reinterpret_cast<const uint8_t*>(cover),sizeof(cover)});

  const auto vertex=Compile("VS","vs_5_0");
  const auto pixel=Compile("PS","ps_5_0");
  static const NativeBackendInputElement layout[]={
    {"POSITION",0,6 /*DXGI_FORMAT_R32G32B32_FLOAT*/,0,0,false,0}};
  NativeBackendPipelineDesc pipeline_desc{};
  pipeline_desc.vertex={static_cast<const uint8_t*>(vertex->GetBufferPointer()),vertex->GetBufferSize()};
  pipeline_desc.pixel={static_cast<const uint8_t*>(pixel->GetBufferPointer()),pixel->GetBufferSize()};
  pipeline_desc.vertex_id=0xD12E1;
  pipeline_desc.pixel_id=0xD12E2;
  pipeline_desc.input_layout=layout;
  pipeline_desc.input_layout_id=0xD12E3;
  // No blending, no depth, solid, all channels - the same state the D3D11
  // compositor uses for the same job.
  pipeline_desc.state={0x10001,0,0,0,15,0};
  pipeline_desc.topology=NativeBackendTopology::TriangleList;
  pipeline_desc.render_targets=1;
  pipeline_desc.rtv_format[0]=28;  // DXGI_FORMAT_R8G8B8A8_UNORM, the swap chain's.
  pipeline_=&backend_->CreatePipeline(pipeline_desc);

  NativeBackendSamplerDesc sampler_desc{};
  sampler_desc.u=sampler_desc.v=sampler_desc.w=NativeBackendAddress::Clamp;
  sampler_=&backend_->CreateSampler(sampler_desc);
}

void NativeD3D12Preview::Draw(const uint8_t* pixels,uint32_t width,uint32_t height,uint32_t format) {
  if(frame_width_!=width || frame_height_!=height || frame_format_!=format) {
    NativeBackendTextureDesc desc{};
    desc.width=width;
    desc.height=height;
    desc.levels=1;
    // Taken from the frame that was actually published, never assumed. It was
    // assumed once, as BGRA against an RGBA source, and the whole window came
    // out with red and blue swapped - which looks like a colour-grading bug
    // rather than a one-word mistake.
    desc.format=format;
    frame_=backend_->CreateTexture(desc,{});
    frame_width_=width;
    frame_height_=height;
    frame_format_=format;
  }
  {
    // The copy path's own upload. Kept out of Composite so the shared path
    // does not pay for a texture it never writes.
    backend_->BeginFrame();
    backend_->Recorder().UpdateTexture(*frame_,{pixels,static_cast<size_t>(width)*height*4});
    backend_->Submit();
  }
  Composite(*frame_,width,height);
}

void NativeD3D12Preview::Composite(NativeBackendTexture& frame,uint32_t width,uint32_t height) {
  auto* back=backend_->BackBuffer();
  if(!back) return;
  if(source_generation_) {
    if(!compositor_) compositor_=std::make_unique<NativeBackendCompositor>(*backend_);
    backend_->BeginFrame();
    compositor_->Draw(backend_->Recorder(),frame,*back,true,gamma_?&*gamma_:nullptr);
    backend_->Submit(); backend_->Present(false);
    presented_.fetch_add(1,std::memory_order_relaxed);
    return;
  }
  NativeBackendRenderTarget* colors[]={back};

  // Letterbox rather than stretch, matching the D3D11 compositor: a preview
  // that silently changed the aspect would make a rendering difference look
  // like a geometry bug.
  const float window_aspect=float(window_width_)/float(window_height_);
  const float frame_aspect=float(width)/float(height);
  const float scale_x=frame_aspect>window_aspect?1.0f:frame_aspect/window_aspect;
  const float scale_y=frame_aspect>window_aspect?window_aspect/frame_aspect:1.0f;
  const float fit[4]{scale_x,scale_y,0,0};

  backend_->BeginFrame();
  auto& recorder=backend_->Recorder();
  recorder.SetRenderTargets(colors,nullptr);
  recorder.SetViewport({0,0,float(window_width_),float(window_height_),0,1});
  recorder.ClearColor(*back,{0,0,0,1});
  recorder.SetPipeline(*pipeline_);
  recorder.SetVertexBuffer(0,*vertices_,sizeof(float)*3,0);
  recorder.SetConstants(NativeBackendStage::Vertex,0,
                        {reinterpret_cast<const uint8_t*>(fit),sizeof(fit)});
  recorder.SetTexture(NativeBackendStage::Pixel,0,&frame);
  recorder.SetSampler(NativeBackendStage::Pixel,0,sampler_);
  recorder.Draw(3,0);
  backend_->Submit();
  backend_->Present(false);
  presented_.fetch_add(1,std::memory_order_relaxed);
}

bool NativeD3D12Preview::DrawShared() {
  if(backend_->name()=="d3d12") {
    VisitNativeBackendFrameMirror(last_sequence_,[&](const NativeBackendPublishedFrame& source) {
      NativeBackendTextureDesc desc{}; desc.width=source.width; desc.height=source.height; desc.format=source.format;
      if(source_generation_!=source.generation) {
        shared_frame_=backend_->OpenSharedTexture(source.texture,desc);
        frame_=backend_->CreateTexture(desc,{});
        if(!shared_frame_) throw std::runtime_error("D3D12 preview scene import failed");
        source_generation_=source.generation; frame_width_=source.width; frame_height_=source.height;
      }
      if(!copied_) {
        desc.width=desc.height=1; copied_=backend_->CreateSharedSurface(desc);
        if(!copied_) throw std::runtime_error("D3D12 preview copy fence creation failed");
      }
      if(!backend_->WaitSharedFence(source.fence,source.value))
        throw std::runtime_error("D3D12 preview scene fence import failed");
      backend_->BeginFrame();
      backend_->Recorder().CopyTexture(*frame_,*shared_frame_);
      backend_->Recorder().ReleaseSharedTexture(*shared_frame_);
      backend_->Submit(); backend_->SignalShared(*copied_);
      last_sequence_=source.sequence; gamma_=source.gamma;
      return NativeBackendFrameCopied{copied_->fence_handle(),copied_->value(),backend_->MarkCompletion()};
    });
    if(source_generation_) {
      Composite(*frame_,frame_width_,frame_height_);
      if(presented_.load(std::memory_order_relaxed)%120==1)
        REXLOG_INFO("D3D12 preview: owned GPU snapshot, gamma applied; frame={}x{}, sequence={}",
          frame_width_,frame_height_,last_sequence_);
      return true;
    }
  }
  if(shared_refused_) return false;
  NativeFrameHandoff::SharedFrame shared;
  uint64_t sequence=0;
  if(!VisitNativePresentationSharedFrame(shared,sequence) || !shared) return false;
  if(sequence==last_sequence_) return true;  // Nothing new; not a failure.

  if(!shared_frame_ || shared_handle_!=shared.texture) {
    NativeBackendTextureDesc desc{};
    desc.width=shared.width;
    desc.height=shared.height;
    desc.levels=1;
    desc.format=shared.format;
    shared_frame_=backend_->OpenSharedTexture(shared.texture,desc);
    if(!shared_frame_) {
      // Said once. A backend that cannot import handles is a fact about the
      // backend, not a per-frame event worth repeating 60 times a second.
      REXLOG_INFO("D3D12 preview: the {} backend cannot open a shared surface; using the copy path",
        std::string(backend_->name()));
      shared_refused_=true;
      return false;
    }
    shared_handle_=shared.texture;
    frame_width_=shared.width;
    frame_height_=shared.height;
  }
  // Wait for the publication to finish on the producing GPU before sampling
  // it. Without this the surface is read mid-copy.
  if(!backend_->WaitSharedFence(shared.fence,shared.value)) {
    REXLOG_INFO("D3D12 preview: the {} backend cannot wait on the shared fence; using the copy path",
      std::string(backend_->name()));
    shared_refused_=true;
    shared_frame_.reset();
    return false;
  }
  Composite(*shared_frame_,shared.width,shared.height);
  last_sequence_=sequence;
  if(presented_.load(std::memory_order_relaxed)%120==1)
    REXLOG_INFO("D3D12 preview: shared surface, no copy; frame={}x{} format={}, window={}x{}, sequence={}; drawn and presented by the {} backend",
      shared.width,shared.height,shared.format,window_width_,window_height_,sequence,
      std::string(backend_->name()));
  return true;
}

bool NativeD3D12Preview::Tick() {
  MSG message{};
  while(PeekMessageW(&message,window_,0,0,PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if(closed_) return false;
  RECT client{};
  if(!GetClientRect(window_,&client) || client.right<=0 || client.bottom<=0) return true;
  const uint32_t width=uint32_t(client.right),height=uint32_t(client.bottom);
  if(!attached_ || width!=window_width_ || height!=window_height_) {
    backend_->AttachWindow(window_,width,height);
    window_width_=width;
    window_height_=height;
    attached_=true;
  }

  // The shared surface first; the copy below is only for backends that cannot
  // import one.
  if(DrawShared()) return true;

  // Readback of the published snapshot. The renderer's own context mutex is
  // held for the duration by VisitNativePresentationFrame, which is why the
  // copy out is kept to exactly the staging map and nothing else happens here.
  std::vector<uint8_t> pixels;
  uint32_t frame_width=0,frame_height=0,frame_format=0;
  const bool drew=VisitNativePresentationFrame(
    [&](ID3D11Device& device,ID3D11DeviceContext& context,ID3D11ShaderResourceView& view,
        uint64_t sequence,NativeFrameKind,const NativeDisplayGamma*) {
      if(sequence==last_sequence_) return;
      ComPtr<ID3D11Resource> resource;
      view.GetResource(&resource);
      ComPtr<ID3D11Texture2D> texture;
      if(FAILED(resource.As(&texture))) return;
      D3D11_TEXTURE2D_DESC description{};
      texture->GetDesc(&description);
      auto staging=description;
      staging.Usage=D3D11_USAGE_STAGING;
      staging.BindFlags=0;
      staging.MiscFlags=0;
      staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Texture2D> copy;
      if(FAILED(device.CreateTexture2D(&staging,nullptr,&copy))) return;
      context.CopyResource(copy.Get(),texture.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if(FAILED(context.Map(copy.Get(),0,D3D11_MAP_READ,0,&mapped))) return;
      const size_t pitch=static_cast<size_t>(description.Width)*4;
      pixels.resize(pitch*description.Height);
      for(UINT row=0;row<description.Height;++row)
        std::memcpy(pixels.data()+static_cast<size_t>(row)*pitch,
                    static_cast<const uint8_t*>(mapped.pData)+static_cast<size_t>(row)*mapped.RowPitch,
                    pitch);
      context.Unmap(copy.Get(),0);
      frame_width=description.Width;
      frame_height=description.Height;
      frame_format=description.Format;
      last_sequence_=sequence;
    });
  if(!drew || pixels.empty()) return true;
  // Cheap evidence that these are game pixels and not a black surface.
  // "Presented 120 frames" is true of a window showing nothing at all, and
  // that is exactly the failure this path could have and still look healthy.
  uint64_t lit=0,total=0;
  for(size_t at=0;at+3<pixels.size();at+=4*64) {
    total+=1;
    if(pixels[at]>8||pixels[at+1]>8||pixels[at+2]>8) ++lit;
  }
  Draw(pixels.data(),frame_width,frame_height,frame_format);
  const uint64_t count=presented_.load(std::memory_order_relaxed);
  if(count==1 || count%120==0)
    REXLOG_INFO("D3D12 preview: presented={}, frame={}x{} format={}, window={}x{}, sequence={}, non_black={}/{} sampled source pixels; every pixel in this window was drawn and presented by the {} backend",
      count,frame_width,frame_height,frame_format,window_width_,window_height_,last_sequence_,lit,total,
      std::string(backend_->name()));
  return true;
}
}  // namespace edf::native
