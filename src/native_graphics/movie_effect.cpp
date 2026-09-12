#include "movie_effect.h"
namespace edf::native {
Effect MakeNativeMovieEffect() {
  Effect effect;
  effect.entries={{false,"VS_Movie","vs_5_0"},{true,"PS_Movie","ps_5_0"},
                  {true,"PS_MovieSD","ps_5_0"}};
  effect.source=R"(
cbuffer MovieVertexData {
  float4 TransformRows[4];
  float4 ProjectionRows[4];
  float4 Params;
  float4 ShadowOffset;
};
cbuffer MoviePixelData { float4 ColorFactor; };
Texture2D<float> YPlane;
Texture2D<float> UPlane;
Texture2D<float> VPlane;
SamplerState YSampler;
SamplerState USampler;
SamplerState VSampler;
struct MovieVertex {
  float4 position:SV_POSITION;
  float2 uv:TEXCOORD0;
  float2 shadow_uv:TEXCOORD1;
};
MovieVertex VS_Movie(float2 position:POSITION0,float2 uv:TEXCOORD0) {
  MovieVertex output;
  float3 p=float3(position,1);
  float3 transformed=float3(dot(TransformRows[0].xyw,p),
                            dot(TransformRows[1].xyw,p),
                            dot(TransformRows[3].xyw,p));
  output.position=float4(dot(transformed,ProjectionRows[0].xyw),
                         dot(transformed,ProjectionRows[1].xyw),
                         dot(transformed,ProjectionRows[2].xyw),
                         dot(transformed,ProjectionRows[3].xyw));
  output.position+=Params.x*float4(ProjectionRows[0].z,ProjectionRows[1].z,
                                  ProjectionRows[2].z,ProjectionRows[3].z);
  output.uv=uv;
  output.shadow_uv=uv-ShadowOffset.xy;
  return output;
}
float4 ConvertMovie(MovieVertex input,uint4 coefficients) {
  float y=YPlane.Sample(YSampler,input.uv)-0.0625;
  float u=UPlane.Sample(USampler,input.uv)-0.5;
  float v=VPlane.Sample(VSampler,input.uv)-0.5;
  float luminance=y*asfloat(0x3f950a7f);
  float3 rgb=float3(v*asfloat(coefficients.x)+luminance,
                   dot(float3(v,u,luminance),float3(asfloat(coefficients.z),asfloat(coefficients.w),1)),
                   u*asfloat(coefficients.y)+luminance);
  return float4(rgb*ColorFactor.rgb,ColorFactor.a);
}
float4 PS_Movie(MovieVertex input):SV_TARGET {
  return ConvertMovie(input,uint4(0x3fe575a2,0x400731db,0xbf0872f2,0xbe5a5b23));
}
float4 PS_MovieSD(MovieVertex input):SV_TARGET {
  return ConvertMovie(input,uint4(0x3fcc4aa0,0x40010a0c,0xbf503a5e,0xbec960c5));
})";
  return effect;
}
}
