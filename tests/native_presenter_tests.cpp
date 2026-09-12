#include "native_graphics/d3d11_presenter.h"
#include "native_graphics/d3d11_frame_compositor.h"
#include "native_graphics/d3d11_frame_handoff.h"
#include "native_graphics/d3d11_texture.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <d3d11sdklayers.h>

using namespace edf::native;
using Microsoft::WRL::ComPtr;
namespace {
void Require(bool condition,const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class F> void Reject(F operation,const char* message) {
  bool rejected=false;
  try { operation(); } catch(const std::runtime_error&) { rejected=true; }
  Require(rejected,message);
}
struct Window {
  HWND handle=CreateWindowExW(0,L"STATIC",L"Native presenter test",WS_OVERLAPPEDWINDOW,
    0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  ~Window() { if(handle) DestroyWindow(handle); }
};
void CheckBuffer(ID3D11Device& device,ID3D11DeviceContext& context,
    ID3D11RenderTargetView& target,UINT width,UINT height) {
  ComPtr<ID3D11Resource> resource;
  target.GetResource(&resource);
  ComPtr<ID3D11Texture2D> texture;
  Require(SUCCEEDED(resource.As(&texture)),"back buffer is not a texture");
  D3D11_TEXTURE2D_DESC desc{};
  texture->GetDesc(&desc);
  Require(desc.Width==width && desc.Height==height && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM,
          "back buffer dimensions/format");
  // Use exact target code values, not UNORM half-way rounding cases whose
  // conversion may differ between WARP and hardware (0.5 -> 127 or 128).
  const float color[]{64/255.f,128/255.f,191/255.f,1};
  context.ClearRenderTargetView(&target,color);
  desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0;
  desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
  ComPtr<ID3D11Texture2D> readback;
  Require(SUCCEEDED(device.CreateTexture2D(&desc,nullptr,&readback)),"readback allocation");
  context.CopyResource(readback.Get(),texture.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};
  Require(SUCCEEDED(context.Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"readback map");
  bool correct=true;
  for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
    const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
    if(correct && !(pixel[0]==64 && pixel[1]==128 && pixel[2]==191 && pixel[3]==255))
      std::cerr << "clear pixel " << x << ',' << y << " RGBA=" << unsigned(pixel[0]) << ','
                << unsigned(pixel[1]) << ',' << unsigned(pixel[2]) << ',' << unsigned(pixel[3]) << '\n';
    correct &= pixel[0]==64 && pixel[1]==128 && pixel[2]==191 && pixel[3]==255;
  }
  context.Unmap(readback.Get(),0);
  Require(correct,"back buffer pixel mismatch");
}
void CheckComposition(ID3D11Device& device,ID3D11DeviceContext& context,
                      NativeWindowPresenter& presenter) {
  NativeFrameCompositor compositor(device);
  auto source=CreateNativeRenderTarget(device,4,2,DXGI_FORMAT_R8G8B8A8_UNORM);
  ClearNativeColorTarget(context,source,0xff4080bfu);
  ResolveNativeRenderTarget(context,source);
  Require(presenter.BeginFrame(8,8),"composition target resize");
  // Hostile prior state must not leak into the presentation pass.
  const float blend_factor[]{0,0,0,0};
  context.OMSetBlendState(nullptr,blend_factor,0);
  context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  compositor.Draw(context,*source.sampled.view.Get(),*presenter.target());
  ComPtr<ID3D11Resource> output;
  presenter.target()->GetResource(&output);
  ComPtr<ID3D11Texture2D> texture;
  Require(SUCCEEDED(output.As(&texture)),"composition texture");
  auto pixel=[&](UINT x,UINT y,const std::array<float,4>& expected) {
    const auto actual=ReadNativeColorPixel(context,*texture.Get(),x,y);
    for(size_t i=0;i<4;++i)
      Require(std::abs(actual[i]-expected[i])<0.002f,"composition pixel mismatch");
  };
  const std::array<float,4> color{64/255.f,128/255.f,191/255.f,1};
  for(UINT y=0;y<8;++y) for(UINT x=0;x<8;++x)
    pixel(x,y,y<2 || y>=6 ? std::array<float,4>{0,0,0,1}:color);
  compositor.Draw(context,*source.sampled.view.Get(),*presenter.target(),false);
  pixel(0,0,color); pixel(7,7,color);
  // Source SRV must be released from the pipeline before it becomes a target.
  ComPtr<ID3D11ShaderResourceView> bound;
  context.PSGetShaderResources(0,1,&bound);
  Require(!bound,"composition retained source binding");
  output.Reset(); texture.Reset();
  Require(presenter.BeginFrame(8,2),"pillarbox target resize");
  compositor.Draw(context,*source.sampled.view.Get(),*presenter.target());
  presenter.target()->GetResource(&output);
  Require(SUCCEEDED(output.As(&texture)),"pillarbox texture");
  for(UINT y=0;y<2;++y) for(UINT x=0;x<8;++x)
    pixel(x,y,x<2 || x>=6 ? std::array<float,4>{0,0,0,1}:color);
  output.Reset(); texture.Reset();
  Require(presenter.BeginFrame(2,2),"orientation target resize");
  auto corners=CreateNativeRenderTarget(device,2,2,DXGI_FORMAT_R8G8B8A8_UNORM);
  const unsigned char rgba[]{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
  context.UpdateSubresource(corners.sampled.resource.Get(),0,nullptr,rgba,8,16);
  compositor.Draw(context,*corners.sampled.view.Get(),*presenter.target(),false);
  presenter.target()->GetResource(&output);
  Require(SUCCEEDED(output.As(&texture)),"orientation texture");
  pixel(0,0,{1,0,0,1}); pixel(1,0,{0,1,0,1});
  pixel(0,1,{0,0,1,1}); pixel(1,1,{1,1,1,1});
  ComPtr<ID3D11Texture2D> mip_texture;
  ComPtr<ID3D11ShaderResourceView> mip_source;
  D3D11_TEXTURE2D_DESC mip_desc{};
  corners.sampled.resource->GetDesc(&mip_desc);
  mip_desc.Width=4; mip_desc.Height=4; mip_desc.MipLevels=3;
  Require(SUCCEEDED(device.CreateTexture2D(&mip_desc,nullptr,&mip_texture)),"mip texture creation");
  context.UpdateSubresource(mip_texture.Get(),1,nullptr,rgba,8,16);
  D3D11_SHADER_RESOURCE_VIEW_DESC mip_view{};
  mip_view.Format=mip_desc.Format; mip_view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
  mip_view.Texture2D.MostDetailedMip=1; mip_view.Texture2D.MipLevels=1;
  Require(SUCCEEDED(device.CreateShaderResourceView(mip_texture.Get(),&mip_view,&mip_source)),
          "mip source creation");
  compositor.Draw(context,*mip_source.Get(),*presenter.target());
  pixel(0,0,{1,0,0,1}); pixel(1,0,{0,1,0,1});
  pixel(0,1,{0,0,1,1}); pixel(1,1,{1,1,1,1});
  ComPtr<ID3D11DeviceContext> deferred;
  Require(SUCCEEDED(device.CreateDeferredContext(0,&deferred)),"compositor deferred context");
  Reject([&]{compositor.Draw(*deferred.Get(),*corners.sampled.view.Get(),*presenter.target());},
         "compositor accepted deferred context");
  auto hdr=CreateNativeRenderTarget(device,2,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
  Reject([&]{compositor.Draw(context,*hdr.sampled.view.Get(),*presenter.target());},
         "untonemapped HDR accepted");
  ComPtr<ID3D11RenderTargetView> alias;
  ComPtr<ID3D11Texture2D> alias_texture;
  ComPtr<ID3D11ShaderResourceView> alias_source;
  D3D11_TEXTURE2D_DESC alias_desc{};
  corners.sampled.resource->GetDesc(&alias_desc);
  alias_desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  Require(SUCCEEDED(device.CreateTexture2D(&alias_desc,nullptr,&alias_texture)),"alias texture creation");
  Require(SUCCEEDED(device.CreateShaderResourceView(alias_texture.Get(),nullptr,&alias_source)),
          "alias source creation");
  Require(SUCCEEDED(device.CreateRenderTargetView(alias_texture.Get(),nullptr,&alias)),
          "alias target creation");
  Reject([&]{compositor.Draw(context,*alias_source.Get(),*alias.Get());},
         "aliased source/target accepted");
  compositor.Draw(context,*corners.sampled.view.Get(),*presenter.target());
  presenter.Present(false);
}
void CheckDisplayGamma(ID3D11Device& device,ID3D11DeviceContext& context) {
  NativeFrameCompositor compositor(device);
  auto source=CreateNativeRenderTarget(device,256,1,DXGI_FORMAT_R8G8B8A8_UNORM);
  auto target=CreateNativeRenderTarget(device,256,1,DXGI_FORMAT_R8G8B8A8_UNORM);
  std::array<uint8_t,1024> pixels{};
  for(unsigned i=0;i<256;++i) {
    pixels[i*4]=pixels[i*4+1]=pixels[i*4+2]=uint8_t(i); pixels[i*4+3]=255;
  }
  context.UpdateSubresource(source.sampled.resource.Get(),0,nullptr,pixels.data(),1024,1024);
  for(auto mode:{NativeDisplayGamma::Mode::Table256,NativeDisplayGamma::Mode::Piecewise128}) {
    std::array<uint8_t,1536> bytes{};
    for(unsigned channel=0;channel<3;++channel) for(unsigned entry=0;entry<256;++entry) {
      const uint16_t value=uint16_t(entry*193+channel*7001);
      const auto at=(channel*256+entry)*2;
      bytes[at]=uint8_t(value>>8); bytes[at+1]=uint8_t(value);
    }
    const auto gamma=NativeDisplayGamma::Decode(bytes,mode);
    compositor.Draw(context,*source.sampled.view.Get(),*target.target.Get(),false,&gamma);
    for(unsigned input:{0u,1u,31u,64u,127u,128u,191u,254u,255u}) {
      const auto result=ReadNativeColorPixel(context,*target.surface.Get(),input,0);
      const auto code=mode==NativeDisplayGamma::Mode::Table256?input:(input*1023+127)/255;
      for(unsigned channel=0;channel<3;++channel) {
        if(std::abs(result[channel]-gamma.EvaluateNormalizedCode(channel,code))>=0.0021f)
          std::cerr << "gamma mode=" << int(mode) << " input=" << input << " channel=" << channel
            << " actual=" << result[channel] << " expected=" << gamma.EvaluateNormalizedCode(channel,code) << '\n';
        Require(std::abs(result[channel]-gamma.EvaluateNormalizedCode(channel,code))<0.0021f,"native GPU gamma mismatch");
      }
      Require(result[3]==1.f,"gamma changed alpha");
    }
  }
  compositor.Draw(context,*source.sampled.view.Get(),*target.target.Get(),false);
  Require(std::abs(ReadNativeColorPixel(context,*target.surface.Get(),64,0)[0]-64/255.f)<0.002f,
    "gamma state leaked into disabled draw");
  // Nonlinear gamma must be applied before bilinear window scaling.
  auto edges=CreateNativeRenderTarget(device,2,1,DXGI_FORMAT_R8G8B8A8_UNORM);
  auto scaled=CreateNativeRenderTarget(device,3,1,DXGI_FORMAT_R8G8B8A8_UNORM);
  const uint8_t edge_pixels[]{0,0,0,255,255,255,255,255};
  context.UpdateSubresource(edges.sampled.resource.Get(),0,nullptr,edge_pixels,8,8);
  std::array<uint8_t,1536> curve{};
  for(unsigned channel=0;channel<3;++channel) for(unsigned i=0;i<256;++i) {
    const auto value=uint16_t((i*i*1023/(255*255))<<6);
    curve[(channel*256+i)*2]=uint8_t(value>>8); curve[(channel*256+i)*2+1]=uint8_t(value);
  }
  const auto gamma=NativeDisplayGamma::Decode(curve,NativeDisplayGamma::Mode::Table256);
  compositor.Draw(context,*edges.sampled.view.Get(),*scaled.target.Get(),false,&gamma);
  Require(std::abs(ReadNativeColorPixel(context,*scaled.surface.Get(),1,0)[0]-0.5f)<0.003f,
    "gamma applied after scaling instead of before");
}
void CheckHandoff(ID3D11Device& device,ID3D11DeviceContext& context,
                  NativeWindowPresenter& presenter) {
  NativeFrameHandoff handoff(device,context);
  static_assert(!std::is_copy_constructible_v<NativeFrameHandoff>);
  Require(!handoff.Visit({}),"empty handoff visited");
  bool empty_context_visited=false;
  handoff.VisitContext([&](auto& d,auto& c) {
    Require(&d==&device,"empty handoff context device");
    c.ClearState(); empty_context_visited=true;
  });
  Require(empty_context_visited,"context access required a published frame");
  Reject([&]{handoff.VisitContext({});},"empty context consumer accepted");
  auto source=CreateNativeRenderTarget(device,4,2,DXGI_FORMAT_R8G8B8A8_UNORM);
  ClearNativeColorTarget(context,source,0xffff0000u);
  handoff.Publish(*source.surface.Get(),NativeFrameKind::Movie);
  ClearNativeColorTarget(context,source,0xff00ff00u);
  auto* render_target=source.target.Get();
  context.OMSetRenderTargets(1,&render_target,nullptr);
  context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  const D3D11_VIEWPORT viewport{1,2,3,4,0.25f,0.75f};
  context.RSSetViewports(1,&viewport);
  const auto restored=[&] {
    ComPtr<ID3D11RenderTargetView> bound;
    context.OMGetRenderTargets(1,&bound,nullptr);
    Require(bound.Get()==render_target,"handoff did not restore target");
    D3D11_PRIMITIVE_TOPOLOGY topology{}; context.IAGetPrimitiveTopology(&topology);
    Require(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"handoff did not restore topology");
    D3D11_VIEWPORT current{}; UINT count=1; context.RSGetViewports(&count,&current);
    Require(count==1 && current.TopLeftX==1 && current.TopLeftY==2 && current.Width==3 &&
            current.Height==4 && current.MinDepth==0.25f && current.MaxDepth==0.75f,
            "handoff did not restore viewport");
  };
  NativeFrameCompositor compositor(device);
  Require(presenter.BeginFrame(4,2),"handoff back-buffer resize");
  // BeginFrame unbinds on resize, so restore the producer binding first.
  context.OMSetRenderTargets(1,&render_target,nullptr);
  Require(handoff.Visit([&](auto& d,auto& c,auto& view,uint64_t sequence,NativeFrameKind kind,auto* gamma) {
    Require(!gamma,"unexpected initial frame gamma");
    Require(&d==&device && sequence==1 && kind==NativeFrameKind::Movie,"handoff metadata");
    ComPtr<ID3D11Resource> resource; view.GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture; Require(SUCCEEDED(resource.As(&texture)),"snapshot texture");
    const auto color=ReadNativeColorPixel(c,*texture.Get(),0,0);
    Require(color[0]==1 && color[1]==0 && color[2]==0,"snapshot changed with producer surface");
    compositor.Draw(c,view,*presenter.target());
    presenter.Present(false);
  }),"published handoff not visited");
  restored();
  Reject([&]{handoff.Visit([&](auto&,auto& c,auto&,auto,auto,auto*) {
    c.ClearState(); throw std::runtime_error("injected consumer failure");
  });},"consumer exception swallowed");
  restored();
  handoff.Invalidate();
  Require(!handoff.Visit({}),"invalidated handoff remained available");
  handoff.Publish(*source.surface.Get(),NativeFrameKind::PartialScene);
  Require(handoff.Visit([&](auto&,auto&,auto&,auto sequence,auto kind,auto*) {
    Require(sequence==2 && kind==NativeFrameKind::PartialScene,"republish metadata");
  }),"republish failed");
  auto hdr=CreateNativeRenderTarget(device,4,2,DXGI_FORMAT_R16G16B16A16_FLOAT);
  Reject([&]{handoff.Publish(*hdr.surface.Get(),NativeFrameKind::PartialScene);},"HDR handoff accepted");
  Require(!handoff.Visit({}),"failed publication exposed stale snapshot");
  auto resized=CreateNativeRenderTarget(device,2,1,DXGI_FORMAT_R8G8B8A8_UNORM);
  ClearNativeColorTarget(context,resized,0xff0000ffu);
  handoff.Publish(*resized.surface.Get(),NativeFrameKind::PartialScene);
  resized={}; // Publication is independent of producer resource lifetime.
  handoff.Visit([&](auto&,auto& c,auto& view,auto sequence,auto,auto*) {
    Require(sequence==3,"resized publication sequence");
    ComPtr<ID3D11Resource> resource; view.GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture; Require(SUCCEEDED(resource.As(&texture)),"resized snapshot");
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    Require(desc.Width==2 && desc.Height==1,"snapshot did not resize");
    const auto color=ReadNativeColorPixel(c,*texture.Get(),1,0);
    Require(color[0]==0 && color[1]==0 && color[2]==1,"resized snapshot contents");
  });
  // Isolated context state must not keep a swap-chain back buffer alive.
  std::array<uint8_t,1536> gamma_bytes{}; gamma_bytes.fill(0xff);
  auto gamma=NativeDisplayGamma::Decode(gamma_bytes,NativeDisplayGamma::Mode::Table256);
  handoff.Publish(*source.surface.Get(),NativeFrameKind::PartialScene,&gamma);
  gamma_bytes.fill(0);
  gamma=NativeDisplayGamma::Decode(gamma_bytes,NativeDisplayGamma::Mode::Table256);
  Require(handoff.Visit([&](auto&,auto&,auto&,auto,auto,const NativeDisplayGamma* captured) {
    Require(captured && captured->EvaluateCode(0,128)==1023,"frame borrowed mutable producer gamma");
  }),"gamma frame unavailable");
  handoff.Publish(*source.surface.Get(),NativeFrameKind::Movie);
  handoff.Visit([&](auto&,auto&,auto&,auto,auto,auto* captured) {
    Require(!captured,"gamma leaked into uncorrected frame");
  });
  Require(presenter.BeginFrame(7,5),"handoff retained back-buffer reference");
}
}
int main(int argc,char** argv) {
  try {
    static_assert(!std::is_copy_constructible_v<NativeWindowPresenter>);
    static_assert(!std::is_copy_assignable_v<NativeWindowPresenter>);
    Window window;
    Require(window.handle!=nullptr,"hidden test window creation");
    const bool hardware=argc==2 && std::string(argv[1])=="--visible-hardware";
    const bool visible=hardware || (argc==2 && std::string(argv[1])=="--visible");
    if(visible) {
      const auto desktop_name=[](HANDLE handle) {
        wchar_t name[256]{}; DWORD needed=0;
        if(!handle || !GetUserObjectInformationW(handle,UOI_NAME,name,sizeof(name),&needed))
          return std::wstring(L"<unavailable>");
        return std::wstring(name);
      };
      const auto input_desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
      std::wcout << L"window_station=" << desktop_name(GetProcessWindowStation())
                 << L" render_desktop=" << desktop_name(GetThreadDesktop(GetCurrentThreadId()))
                 << L" input_desktop=" << desktop_name(input_desktop) << L'\n';
      if(input_desktop) CloseDesktop(input_desktop);
      ShowWindow(window.handle,SW_SHOWNOACTIVATE);
      SetWindowPos(window.handle,HWND_TOPMOST,40,40,160,120,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,visible?D3D11_CREATE_DEVICE_DEBUG:0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device creation");
    Reject([&]{NativeWindowPresenter invalid(nullptr,*device.Get(),*context.Get());},"null HWND accepted");
    ComPtr<ID3D11DeviceContext> deferred;
    Require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"deferred context creation");
    Reject([&]{NativeWindowPresenter invalid(window.handle,*device.Get(),*deferred.Get());},
           "deferred context accepted");
    ComPtr<ID3D11Device> other_device;
    ComPtr<ID3D11DeviceContext> other_context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&other_device,nullptr,&other_context)),"second WARP device creation");
    Reject([&]{NativeWindowPresenter invalid(window.handle,*device.Get(),*other_context.Get());},
           "foreign context accepted");
    {
      NativeWindowPresenter temporary(window.handle,*device.Get(),*context.Get());
      Require(temporary.BeginFrame(8,8),"temporary presenter frame");
      auto* view=temporary.target();
      context->OMSetRenderTargets(1,&view,nullptr);
    }
    // Destruction must release context-held back-buffer references too.
    NativeWindowPresenter presenter(window.handle,*device.Get(),*context.Get());
    Require(!presenter.Present(false),"uninitialized present accepted");
    Require(!presenter.TestVisibility(),"uninitialized visibility accepted");
    Require(!presenter.BeginFrame(0,0),"zero-size frame accepted");
    Require(presenter.BeginFrame(32,16),"initial frame rejected");
    CheckBuffer(*device.Get(),*context.Get(),*presenter.target(),32,16);
    const bool presented=presenter.Present(false); // Hidden clients may be occluded.
    Require(presenter.Diagnostics().last_sync_interval==0,"unsynchronized native present interval");
    presenter.Present(true);
    Require(presenter.Diagnostics().last_sync_interval==1,"native VSync interval not forwarded");
    presenter.Present(false);
    Require(presenter.Diagnostics().last_sync_interval==0,"native VSync could not be disabled again");
    if(visible) {
      unsigned successful=presented?1:0;
      for(unsigned attempt=0;attempt<60;++attempt) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
          TranslateMessage(&message); DispatchMessageW(&message);
        }
        const float clear[]{0.25f,0.5f,0.75f,1};
        context->ClearRenderTargetView(presenter.target(),clear);
        if(presenter.Present(false)) ++successful;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      }
      std::cout << "visible standalone successful presents=" << successful << "/61\n";
      ComPtr<ID3D11InfoQueue> messages;
      if(SUCCEEDED(device.As(&messages))) {
        for(UINT64 i=0;i<messages->GetNumStoredMessagesAllowedByRetrievalFilter() && i<32;++i) {
          SIZE_T size=0; messages->GetMessage(i,nullptr,&size);
          std::vector<unsigned char> bytes(size);
          auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
          if(SUCCEEDED(messages->GetMessage(i,message,&size)))
            std::cout << "D3D11 severity=" << message->Severity << ' ' << message->pDescription << '\n';
        }
      }
      const auto diagnostic=presenter.Diagnostics();
      std::cout << "present_hr=0x" << std::hex << uint32_t(diagnostic.last_present)
                << " output_hr=0x" << uint32_t(diagnostic.output_result) << std::dec
                << " windowed=" << diagnostic.chain.Windowed
                << " swap_effect=" << diagnostic.chain.SwapEffect
                << " hwnd_match=" << (diagnostic.chain.OutputWindow==window.handle)
                << " output_attached=" << diagnostic.output.AttachedToDesktop
                << " output_rect=" << diagnostic.output.DesktopCoordinates.left << ','
                << diagnostic.output.DesktopCoordinates.top << ','
                << diagnostic.output.DesktopCoordinates.right << ','
                << diagnostic.output.DesktopCoordinates.bottom << '\n';
      Require(successful>0,"visible presentation failed: every present was occluded");
    }
    Require(presenter.BeginFrame(32,16),"same-size frame rejected");
    auto* target=presenter.target();
    context->OMSetRenderTargets(1,&target,nullptr);
    Require(presenter.BeginFrame(17,9),"resize rejected");
    CheckBuffer(*device.Get(),*context.Get(),*presenter.target(),17,9);
    presenter.Present(false);
    Require(!presenter.BeginFrame(0,9) && !presenter.Present(false),"suspension failed");
    Require(!presenter.TestVisibility(),"suspended visibility accepted");
    Require(presenter.BeginFrame(17,9),"resume failed");
    Reject([&]{presenter.BeginFrame(0xffffffffu,9);},"oversized client accepted");
    Require(!presenter.Present(false),"failed frame remained presentable");
    bool thread_rejected=false;
    std::thread wrong_thread([&]{
      try { presenter.BeginFrame(17,9); }
      catch(const std::runtime_error&) { thread_rejected=true; }
    });
    wrong_thread.join();
    Require(thread_rejected,"wrong-thread access accepted");
    Require(presenter.BeginFrame(17,9),"recovery after invalid call failed");
    CheckBuffer(*device.Get(),*context.Get(),*presenter.target(),17,9);
    CheckComposition(*device.Get(),*context.Get(),presenter);
    CheckDisplayGamma(*device.Get(),*context.Get());
    CheckHandoff(*device.Get(),*context.Get(),presenter);
    DestroyWindow(window.handle); window.handle=nullptr;
    Reject([&]{presenter.Present(false);},"destroyed window accepted");
    context->ClearState();
    std::cout << "native presenter tests passed\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
