#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct ShaderSemanticCase {
  const char *name;
  const char *body;
  std::array<UINT, 4> inputs;
  UINT expected;
};

class ShaderSemanticMatrixSpec
    : public ::testing::TestWithParam<ShaderSemanticCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(ShaderSemanticMatrixSpec, ExecutesThroughAirConversion) {
  const auto &test = GetParam();
  const std::string source = std::string(R"(
    ByteAddressBuffer input : register(t0);
    RWByteAddressBuffer output : register(u0);

    uint evaluate(uint a, uint b, uint c, uint d) {
  )") + test.body + R"(
    }

    [numthreads(1, 1, 1)]
    void main() {
      output.Store(0, evaluate(input.Load(0), input.Load(4),
                               input.Load(8), input.Load(12)));
    }
  )";
  const auto shader = CompileShader(source.c_str(), "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();

  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 1;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].OffsetInDescriptorsFromTableStart = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 2;
  parameter.DescriptorTable.pDescriptorRanges = ranges;
  const D3D12_ROOT_SIGNATURE_DESC root_desc = {1, &parameter, 0, nullptr,
                                               D3D12_ROOT_SIGNATURE_FLAG_NONE};
  auto root_signature = context_.CreateRootSignature(root_desc);
  ASSERT_TRUE(root_signature);
  const D3D12_SHADER_BYTECODE bytecode = {shader.bytecode->GetBufferPointer(),
                                          shader.bytecode->GetBufferSize()};
  auto pipeline =
      context_.CreateComputePipeline(root_signature.get(), bytecode);
  ASSERT_TRUE(pipeline);

  auto upload = context_.CreateUploadBuffer(
      sizeof(test.inputs), test.inputs.data(), sizeof(test.inputs));
  auto input = context_.CreateBuffer(
      sizeof(test.inputs), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = context_.CreateBuffer(
      sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  context_.list()->CopyBufferRegion(input.get(), 0, upload.get(), 0,
                                    sizeof(test.inputs));
  D3D12TestContext::Transition(
      context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.NumElements = sizeof(test.inputs) / sizeof(UINT);
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = 1;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateShaderResourceView(
      input.get(), &srv, context_.CpuDescriptorHandle(heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(heap.get(), 1));

  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature.get());
  context_.list()->SetPipelineState(pipeline.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(1, 1, 1);
  D3D12TestContext::Transition(
      context_.list(), output.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), sizeof(UINT), &bytes), S_OK);
  ASSERT_EQ(bytes.size(), sizeof(UINT));
  UINT actual = 0;
  std::memcpy(&actual, bytes.data(), sizeof(actual));
  EXPECT_EQ(actual, test.expected);
}

std::string ShaderSemanticCaseName(
    const ::testing::TestParamInfo<ShaderSemanticCase> &info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    ArithmeticConversionAndControlFlow, ShaderSemanticMatrixSpec,
    ::testing::Values(
        ShaderSemanticCase{"IntegerAdd", "return a + b;", {0x1234, 0x4321},
                           0x5555},
        ShaderSemanticCase{"IntegerSub", "return a - b;", {100, 37}, 63},
        ShaderSemanticCase{"IntegerMul", "return a * b;", {17, 19}, 323},
        ShaderSemanticCase{"UnsignedAddWraps", "return a + b;", {UINT_MAX, 1},
                           0},
        ShaderSemanticCase{"UnsignedMultiplyWraps", "return a * b;",
                           {0x10000, 0x10000}, 0},
        ShaderSemanticCase{"UnsignedDiv", "return a / b;", {100, 7}, 14},
        ShaderSemanticCase{"SignedDiv", "return (uint)((int)a / (int)b);",
                           {UINT(-100), 7}, UINT(-14)},
        ShaderSemanticCase{"UnsignedMin", "return min(a, b);", {5, 9}, 5},
        ShaderSemanticCase{"SignedMax",
                           "return (uint)max((int)a, (int)b);",
                           {UINT(-5), UINT(-9)}, UINT(-5)},
        ShaderSemanticCase{"MultiplyAdd", "return mad(a, b, c);", {3, 7, 5},
                           26},
        ShaderSemanticCase{"BitwiseAnd", "return a & b;",
                           {0xff00ff00, 0x0f0f0f0f}, 0x0f000f00},
        ShaderSemanticCase{"BitwiseOr", "return a | b;", {0xff00, 0x00ff},
                           0xffff},
        ShaderSemanticCase{"BitwiseXor", "return a ^ b;", {0xaaaa, 0x0f0f},
                           0xa5a5},
        ShaderSemanticCase{"BitwiseNot", "return ~a;", {0x0f0f0f0f},
                           0xf0f0f0f0},
        ShaderSemanticCase{"ShiftLeft", "return a << b;", {3, 5}, 96},
        ShaderSemanticCase{"ShiftLeftZero", "return a << b;",
                           {0x89abcdef, 0}, 0x89abcdef},
        ShaderSemanticCase{"ShiftLeftHighBit", "return a << b;", {1, 31},
                           0x80000000},
        ShaderSemanticCase{"LogicalShiftRight", "return a >> b;",
                           {0x80000000, 4}, 0x08000000},
        ShaderSemanticCase{"ArithmeticShiftRight",
                           "return (uint)((int)a >> b);", {0x80000000, 4},
                           0xf8000000},
        ShaderSemanticCase{"ArithmeticShiftRightSignFill",
                           "return (uint)((int)a >> b);", {0x80000000, 31},
                           UINT_MAX},
        ShaderSemanticCase{"CountBits", "return countbits(a);", {0xf0f0}, 8},
        ShaderSemanticCase{"FirstBitLow", "return firstbitlow(a);", {0x50},
                           4},
        ShaderSemanticCase{"FirstBitHigh", "return firstbithigh(a);", {0x50},
                           6},
        ShaderSemanticCase{"IntegerComparison",
                           "return (a > b && c <= d) ? 1 : 0;", {7, 3, 2, 2},
                           1},
        ShaderSemanticCase{"FloatAdd",
                           "return asuint(asfloat(a) + asfloat(b));",
                           {0x3fc00000, 0x40100000}, 0x40700000},
        ShaderSemanticCase{"FloatMultiply",
                           "return asuint(asfloat(a) * asfloat(b));",
                           {0x3fc00000, 0xc0000000}, 0xc0400000},
        ShaderSemanticCase{"FloatMultiplyPreservesNegativeZero",
                           "return asuint(asfloat(a) * asfloat(b));",
                           {0x80000000, 0x3f800000}, 0x80000000},
        ShaderSemanticCase{"FloatOverflowProducesInfinity",
                           "return asuint(asfloat(a) * asfloat(b));",
                           {0x7f7fffff, 0x40000000}, 0x7f800000},
        ShaderSemanticCase{
            "FloatNanClassification",
            "float value = asfloat(a); return value != value ? 1 : 0;",
            {0x7fc12345}, 1},
        ShaderSemanticCase{"FloatInfinityPropagatesThroughAdd",
                           "return asuint(asfloat(a) + asfloat(b));",
                           {0xff800000, 0x3f800000}, 0xff800000},
        ShaderSemanticCase{
            "FloatDot4",
            "return asuint(dot(float4(asfloat(a), 2, 3, 4), "
            "float4(2, 3, 4, 5)));",
            {0x3f800000}, 0x42200000},
        ShaderSemanticCase{"FloatSqrt", "return asuint(sqrt(asfloat(a)));",
                           {0x41800000}, 0x40800000},
        ShaderSemanticCase{"FloatSaturate",
                           "return asuint(saturate(asfloat(a)));",
                           {0x3fc00000}, 0x3f800000},
        ShaderSemanticCase{"SignedToFloat",
                           "return asuint((float)(int)a);", {UINT(-7)},
                           0xc0e00000},
        ShaderSemanticCase{"UnsignedToFloat", "return asuint((float)a);", {7},
                           0x40e00000},
        ShaderSemanticCase{"FloatToSigned",
                           "return (uint)(int)asfloat(a);", {0xc0700000},
                           UINT(-3)},
        ShaderSemanticCase{"FloatToUnsigned",
                           "return (uint)asfloat(a);", {0x40700000}, 3},
        ShaderSemanticCase{"Float16RoundTrip",
                           "return asuint(f16tof32(f32tof16(asfloat(a))));",
                           {0x3fc00000}, 0x3fc00000},
        ShaderSemanticCase{"Float16SmallestSubnormal",
                           "return asuint(f16tof32(a));", {1}, 0x33800000},
        ShaderSemanticCase{"Float16MaximumFinite",
                           "return f32tof16(asfloat(a));", {0x477fe000},
                           0x7bff},
        ShaderSemanticCase{"FloatRoundTiesToEven",
                           "return (uint)round(asfloat(a));", {0x40200000}, 2},
        ShaderSemanticCase{"FloatFloorNegative",
                           "return (uint)(int)floor(asfloat(a));",
                           {0xc0600000}, UINT(-4)},
        ShaderSemanticCase{"FloatCeilNegative",
                           "return (uint)(int)ceil(asfloat(a));", {0xc0600000},
                           UINT(-3)},
        ShaderSemanticCase{"BitcastRoundTrip",
                           "return asuint(asfloat(a));", {0x3f400000},
                           0x3f400000},
        ShaderSemanticCase{"IfTrue", "if (a) return b; return c;", {1, 11, 13},
                           11},
        ShaderSemanticCase{"IfFalse", "if (a) return b; return c;", {0, 11, 13},
                           13},
        ShaderSemanticCase{
            "NestedIf",
            "if (a) { if (b) return c; return d; } return 0;",
            {1, 1, 17, 19}, 17},
        ShaderSemanticCase{
            "SwitchCase",
            "switch (a) { case 1: return b; case 2: return c; default: return d; }",
            {2, 17, 19, 23}, 19},
        ShaderSemanticCase{
            "SwitchDefault",
            "switch (a) { case 1: return b; case 2: return c; default: return d; }",
            {7, 17, 19, 23}, 23},
        ShaderSemanticCase{"LoopZeroIterations",
                           "uint total = b; for (uint i = 0; i < a; ++i) total += i; return total;",
                           {0, 29}, 29},
        ShaderSemanticCase{"LoopOneIteration",
                           "uint total = b; for (uint i = 0; i < a; ++i) total += c; return total;",
                           {1, 29, 2}, 31},
        ShaderSemanticCase{"LoopManyIterations",
                           "uint total = 0; for (uint i = 0; i < a; ++i) total += i; return total;",
                           {5}, 10},
        ShaderSemanticCase{"LoopBreak",
                           "uint total = 0; for (uint i = 0; i < a; ++i) { if (i == b) break; total += i; } return total;",
                           {8, 3}, 3},
        ShaderSemanticCase{"LoopContinue",
                           "uint total = 0; for (uint i = 0; i < a; ++i) { if ((i & 1) == 0) continue; total += i; } return total;",
                           {6}, 9},
        ShaderSemanticCase{"EarlyReturn", "if (a == b) return c; return d;",
                           {5, 5, 37, 41}, 37},
        ShaderSemanticCase{"PhiTwoPredecessors",
                           "uint value; if (a < b) value = c; else value = d; return value;",
                           {2, 3, 43, 47}, 43}),
    ShaderSemanticCaseName);

} // namespace
