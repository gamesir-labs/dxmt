RWByteAddressBuffer output : register(u0);

[numthreads(32, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
  const uint lane = WaveGetLaneIndex();
  const uint lane_count = WaveGetLaneCount();
  const uint active_count = WaveActiveSum(1u);
  const uint first_dispatch_id = WaveReadLaneFirst(dispatch_id.x);
  const uint last_active_lane = active_count - 1u;

  const uint sum = WaveActiveSum(dispatch_id.x + 1u);
  const uint minimum = WaveActiveMin(dispatch_id.x + 7u);
  const uint maximum = WaveActiveMax(dispatch_id.x + 7u);
  const uint prefix_count = WavePrefixSum(1u);
  const uint product = WaveActiveProduct(1u);
  const uint prefix_product = WavePrefixProduct(1u);
  const uint last_dispatch_id =
      WaveReadLaneAt(dispatch_id.x, last_active_lane);
  const uint all_in_range = WaveActiveAllTrue(dispatch_id.x < 32u);
  const uint any_last =
      WaveActiveAnyTrue(dispatch_id.x == first_dispatch_id + active_count - 1u);
  const uint4 even_ballot =
      WaveActiveBallot((dispatch_id.x & 1u) == 0u);
  const uint even_count = countbits(even_ballot.x) +
                          countbits(even_ballot.y) +
                          countbits(even_ballot.z) +
                          countbits(even_ballot.w);
  const uint first_lane = WaveIsFirstLane() ? 1u : 0u;

  const uint base = dispatch_id.x * 16u * 4u;
  output.Store4(base + 0u,
                uint4(lane, lane_count, active_count, first_dispatch_id));
  output.Store4(base + 16u, uint4(sum, minimum, maximum, prefix_count));
  output.Store4(base + 32u,
                uint4(last_dispatch_id, all_in_range, any_last, even_count));
  output.Store2(base + 48u, uint2(first_lane, WaveActiveSum(first_lane)));
  output.Store2(base + 56u, uint2(product, prefix_product));
}
