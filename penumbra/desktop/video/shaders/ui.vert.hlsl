cbuffer ViewportParams : register(b0, space1) {
  float4 viewport;
};

struct VertexInput {
  float4 position_uv : TEXCOORD0;
  float4 color : TEXCOORD1;
  float4 shape : TEXCOORD2;
  float4 border_color : TEXCOORD3;
  float3 local_kind : TEXCOORD4;
};

struct VertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : TEXCOORD1;
  float4 shape : TEXCOORD2;
  float4 border_color : TEXCOORD3;
  float3 local_kind : TEXCOORD4;
};

VertexOutput main(VertexInput input) {
  VertexOutput output;
  float2 ndc = input.position_uv.xy / viewport.xy * float2(2.0, -2.0) + float2(-1.0, 1.0);
  output.position = float4(ndc, 0.0, 1.0);
  output.uv = input.position_uv.zw;
  output.color = input.color;
  output.shape = input.shape;
  output.border_color = input.border_color;
  output.local_kind = input.local_kind;
  return output;
}
