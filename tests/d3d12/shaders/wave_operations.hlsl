RWByteAddressBuffer output : register(u0);

[numthreads(32, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  const uint lane = WaveGetLaneIndex();
  const uint lane_count = WaveGetLaneCount();
  const uint active_count = WaveActiveSum(1u);
  const uint first_dispatch_id = WaveReadLaneFirst(dispatch_id.x);
  output.Store4(dispatch_id.x * 16,
                uint4(lane, lane_count, active_count, first_dispatch_id));
}
