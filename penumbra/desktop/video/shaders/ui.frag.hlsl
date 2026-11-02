Texture2D<float4> glyph_atlas : register(t0, space2);
Texture2D<float4> icon_atlas : register(t1, space2);
SamplerState glyph_sampler : register(s0, space2);
SamplerState icon_sampler : register(s1, space2);

struct PixelInput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : TEXCOORD1;
  float4 shape : TEXCOORD2;
  float4 border_color : TEXCOORD3;
  float3 local_kind : TEXCOORD4;
};

float RoundedBoxDistance(float2 point_from_centre, float2 half_size, float radius) {
  float2 corner = abs(point_from_centre) - half_size + radius;
  return length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - radius;
}

float4 Premultiply(float4 color, float coverage) {
  return float4(color.rgb * color.a * coverage, color.a * coverage);
}

float4 main(PixelInput input) : SV_Target0 {
  float glyph_coverage = glyph_atlas.Sample(glyph_sampler, input.uv).r;
  float icon_distance = icon_atlas.Sample(icon_sampler, input.uv).r;
  float icon_width = max(fwidth(icon_distance), 0.0001);
  float icon_coverage = saturate((icon_distance - 0.5) / icon_width + 0.5);

  float box_distance = RoundedBoxDistance(input.local_kind.xy, input.shape.xy, input.shape.z);
  float outer = saturate(0.5 - box_distance);
  float inner = outer;
  if (input.shape.w > 0.0) {
    inner = saturate(0.5 - (box_distance + input.shape.w));
  }
  float4 box = Premultiply(input.color, inner) + Premultiply(input.border_color, outer - inner);

  float kind = input.local_kind.z;
  if (kind > 1.5) {
    return Premultiply(input.color, icon_coverage);
  }
  if (kind > 0.5) {
    return Premultiply(input.color, glyph_coverage);
  }
  return box;
}
