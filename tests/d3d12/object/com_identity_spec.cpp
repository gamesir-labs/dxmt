#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

class ComIdentitySpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_TRUE(SUCCEEDED(context_.Initialize())); }

  template <typename Visitor> void ForEachObject(Visitor &&visit) {
    ComPtr<ID3D12Fence> fence;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()))));
    auto resource = context_.CreateBuffer(256, D3D12_HEAP_TYPE_DEFAULT,
                                          D3D12_RESOURCE_FLAG_NONE,
                                          D3D12_RESOURCE_STATE_COMMON);
    auto descriptor_heap = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, false);
    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    auto root_signature = context_.CreateRootSignature(root_desc);

    D3D12_HEAP_DESC heap_desc = {};
    heap_desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    heap_desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    ComPtr<ID3D12Heap> heap;
    ASSERT_TRUE(SUCCEEDED(
        context_.device()->CreateHeap(&heap_desc, IID_PPV_ARGS(heap.put()))));

    const D3D12_QUERY_HEAP_DESC query_desc = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
                                              1, 0};
    ComPtr<ID3D12QueryHeap> query_heap;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateQueryHeap(
        &query_desc, IID_PPV_ARGS(query_heap.put()))));

    D3D12_INDIRECT_ARGUMENT_DESC argument_desc = {};
    argument_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC signature_desc = {};
    signature_desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    signature_desc.NumArgumentDescs = 1;
    signature_desc.pArgumentDescs = &argument_desc;
    ComPtr<ID3D12CommandSignature> command_signature;
    ASSERT_TRUE(SUCCEEDED(context_.device()->CreateCommandSignature(
        &signature_desc, nullptr, IID_PPV_ARGS(command_signature.put()))));

    ASSERT_TRUE(resource);
    ASSERT_TRUE(descriptor_heap);
    ASSERT_TRUE(root_signature);

    visit("device", static_cast<IUnknown *>(context_.device()), false);
    visit("queue", static_cast<IUnknown *>(context_.queue()), true);
    visit("allocator", static_cast<IUnknown *>(context_.allocator()), true);
    visit("command list", static_cast<IUnknown *>(context_.list()), true);
    visit("fence", static_cast<IUnknown *>(fence.get()), true);
    visit("heap", static_cast<IUnknown *>(heap.get()), true);
    visit("resource", static_cast<IUnknown *>(resource.get()), true);
    visit("descriptor heap", static_cast<IUnknown *>(descriptor_heap.get()),
          true);
    visit("root signature", static_cast<IUnknown *>(root_signature.get()),
          true);
    visit("query heap", static_cast<IUnknown *>(query_heap.get()), true);
    visit("command signature", static_cast<IUnknown *>(command_signature.get()),
          true);
  }

  D3D12TestContext context_;
};

TEST_F(ComIdentitySpec, IUnknownIdentityIsStable) {
  ForEachObject([](const char *name, IUnknown *object, bool) {
    ComPtr<IUnknown> direct_identity;
    ASSERT_TRUE(
        SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(direct_identity.put()))))
        << name;
    ComPtr<ID3D12Object> d3d_object;
    ASSERT_TRUE(
        SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(d3d_object.put()))))
        << name;
    ComPtr<IUnknown> object_identity;
    ASSERT_TRUE(SUCCEEDED(
        d3d_object->QueryInterface(IID_PPV_ARGS(object_identity.put()))))
        << name;

    EXPECT_EQ(direct_identity.get(), object_identity.get()) << name;
  });
}

TEST_F(ComIdentitySpec, RepeatedQueryReturnsSameIdentity) {
  ForEachObject([](const char *name, IUnknown *object, bool) {
    ComPtr<IUnknown> first;
    ComPtr<IUnknown> second;
    ASSERT_TRUE(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(first.put()))))
        << name;
    ASSERT_TRUE(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(second.put()))))
        << name;

    EXPECT_EQ(first.get(), second.get()) << name;
  });
}

TEST_F(ComIdentitySpec, UnsupportedInterfaceClearsOutput) {
  const GUID unsupported = {0x130e9f81,
                            0x7fb8,
                            0x4d5c,
                            {0xa1, 0x53, 0xc8, 0x1f, 0x2b, 0x37, 0xe6, 0x44}};
  ForEachObject([&](const char *name, IUnknown *object, bool) {
    void *output = reinterpret_cast<void *>(uintptr_t{1});

    EXPECT_EQ(object->QueryInterface(unsupported, &output), E_NOINTERFACE)
        << name;
    EXPECT_EQ(output, nullptr) << name;
  });
}

TEST_F(ComIdentitySpec, NullOutputPointerIsRejected) {
  ForEachObject([](const char *name, IUnknown *object, bool) {
    EXPECT_EQ(object->QueryInterface(__uuidof(IUnknown), nullptr), E_POINTER)
        << name;
  });
}

TEST_F(ComIdentitySpec, DeviceChildrenReturnCreatingDeviceIdentity) {
  ComPtr<IUnknown> expected_device;
  ASSERT_TRUE(SUCCEEDED(
      context_.device()->QueryInterface(IID_PPV_ARGS(expected_device.put()))));

  ForEachObject([&](const char *name, IUnknown *object, bool device_child) {
    if (!device_child)
      return;
    ComPtr<ID3D12DeviceChild> child;
    ASSERT_TRUE(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(child.put()))))
        << name;
    ComPtr<IUnknown> actual_device;
    ASSERT_TRUE(SUCCEEDED(child->GetDevice(IID_PPV_ARGS(actual_device.put()))))
        << name;

    EXPECT_EQ(actual_device.get(), expected_device.get()) << name;
  });
}

TEST_F(ComIdentitySpec, GetDeviceClearsOutputForUnsupportedInterface) {
  ForEachObject([](const char *name, IUnknown *object, bool device_child) {
    if (!device_child)
      return;
    ComPtr<ID3D12DeviceChild> child;
    ASSERT_TRUE(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(child.put()))))
        << name;
    void *output = reinterpret_cast<void *>(uintptr_t{1});

    EXPECT_EQ(child->GetDevice(__uuidof(ID3D12GraphicsCommandList), &output),
              E_NOINTERFACE)
        << name;
    EXPECT_EQ(output, nullptr) << name;
  });
}

TEST_F(ComIdentitySpec, GetDeviceNullOutputPointerIsRejected) {
  ForEachObject([](const char *name, IUnknown *object, bool device_child) {
    if (!device_child)
      return;
    ComPtr<ID3D12DeviceChild> child;
    ASSERT_TRUE(SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(child.put()))))
        << name;

    EXPECT_EQ(child->GetDevice(__uuidof(IUnknown), nullptr), E_POINTER) << name;
  });
}

} // namespace
