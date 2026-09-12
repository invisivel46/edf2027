#include "font_effect.h"
namespace edf::native {
Effect MakeNativeFontEffect() {
  // Source at820179A8, referenced by pointer82556148 in821AD3B8.
  // Preserve the source's alpha=2 for value>0.5 despite its misleading comment.
  Effect effect;
  effect.entries={{false,"FontVertexShader","vs_5_0"},{true,"FontPixelShader","ps_5_0"}};
  effect.source=R"(
cbuffer FontVertexData {
  float4 VertexColor;
  float2 TexScale;
  float2 Offset;
  float2 Scale;
};
cbuffer FontPixelData { float4 ChannelSelector; float4 Mask; };
Texture2D<float4> FontTexture;
SamplerState FontSampler;
struct FontVertex { float4 position:SV_POSITION; float4 diffuse:COLOR0; float2 uv:TEXCOORD0; };
FontVertex FontVertexShader(float2 position:POSITION0,float2 uv:TEXCOORD0) {
  FontVertex result;
  result.position=float4(position*Scale+Offset,0,1);
  result.diffuse=VertexColor;
  result.uv=uv*TexScale;
  return result;
}
float4 FontPixelShader(FontVertex input):SV_TARGET {
  float4 texel=FontTexture.Sample(FontSampler,input.uv);
  float value=dot(texel,ChannelSelector);
  float rgb=value>0.5 ? 2*value-1 : 0;
  float4 color=float4(rgb,rgb,rgb,2*(value>0.5 ? 1 : value));
  return lerp(color,texel,Mask)*input.diffuse;
}
)";
  return effect;
}
}
