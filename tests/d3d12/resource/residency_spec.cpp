#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class ResidencySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12Heap> CreateBufferHeap() {
    D3D12_HEAP_DESC desc = {};
    desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    desc.Properties.CreationNodeMask = 1;
    desc.Properties.VisibleNodeMask = 1;
    desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ComPtr<ID3D12Heap> heap;
    EXPECT_EQ(context_.device()->CreateHeap(
                  &desc, __uuidof(ID3D12Heap),
                  reinterpret_cast<void **>(heap.put())),
              S_OK);
    return heap;
  }

  ComPtr<ID3D12QueryHeap> CreateQueryHeap() {
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 2;
    ComPtr<ID3D12QueryHeap> heap;
    EXPECT_EQ(context_.device()->CreateQueryHeap(
                  &desc, __uuidof(ID3D12QueryHeap),
                  reinterpret_cast<void **>(heap.put())),
              S_OK);
    return heap;
  }

  D3D12TestContext context_;
};

TEST_F(ResidencySpec, MakesAndEvictsEverySupportedObjectKind) {
  auto resource = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto descriptor_heap = context_.CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true);
  auto heap = CreateBufferHeap();
  auto query_heap = CreateQueryHeap();
  ASSERT_TRUE(resource);
  ASSERT_TRUE(descriptor_heap);
  ASSERT_TRUE(heap);
  ASSERT_TRUE(query_heap);
  std::array<ID3D12Pageable *, 4> objects = {
      resource.get(), descriptor_heap.get(), heap.get(), query_heap.get()};

  EXPECT_EQ(context_.device()->MakeResident(
                static_cast<UINT>(objects.size()), objects.data()),
            S_OK);
  EXPECT_EQ(context_.device()->Evict(static_cast<UINT>(objects.size()),
                                     objects.data()),
            S_OK);
  EXPECT_EQ(context_.device()->MakeResident(
                static_cast<UINT>(objects.size()), objects.data()),
            S_OK);
}

TEST_F(ResidencySpec, EnqueueMakeResidentSignalsFenceValuesInOrder) {
  ComPtr<ID3D12Device3> device3;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D12Device3),
                reinterpret_cast<void **>(device3.put())),
            S_OK);
  auto first = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto second = context_.CreateBuffer(
      8192, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ComPtr<ID3D12Fence> fence;
  ASSERT_EQ(context_.device()->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                reinterpret_cast<void **>(fence.put())),
            S_OK);

  ID3D12Pageable *first_object[] = {first.get()};
  std::array<ID3D12Pageable *, 2> both = {first.get(), second.get()};
  ASSERT_EQ(device3->EnqueueMakeResident(D3D12_RESIDENCY_FLAG_NONE, 1,
                                         first_object, fence.get(), 3),
            S_OK);
  ASSERT_EQ(device3->EnqueueMakeResident(
                D3D12_RESIDENCY_FLAG_NONE, static_cast<UINT>(both.size()),
                both.data(), fence.get(), 7),
            S_OK);
  ASSERT_EQ(context_.WaitForFence(fence.get(), 7), S_OK);
  EXPECT_GE(fence->GetCompletedValue(), 7u);
}

TEST_F(ResidencySpec, AcceptsPriorityBucketsAndApplicationValues) {
  ComPtr<ID3D12Device1> device1;
  ASSERT_EQ(context_.device()->QueryInterface(
                __uuidof(ID3D12Device1),
                reinterpret_cast<void **>(device1.put())),
            S_OK);
  auto resource = context_.CreateBuffer(
      4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_COMMON);
  auto heap = CreateBufferHeap();
  ASSERT_TRUE(resource);
  ASSERT_TRUE(heap);
  std::array<ID3D12Pageable *, 2> objects = {resource.get(), heap.get()};
  const std::array<D3D12_RESIDENCY_PRIORITY, 2> priorities = {
      D3D12_RESIDENCY_PRIORITY_LOW,
      static_cast<D3D12_RESIDENCY_PRIORITY>(
          D3D12_RESIDENCY_PRIORITY_NORMAL + 1),
  };
  EXPECT_EQ(device1->SetResidencyPriority(
                static_cast<UINT>(objects.size()), objects.data(),
                priorities.data()),
            S_OK);
}

} // namespace
