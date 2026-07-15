struct VsInput {
  float4 position : POSITION;
};

struct VsOutput {
  float4 position : SV_Position;
  float scalar : TEXCOORD1;
};

VsOutput main(VsInput input) {
  VsOutput output;
  output.position = float4(input.position.xyz, 1.0);
  output.scalar = input.position.w;
  return output;
}
