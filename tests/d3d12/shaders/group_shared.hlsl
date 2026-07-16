RWStructuredBuffer<uint> data : register(u0);

cbuffer Parameters : register(b0) {
  uint inputStride;
  uint resultBase;
};

static const uint kThreadCount = 32;
groupshared uint values[kThreadCount];

[numthreads(32, 1, 1)]
void main(uint lane : SV_GroupIndex, uint3 group : SV_GroupID) {
  values[lane] = data[group.x * inputStride + lane];
  GroupMemoryBarrierWithGroupSync();

  if (lane == 0) {
    uint sum = 0;
    [loop]
    for (uint i = 0; i < kThreadCount; ++i)
      sum += values[(i * 5) & (kThreadCount - 1)];
    data[resultBase + group.x] = sum;
  }
}
