#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Plan §16.8: CopyDescriptorsSimple count/offset matrix for CBV_SRV_UAV.
// Public D3D12 API only.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct CopyDescCase {
  UINT heap_count;
  UINT src_start;
  UINT dst_start;
  UINT copy_count;
};

std::vector<CopyDescCase> BuildCopyDescCases() {
  std::vector<CopyDescCase> cases;
  // Keep PR-friendly density: power-of-two heaps, endpoint/identity/shift copies.
  const UINT heap_counts[] = {2, 4, 8, 16, 32, 64};
  for (const UINT heap_count : heap_counts) {
    const UINT starts[] = {0, 1, heap_count / 2,
                           heap_count > 1 ? heap_count - 1 : 0};
    const UINT counts[] = {1, 2, heap_count / 2, heap_count};
    for (const UINT src : starts) {
      for (const UINT dst : starts) {
        for (const UINT n : counts) {
          if (n == 0 || src + n > heap_count || dst + n > heap_count)
            continue;
          cases.push_back({heap_count, src, dst, n});
        }
      }
    }
  }
  // Deduplicate identical tuples from repeated start/count samples.
  std::sort(cases.begin(), cases.end(),
            [](const CopyDescCase &a, const CopyDescCase &b) {
              if (a.heap_count != b.heap_count)
                return a.heap_count < b.heap_count;
              if (a.src_start != b.src_start)
                return a.src_start < b.src_start;
              if (a.dst_start != b.dst_start)
                return a.dst_start < b.dst_start;
              return a.copy_count < b.copy_count;
            });
  cases.erase(std::unique(cases.begin(), cases.end(),
                          [](const CopyDescCase &a, const CopyDescCase &b) {
                            return a.heap_count == b.heap_count &&
                                   a.src_start == b.src_start &&
                                   a.dst_start == b.dst_start &&
                                   a.copy_count == b.copy_count;
                          }),
              cases.end());
  return cases;
}

class CopyDescriptorMatrixSpec
    : public ::testing::TestWithParam<CopyDescCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    shader_ = CompileShader(R"(
      ByteAddressBuffer input : register(t0);
      RWByteAddressBuffer output : register(u0);
      [numthreads(1,1,1)]
      void main(uint3 id : SV_DispatchThreadID) {
        uint byte_offset = id.x * 4u;
        output.Store(byte_offset, input.Load(byte_offset) + 1u);
      }
    )",
                            "cs_5_0");
    ASSERT_EQ(shader_.result, S_OK) << shader_.diagnostic_text();

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 2;
    param.DescriptorTable.pDescriptorRanges = ranges;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &param;
    root_signature_ = context_.CreateRootSignature(desc);
    ASSERT_TRUE(root_signature_);
    const D3D12_SHADER_BYTECODE bytecode = {
        shader_.bytecode->GetBufferPointer(),
        shader_.bytecode->GetBufferSize()};
    pipeline_ = context_.CreateComputePipeline(root_signature_.get(), bytecode);
    ASSERT_TRUE(pipeline_);
  }

  D3D12TestContext context_;
  dxmt::test::ShaderCompilation shader_;
  ComPtr<ID3D12RootSignature> root_signature_;
  ComPtr<ID3D12PipelineState> pipeline_;
};

