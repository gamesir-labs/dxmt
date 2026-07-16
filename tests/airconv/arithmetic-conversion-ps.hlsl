cbuffer Parameters : register(b0) {
  float float_value;
  uint uint_value;
  int int_value;
  uint shift_value;
};

uint4 main(float4 position : SV_Position) : SV_Target0 {
  const uint shift = shift_value & 31u;
  const uint shifted = (uint_value << shift) | (uint_value >> shift);
  const int signed_shifted = int_value >> shift;
  const uint float_to_uint = (uint)max(float_value, 0.0f);
  const int float_to_int = (int)float_value;
  const float uint_to_float = (float)uint_value;
  const float int_to_float = (float)int_value;
  const float rounded_half = f16tof32(f32tof16(float_value));

  const uint mixed_conversion = asuint(uint_to_float) ^ asuint(int_to_float) ^
                                asuint(rounded_half) ^ countbits(uint_value);
  return uint4(shifted, asuint(signed_shifted),
               float_to_uint ^ asuint(float_to_int), mixed_conversion);
}
