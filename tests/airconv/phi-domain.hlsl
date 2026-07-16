struct ControlPoint {
  float4 position : POSITION;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

[domain("tri")]
float4 main(PatchConstants constants,
            float3 barycentrics : SV_DomainLocation,
            const OutputPatch<ControlPoint, 3> patch) : SV_Position {
  return patch[0].position * barycentrics.x +
         patch[1].position * barycentrics.y +
         patch[2].position * barycentrics.z;
}
