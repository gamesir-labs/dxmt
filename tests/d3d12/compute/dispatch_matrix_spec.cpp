#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan compute: Dispatch thread-group size matrix with per-group write oracle.
// Public D3D12 API only.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct DispatchCase {
  UINT x;
  UINT y;
  UINT z;
};

std::vector<DispatchCase> BuildDispatchCases() {
  std::vector<DispatchCase> cases;
  // 16 * 8 * 4 = 512 combinations; combined with other matrices exceeds 1000.
  for (UINT x = 1; x <= 16; ++x) {
    for (UINT y = 1; y <= 8; ++y) {
      for (UINT z = 1; z <= 4; ++z)
        cases.push_back({x, y, z});
    }
  }
  return cases;
}

class DispatchMatrixSpec : public ::testing::TestWithParam<DispatchCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    shader_ = CompileShader(R"(
      RWByteAddressBuffer output : register(u0);
      [numthreads(1, 1, 1)]
      void main(uint3 gid : SV_GroupID) {
        // Pack group id into a linear index slot.
        const uint index = gid.x + 32u * (gid.y + 16u * gid.z);
        const uint value = 0xA0000000u | gid.x | (gid.y << 8) | (gid.z << 16);
        output.Store(index * 4u, value);
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &param;
    root_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_);
    const D3D12_SHADER_BYTECODE bc = {shader_.bytecode->GetBufferPointer(),
                                      shader_.bytecode->GetBufferSize()};
    pipeline_ = context_.CreateComputePipeline(root_.get(), bc);
    ASSERT_TRUE(pipeline_);
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
  ComPtr<ID3D12RootSignature> root_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(DispatchMatrixSpec, EachGroupWritesPackedIdentity) {
  const auto &test = GetParam();
  // Linear capacity for x<=16,y<=8,z<=8 with packing formula above.
  constexpr UINT kSlots = 32u * 16u * 8u;
  std::vector<UINT> zeros(kSlots, 0u);
  auto upload = context_.CreateUploadBuffer(kSlots * sizeof(UINT), zeros.data(),
                                            zeros.size() * sizeof(UINT));
  auto output = context_.CreateBuffer(
      kSlots * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
  ASSERT_TRUE(upload);
  ASSERT_TRUE(output);
  ASSERT_TRUE(heap);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.NumElements = kSlots;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());

  context_.list()->CopyBufferRegion(output.get(), 0, upload.get(), 0,
                                    kSlots * sizeof(UINT));
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_COPY_DEST,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  ID3D12DescriptorHeap *heaps[] = {heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, heap->GetGPUDescriptorHandleForHeapStart());
  context_.list()->Dispatch(test.x, test.y, test.z);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> bytes;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), kSlots * sizeof(UINT), &bytes),
            S_OK);
  // Verify every group that should have run.
  for (UINT z = 0; z < test.z; ++z) {
    for (UINT y = 0; y < test.y; ++y) {
      for (UINT x = 0; x < test.x; ++x) {
        const UINT index = x + 32u * (y + 16u * z);
        UINT value = 0;
        std::memcpy(&value, bytes.data() + index * sizeof(UINT), sizeof(value));
        const UINT expected = 0xA0000000u | x | (y << 8) | (z << 16);
        EXPECT_EQ(value, expected) << "gid " << x << "," << y << "," << z;
      }
    }
  }
  // Spot-check a few slots outside the dispatch rectangle stay zero.
  const UINT outside[][3] = {{test.x, 0, 0}, {0, test.y, 0}, {0, 0, test.z},
                             {31, 15, 7}};
  for (const auto &o : outside) {
    if (o[0] < test.x && o[1] < test.y && o[2] < test.z)
      continue;
    if (o[0] >= 32 || o[1] >= 16 || o[2] >= 8)
      continue;
    const UINT index = o[0] + 32u * (o[1] + 16u * o[2]);
    UINT value = 0;
    std::memcpy(&value, bytes.data() + index * sizeof(UINT), sizeof(value));
    EXPECT_EQ(value, 0u) << "outside " << o[0] << "," << o[1] << "," << o[2];
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string DispatchName(const ::testing::TestParamInfo<DispatchCase> &info) {
  return "X" + std::to_string(info.param.x) + "Y" +
         std::to_string(info.param.y) + "Z" + std::to_string(info.param.z);
}

INSTANTIATE_TEST_SUITE_P(GroupMatrix, DispatchMatrixSpec,
                         ::testing::ValuesIn(BuildDispatchCases()),
                         DispatchName);

} // namespace
