cbuffer Parameters : register(b0) {
  uint selector;
  uint limit;
  uint seed;
  uint padding;
};

uint main(float4 position : SV_Position) : SV_Target0 {
  uint value = seed ^ (uint)position.x;

  if ((selector & 8u) != 0u)
    value += 3u;
  else
    value += 5u;

  [loop]
  for (uint i = 0u; i < limit; ++i) {
    if (i == 2u)
      continue;
    value = value * 33u + i;
    if (value == 0xdeadbeefu)
      break;
  }

  switch (selector & 3u) {
  case 0u:
    value += 11u;
    break;
  case 1u:
    value ^= 0x55u;
    break;
  case 2u:
    value *= 7u;
    break;
  default:
    value -= 13u;
    break;
  }

  return value;
}
