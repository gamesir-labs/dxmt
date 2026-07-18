#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct DescriptorHeapCase {
  D3D12_DESCRIPTOR_HEAP_TYPE type;
  D3D12_DESCRIPTOR_HEAP_FLAGS flags;
  UINT count;
  const char *name;
};

class DescriptorHeapContractSpec
    : public ::testing::TestWithParam<DescriptorHeapCase> {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.InitializeSharedDevice("descriptor-heap-contract"),
              S_OK);
  }

  D3D12TestContext context_;
};

TEST(DescriptorHeapCapabilitySpec, ValidatesWithoutCreatingObjects) {
  D3D12TestContext context;
  ASSERT_EQ(context.InitializeSharedDevice("descriptor-heap-capability"),
            S_OK);

  constexpr std::array valid_cases = {
      D3D12_DESCRIPTOR_HEAP_DESC{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1,
                                 D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0},
      D3D12_DESCRIPTOR_HEAP_DESC{
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1,
          D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 1},
      D3D12_DESCRIPTOR_HEAP_DESC{D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1,
                                 D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0},
      D3D12_DESCRIPTOR_HEAP_DESC{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1,
                                 D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0},
      D3D12_DESCRIPTOR_HEAP_DESC{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1,
                                 D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 1},
  };
  for (const auto &desc : valid_cases) {
    EXPECT_EQ(context.device()->CreateDescriptorHeap(
                  &desc, __uuidof(ID3D12DescriptorHeap), nullptr),
              S_FALSE)
        << "type " << static_cast<UINT>(desc.Type) << " flags "
        << static_cast<UINT>(desc.Flags);
  }

  EXPECT_EQ(context.device()->CreateDescriptorHeap(
                nullptr, __uuidof(ID3D12DescriptorHeap), nullptr),
            E_INVALIDARG);
  auto invalid = valid_cases[0];
  invalid.NumDescriptors = 0;
  EXPECT_EQ(context.device()->CreateDescriptorHeap(
                &invalid, __uuidof(ID3D12DescriptorHeap), nullptr),
            E_INVALIDARG);
  invalid = valid_cases[0];
  invalid.Flags = static_cast<D3D12_DESCRIPTOR_HEAP_FLAGS>(2);
  EXPECT_EQ(context.device()->CreateDescriptorHeap(
                &invalid, __uuidof(ID3D12DescriptorHeap), nullptr),
            E_INVALIDARG);
  invalid.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  invalid.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  EXPECT_EQ(context.device()->CreateDescriptorHeap(
                &invalid, __uuidof(ID3D12DescriptorHeap), nullptr),
            E_INVALIDARG);
}

TEST_P(DescriptorHeapContractSpec, PreservesDescriptionAndBoundaryHandles) {
  const auto &test = GetParam();
  const D3D12_DESCRIPTOR_HEAP_DESC desc = {test.type, test.count, test.flags,
                                           0};
  ComPtr<ID3D12DescriptorHeap> heap;
  ASSERT_EQ(context_.device()->CreateDescriptorHeap(
                &desc, IID_PPV_ARGS(heap.put())),
            S_OK);
  ASSERT_TRUE(heap);

  const auto actual = heap->GetDesc();
  EXPECT_EQ(actual.Type, desc.Type);
  EXPECT_EQ(actual.NumDescriptors, desc.NumDescriptors);
  EXPECT_EQ(actual.Flags, desc.Flags);
  EXPECT_EQ(actual.NodeMask, 0u);

  const UINT increment =
      context_.device()->GetDescriptorHandleIncrementSize(test.type);
  ASSERT_GT(increment, 0u);
  const auto cpu_first = heap->GetCPUDescriptorHandleForHeapStart();
  const auto cpu_again = heap->GetCPUDescriptorHandleForHeapStart();
  ASSERT_NE(cpu_first.ptr, 0u);
  EXPECT_EQ(cpu_again.ptr, cpu_first.ptr);
  const std::uintptr_t cpu_last =
      cpu_first.ptr + std::uintptr_t(test.count - 1) * increment;
  EXPECT_GE(cpu_last, cpu_first.ptr);

  if (test.flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
    const auto gpu_first = heap->GetGPUDescriptorHandleForHeapStart();
    const auto gpu_again = heap->GetGPUDescriptorHandleForHeapStart();
    ASSERT_NE(gpu_first.ptr, 0u);
    EXPECT_EQ(gpu_again.ptr, gpu_first.ptr);
    const UINT64 gpu_last =
        gpu_first.ptr + UINT64(test.count - 1) * increment;
    EXPECT_GE(gpu_last, gpu_first.ptr);
  }

  ComPtr<IUnknown> expected_device;
  ComPtr<IUnknown> actual_device;
  ASSERT_EQ(context_.device()->QueryInterface(IID_PPV_ARGS(expected_device.put())),
            S_OK);
  ASSERT_EQ(heap->GetDevice(IID_PPV_ARGS(actual_device.put())), S_OK);
  EXPECT_EQ(actual_device.get(), expected_device.get());
}

std::string DescriptorHeapCaseName(
    const ::testing::TestParamInfo<DescriptorHeapCase> &info) {
  return std::string(info.param.name) + "_" +
         std::to_string(info.param.count);
}

constexpr std::array<UINT, 6> kBoundaryCounts = {1, 2, 31, 32, 33, 1024};

std::vector<DescriptorHeapCase> MakeDescriptorHeapCases() {
  std::vector<DescriptorHeapCase> cases;
  for (UINT count : kBoundaryCounts) {
    cases.push_back({D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                     D3D12_DESCRIPTOR_HEAP_FLAG_NONE, count, "CbvSrvUavCpu"});
    cases.push_back({D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                     D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, count,
                     "CbvSrvUavGpu"});
    cases.push_back({D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                     D3D12_DESCRIPTOR_HEAP_FLAG_NONE, count, "SamplerCpu"});
    cases.push_back({D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                     D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, count,
                     "SamplerGpu"});
    cases.push_back({D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                     D3D12_DESCRIPTOR_HEAP_FLAG_NONE, count, "RtvCpu"});
    cases.push_back({D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                     D3D12_DESCRIPTOR_HEAP_FLAG_NONE, count, "DsvCpu"});
  }
  return cases;
}

INSTANTIATE_TEST_SUITE_P(BoundaryMatrix, DescriptorHeapContractSpec,
                         ::testing::ValuesIn(MakeDescriptorHeapCases()),
                         DescriptorHeapCaseName);

} // namespace