TEST_P(CopyDescriptorMatrixSpec, CopiedTableExecutesComputeIncrement) {
  const auto &test = GetParam();
  // Need at least 2 descriptors in the table (SRV+UAV). Use heap large enough.
  if (test.heap_count < 2)
    GTEST_SKIP() << "need >=2 descriptors for SRV+UAV table";
  if (test.copy_count < 2 || test.src_start + 2 > test.heap_count ||
      test.dst_start + 2 > test.heap_count)
    GTEST_SKIP() << "need contiguous SRV+UAV pair in copy window";

  // Source pair at src_start, dest pair at dst_start after copy of full window
  // containing the pair; bind dest_start as table start.
  // Simpler: create source heap with SRV at 0 UAV at 1, copy `copy_count`
  // descriptors starting at src to dst, then bind dst_start only when the pair
  // lands at dst_start..dst_start+1.
  if (!(test.src_start == 0 && test.copy_count >= 2 &&
        test.dst_start + 1 < test.heap_count))
    GTEST_SKIP() << "execution oracle requires src pair at 0 and copy>=2";

  constexpr UINT kCount = 8;
  std::vector<UINT> pattern(kCount);
  for (UINT i = 0; i < kCount; ++i)
    pattern[i] = 100u + i;

  auto input = context_.CreateBuffer(
      kCount * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COPY_DEST);
  auto output = context_.CreateBuffer(
      kCount * sizeof(UINT), D3D12_HEAP_TYPE_DEFAULT,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  auto upload = context_.CreateUploadBuffer(kCount * sizeof(UINT),
                                            pattern.data(),
                                            pattern.size() * sizeof(UINT));
  ASSERT_TRUE(input);
  ASSERT_TRUE(output);
  ASSERT_TRUE(upload);

  auto src_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, test.heap_count, false);
  auto dst_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, test.heap_count, true);
  ASSERT_TRUE(src_heap);
  ASSERT_TRUE(dst_heap);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = 0;
  srv.Buffer.NumElements = kCount;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
  uav.Format = DXGI_FORMAT_R32_TYPELESS;
  uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav.Buffer.FirstElement = 0;
  uav.Buffer.NumElements = kCount;
  uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

  context_.device()->CreateShaderResourceView(
      input.get(), &srv, context_.CpuDescriptorHandle(src_heap.get(), 0));
  context_.device()->CreateUnorderedAccessView(
      output.get(), nullptr, &uav,
      context_.CpuDescriptorHandle(src_heap.get(), 1));

  context_.device()->CopyDescriptorsSimple(
      test.copy_count, context_.CpuDescriptorHandle(dst_heap.get(), test.dst_start),
      context_.CpuDescriptorHandle(src_heap.get(), test.src_start),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  context_.list()->CopyBufferRegion(input.get(), 0, upload.get(), 0,
                                    kCount * sizeof(UINT));
  D3D12TestContext::Transition(
      context_.list(), input.get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  ID3D12DescriptorHeap *heaps[] = {dst_heap.get()};
  context_.list()->SetDescriptorHeaps(1, heaps);
  context_.list()->SetComputeRootSignature(root_signature_.get());
  context_.list()->SetPipelineState(pipeline_.get());
  context_.list()->SetComputeRootDescriptorTable(
      0, context_.GpuDescriptorHandle(dst_heap.get(), test.dst_start));
  context_.list()->Dispatch(kCount, 1, 1);
  D3D12TestContext::Transition(context_.list(), output.get(),
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               D3D12_RESOURCE_STATE_COPY_SOURCE);

  std::vector<std::uint8_t> actual;
  ASSERT_EQ(context_.ReadbackBuffer(output.get(), kCount * sizeof(UINT),
                                    &actual),
            S_OK);
  for (UINT i = 0; i < kCount; ++i) {
    UINT value = 0;
    std::memcpy(&value, actual.data() + i * sizeof(UINT), sizeof(value));
    EXPECT_EQ(value, pattern[i] + 1u) << "i=" << i << " heap=" << test.heap_count
                                      << " dst=" << test.dst_start;
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_P(CopyDescriptorMatrixSpec, CopyDoesNotRemoveDevice) {
  const auto &test = GetParam();
  auto src = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, test.heap_count, false);
  auto dst = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, test.heap_count, false);
  ASSERT_TRUE(src);
  ASSERT_TRUE(dst);
  // Create a trivial SRV so copy has defined content.
  auto buffer = context_.CreateBuffer(64, D3D12_HEAP_TYPE_DEFAULT,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(buffer);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
  srv.Format = DXGI_FORMAT_R32_TYPELESS;
  srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Buffer.FirstElement = 0;
  srv.Buffer.NumElements = 16;
  srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
  for (UINT i = 0; i < test.heap_count; ++i)
    context_.device()->CreateShaderResourceView(
        buffer.get(), &srv, context_.CpuDescriptorHandle(src.get(), i));

  context_.device()->CopyDescriptorsSimple(
      test.copy_count, context_.CpuDescriptorHandle(dst.get(), test.dst_start),
      context_.CpuDescriptorHandle(src.get(), test.src_start),
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::string CopyDescName(const ::testing::TestParamInfo<CopyDescCase> &info) {
  return "H" + std::to_string(info.param.heap_count) + "S" +
         std::to_string(info.param.src_start) + "D" +
         std::to_string(info.param.dst_start) + "N" +
         std::to_string(info.param.copy_count) + "I" +
         std::to_string(info.index);
}

INSTANTIATE_TEST_SUITE_P(CopyMatrix, CopyDescriptorMatrixSpec,
                         ::testing::ValuesIn(BuildCopyDescCases()),
                         CopyDescName);

} // namespace
