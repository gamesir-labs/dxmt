struct PixelOutput {
  float4 color : SV_Target0;
  float4 blendFactor : SV_Target1;
};

PixelOutput main() {
  PixelOutput output;
  output.color = float4(1.0, 0.0, 0.0, 1.0);
  output.blendFactor = float4(0.25, 0.25, 0.25, 0.25);
  return output;
}
