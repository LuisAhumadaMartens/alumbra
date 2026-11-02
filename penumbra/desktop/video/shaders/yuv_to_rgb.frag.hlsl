Texture2D<float4> luma_plane : register(t0, space2);
Texture2D<float4> first_chroma_plane : register(t1, space2);
Texture2D<float4> second_chroma_plane : register(t2, space2);
SamplerState luma_sampler : register(s0, space2);
SamplerState first_chroma_sampler : register(s1, space2);
SamplerState second_chroma_sampler : register(s2, space2);

cbuffer ConversionParams : register(b0, space3) {
  float4 scale;
  float4 bias;
  float4 matrix_row_red;
  float4 matrix_row_green;
  float4 matrix_row_blue;
  float4 chroma_transform;
  float4 layout_flags;
};

struct PixelInput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target0 {
  float2 chroma_uv = input.uv * chroma_transform.xy + chroma_transform.zw;
  float luma = luma_plane.Sample(luma_sampler, input.uv).r;
  float4 first = first_chroma_plane.Sample(first_chroma_sampler, chroma_uv);
  float4 second = second_chroma_plane.Sample(second_chroma_sampler, chroma_uv);
  float blue_difference = first.r;
  float red_difference = lerp(second.r, first.g, layout_flags.x);
  float3 yuv = float3(luma, blue_difference, red_difference) * scale.xyz + bias.xyz;
  float3 rgb = float3(dot(matrix_row_red.xyz, yuv), dot(matrix_row_green.xyz, yuv),
                      dot(matrix_row_blue.xyz, yuv));
  return float4(saturate(rgb), 1.0);
}
