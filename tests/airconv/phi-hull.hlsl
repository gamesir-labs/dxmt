struct ControlPoint {
  float4 position : POSITION;
};

struct PatchConstants {
  float edge[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};

PatchConstants patch_constants(InputPatch<ControlPoint, 3> patch) {
  PatchConstants result;
  result.edge[0] = 2.0f;
  result.edge[1] = 2.0f;
  result.edge[2] = 2.0f;
  result.inside = 2.0f;
  return result;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch_constants")]
ControlPoint main(InputPatch<ControlPoint, 3> patch,
                  uint control_point_id : SV_OutputControlPointID) {
  return patch[control_point_id];
}
