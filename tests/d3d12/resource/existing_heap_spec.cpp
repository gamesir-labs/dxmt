#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

constexpr SIZE_T kAllocationSize = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

class VirtualAllocation {
public:
  VirtualAllocation()
      : address_(VirtualAlloc(nullptr, kAllocationSize,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {}

  ~VirtualAllocation() {
    if (address_)
      VirtualFree(address_, 0, MEM_RELEASE);
  }

  VirtualAllocation(const VirtualAllocation &) = delete;
  VirtualAllocation &operator=(const VirtualAllocation &) = delete;

  void *get() const { return address_; }
  explicit operator bool() const { return address_ != nullptr; }

private:
  void *address_ = nullptr;
};

class FileMapping {
public:
  FileMapping()
      : handle_(CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                   PAGE_READWRITE, 0,
                                   static_cast<DWORD>(kAllocationSize),
                                   nullptr)) {}

  ~FileMapping() {
    if (handle_)
      CloseHandle(handle_);
  }

  FileMapping(const FileMapping &) = delete;
  FileMapping &operator=(const FileMapping &) = delete;

  HANDLE get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }

private:
  HANDLE handle_ = nullptr;
};

D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

class ExistingHeapSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_EQ(context_.device()->QueryInterface(
                  __uuidof(ID3D12Device3),
                  reinterpret_cast<void **>(device3_.put())),
              S_OK);
    ASSERT_TRUE(device3_);
  }

  D3D12TestContext context_;
  ComPtr<ID3D12Device3> device3_;
};

TEST_F(ExistingHeapSpec, VirtualAllocationBacksGpuWrittenPlacedBuffer) {
  VirtualAllocation allocation;
  ASSERT_TRUE(allocation);
  ComPtr<ID3D12Heap> heap;
  ASSERT_EQ(device3_->OpenExistingHeapFromAddress(
                allocation.get(), __uuidof(ID3D12Heap),
                reinterpret_cast<void **>(heap.put())),
            S_OK);
  ASSERT_TRUE(heap);

  const auto heap_desc = heap->GetDesc();
  EXPECT_EQ(heap_desc.SizeInBytes, kAllocationSize);
  EXPECT_EQ(heap_desc.Alignment,
            D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
  EXPECT_EQ(heap_desc.Properties.Type, D3D12_HEAP_TYPE_CUSTOM);
  EXPECT_EQ(heap_desc.Properties.CPUPageProperty,
            D3D12_CPU_PAGE_PROPERTY_WRITE_BACK);
  EXPECT_EQ(heap_desc.Properties.MemoryPoolPreference, D3D12_MEMORY_POOL_L0);
  EXPECT_EQ(heap_desc.Properties.CreationNodeMask, 1u);
  EXPECT_EQ(heap_desc.Properties.VisibleNodeMask, 1u);
  constexpr D3D12_HEAP_FLAGS required_flags = static_cast<D3D12_HEAP_FLAGS>(
      D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER);
  EXPECT_EQ(heap_desc.Flags & required_flags, required_flags);

  constexpr std::array<std::uint32_t, 8> expected = {
      0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f001u,
      0x13579bdfu, 0x2468ace0u, 0xfeedc0deu, 0x0badf00du};
  auto upload = context_.CreateUploadBuffer(sizeof(expected), expected.data(),
                                             sizeof(expected));
  ASSERT_TRUE(upload);
  auto resource_desc = BufferDesc(sizeof(expected));
  resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
  ComPtr<ID3D12Resource> resource;
  ASSERT_EQ(context_.device()->CreatePlacedResource(
                heap.get(), 0, &resource_desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, __uuidof(ID3D12Resource),
                reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_TRUE(resource);

  context_.list()->CopyBufferRegion(resource.get(), 0, upload.get(), 0,
                                    sizeof(expected));
  ASSERT_EQ(context_.ExecuteAndWait(), S_OK);

  void *mapping = nullptr;
  D3D12_RANGE read_range = {0, sizeof(expected)};
  ASSERT_EQ(resource->Map(0, &read_range, &mapping), S_OK);
  ASSERT_NE(mapping, nullptr);
  EXPECT_EQ(std::memcmp(mapping, expected.data(), sizeof(expected)), 0);
  EXPECT_EQ(std::memcmp(allocation.get(), expected.data(), sizeof(expected)),
            0);
  D3D12_RANGE written_range = {0, 0};
  resource->Unmap(0, &written_range);
}


} // namespace
