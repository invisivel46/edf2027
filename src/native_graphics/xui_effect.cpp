#include "xui_effect.h"
namespace edf::native {
Effect MakeNativeXuiTextureEffect() {
  Effect effect;
  effect.entries={{false,"VS_XuiTexture","vs_5_0"},{true,"PS_XuiTexture","ps_5_0"},
                  {true,"PS_XuiSolid","ps_5_0"},{true,"PS_XuiAlphaMask","ps_5_0"}};
  effect.source=R"(
cbuffer XuiVertexData {
  float4 TransformRows[4]; // c0..3
  float4 ProjectionRows[4]; // c4..7
  float4 Params; // c8, x adds projected depth
  float4 TextureOffset; // c9
  float4 TextureRows[2]; // c10..11
  float4 ArithmeticBias; // c255; retained explicitly, not assumed zero
};
cbuffer XuiPixelData { float4 BrushColor; float4 ColorFactor; }; // PS c0, c1
Texture2D<float4> BrushTexture;
SamplerState BrushSampler;
struct XuiVertex { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
XuiVertex VS_XuiTexture(float2 position:POSITION0) {
  XuiVertex output;
  float3 p=float3(position,1);
  float3 transformed=float3(dot(TransformRows[0].xyw,p)+ArithmeticBias.x,
                            dot(TransformRows[1].xyw,p)+ArithmeticBias.x,
                            dot(TransformRows[3].xyw,p));
  output.position=float4(dot(transformed,ProjectionRows[0].xyw),
                         dot(transformed,ProjectionRows[1].xyw),
                         dot(transformed,ProjectionRows[2].xyw),
                         dot(transformed,ProjectionRows[3].xyw));
  output.position+=Params.x*float4(ProjectionRows[0].z,ProjectionRows[1].z,
                                  ProjectionRows[2].z,ProjectionRows[3].z);
  output.uv=float2(dot(TextureRows[0].xyw,p),dot(TextureRows[1].xyw,p))+TextureOffset.xy;
  return output;
}
float4 PS_XuiTexture(XuiVertex input):SV_TARGET {
  return BrushTexture.Sample(BrushSampler,input.uv)*ColorFactor;
}
float4 PS_XuiSolid(XuiVertex input):SV_TARGET { return BrushColor*ColorFactor; }
float4 PS_XuiAlphaMask(XuiVertex input):SV_TARGET {
  return float4(ColorFactor.rgb,BrushTexture.Sample(BrushSampler,input.uv).a*ColorFactor.a);
}
)";
  return effect;
}
}
