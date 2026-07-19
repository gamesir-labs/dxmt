RWByteAddressBuffer output : register(u0);

[numthreads(32, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  const uint lane = WaveGetLaneIndex();
  const uint lane_count = WaveGetLaneCount();
  const uint active_count = WaveActiveSum(1u);
  const uint first_dispatch_id = WaveReadLaneFirst(dispatch_id.x);
  const uint last_active_lane = active_count - 1u;

  const uint sum = WaveActiveSum(dispatch_id.x + 1u);
  const uint base = dispatch_id.x * 5u * 4u;
  output.Store4(base + 0u,
                uint4(lane, lane_count, active_count, first_dispatch_id));
  output.Store(base + 16u, sum);
}
