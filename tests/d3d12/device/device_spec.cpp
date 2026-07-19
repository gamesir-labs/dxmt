#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "d3d12_pipeline_stream.hpp"
#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>

namespace {

using dxmt::test::PipelineSubobject;

template <typename T> void release_object(T*& object) {
  if (object) {
    object->Release();
    object = nullptr;
  }
}

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure()
      << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

ID3D12RootSignature *CreateRootSignature(
    ID3D12Device *device, const D3D12_ROOT_SIGNATURE_DESC &desc) {
  ID3DBlob *blob = nullptr;
  ID3DBlob *error = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error);
  release_object(error);
  if (FAILED(hr)) {
    release_object(blob);
    return nullptr;
  }

  ID3D12RootSignature *root_signature = nullptr;
  hr = device->CreateRootSignature(
      0, blob->GetBufferPointer(), blob->GetBufferSize(),
      __uuidof(ID3D12RootSignature),
      reinterpret_cast<void **>(&root_signature));
  release_object(blob);
  return SUCCEEDED(hr) ? root_signature : nullptr;
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC BasicGraphicsPipelineDesc(
    ID3D12RootSignature *root_signature) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature;
  desc.VS = dxmt::test::FullscreenVertexShader();
  desc.PS = dxmt::test::TextureUavPixelShader();
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  desc.SampleMask = UINT_MAX;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  return desc;
}

class D3D12DeviceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                   __uuidof(ID3D12Device),
                                   reinterpret_cast<void**>(&device_));

    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(device_, nullptr);
  }

  void TearDown() override {
    release_object(device_);
  }

  ID3D12Device* device_ = nullptr;
};

} // namespace

TEST(D3D12DeviceCreationSpec, RejectsFeatureLevel93) {
  ID3D12Device* device = nullptr;

  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_9_3,
                                 __uuidof(ID3D12Device),
                                 reinterpret_cast<void**>(&device));

  EXPECT_EQ(hr, E_INVALIDARG);
  EXPECT_EQ(device, nullptr);

  release_object(device);
}

TEST(D3D12DeviceCreationSpec, SupportsCapabilityProbeWithoutOutput) {
  EXPECT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                              __uuidof(ID3D12Device), nullptr),
            S_FALSE);
  EXPECT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                              __uuidof(ID3D12Device), nullptr),
            S_FALSE);
}

TEST(D3D12DeviceCreationSpec, CapabilityProbeStillValidatesFeatureLevel) {
  EXPECT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_9_3,
                              __uuidof(ID3D12Device), nullptr),
            E_INVALIDARG);
}

TEST(D3D12DeviceCreationSpec, CreatesAtMaximumAdvertisedFeatureLevel) {
  ID3D12Device *device = nullptr;
  EXPECT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
                              __uuidof(ID3D12Device),
                              reinterpret_cast<void **>(&device)),
            S_OK);
  EXPECT_NE(device, nullptr);
  release_object(device);
}

TEST(D3D12DeviceCreationSpec, UnsupportedInterfaceClearsOutput) {
  const GUID unsupported = {0xe2696eb4,
                            0xd9a0,
                            0x46ab,
                            {0xb4, 0x21, 0x39, 0xc5, 0xcf, 0x3f, 0xc0, 0x55}};
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, unsupported,
                              &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
}

TEST(D3D12DeviceCreationSpec, RejectsNonAdapterAndClearsOutput) {
  ID3D12Device *existing = nullptr;
  ASSERT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                              __uuidof(ID3D12Device),
                              reinterpret_cast<void **>(&existing)),
            S_OK);
  void *output = reinterpret_cast<void *>(std::uintptr_t{1});
  EXPECT_EQ(D3D12CreateDevice(existing, D3D_FEATURE_LEVEL_11_0,
                              __uuidof(ID3D12Device), &output),
            E_INVALIDARG);
  EXPECT_EQ(output, nullptr);
  release_object(existing);
}

TEST(D3D12DeviceCreationSpec,
     RepeatedAndExplicitCreationPreserveAdapterIdentity) {
  ID3D12Device *first = nullptr;
  ID3D12Device *repeated = nullptr;
  ASSERT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                              __uuidof(ID3D12Device),
                              reinterpret_cast<void **>(&first)),
            S_OK);
  ASSERT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                              __uuidof(ID3D12Device),
                              reinterpret_cast<void **>(&repeated)),
            S_OK);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(repeated, nullptr);

  const LUID expected_luid = first->GetAdapterLuid();
  const LUID repeated_luid = repeated->GetAdapterLuid();
  EXPECT_EQ(repeated_luid.HighPart, expected_luid.HighPart);
  EXPECT_EQ(repeated_luid.LowPart, expected_luid.LowPart);
  EXPECT_EQ(first->GetNodeCount(), 1u);
  EXPECT_EQ(repeated->GetNodeCount(), 1u);

  IDXGIFactory1 *factory = nullptr;
  ASSERT_EQ(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                               reinterpret_cast<void **>(&factory)),
            S_OK);
  ASSERT_NE(factory, nullptr);
  IDXGIAdapter1 *matching_adapter = nullptr;
  for (UINT index = 0;; ++index) {
    IDXGIAdapter1 *candidate = nullptr;
    const HRESULT hr = factory->EnumAdapters1(index, &candidate);
    if (hr == DXGI_ERROR_NOT_FOUND)
      break;
    ASSERT_EQ(hr, S_OK);
    ASSERT_NE(candidate, nullptr);
    DXGI_ADAPTER_DESC1 desc = {};
    ASSERT_EQ(candidate->GetDesc1(&desc), S_OK);
    if (desc.AdapterLuid.HighPart == expected_luid.HighPart &&
        desc.AdapterLuid.LowPart == expected_luid.LowPart) {
      matching_adapter = candidate;
      break;
    }
    release_object(candidate);
  }
  ASSERT_NE(matching_adapter, nullptr);

  ID3D12Device *explicit_device = nullptr;
  ASSERT_EQ(D3D12CreateDevice(matching_adapter, D3D_FEATURE_LEVEL_11_0,
                              __uuidof(ID3D12Device),
                              reinterpret_cast<void **>(&explicit_device)),
            S_OK);
  ASSERT_NE(explicit_device, nullptr);
  const LUID explicit_luid = explicit_device->GetAdapterLuid();
  EXPECT_EQ(explicit_luid.HighPart, expected_luid.HighPart);
  EXPECT_EQ(explicit_luid.LowPart, expected_luid.LowPart);
  EXPECT_EQ(explicit_device->GetNodeCount(), 1u);

  release_object(explicit_device);
  release_object(matching_adapter);
  release_object(factory);
  release_object(repeated);
  release_object(first);
}

TEST_F(D3D12DeviceSpec, ReportsAtLeastOneNode) {
  EXPECT_GE(device_->GetNodeCount(), 1u);
}

TEST_F(D3D12DeviceSpec, AdapterLuidMatchesEnumeratedDxgiAdapter) {
  IDXGIFactory1 *factory = nullptr;
  ASSERT_EQ(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                               reinterpret_cast<void **>(&factory)),
            S_OK);
  ASSERT_NE(factory, nullptr);

  const LUID device_luid = device_->GetAdapterLuid();
  bool matched = false;
  for (UINT index = 0;; ++index) {
    IDXGIAdapter1 *adapter = nullptr;
    const HRESULT hr = factory->EnumAdapters1(index, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND)
      break;
    ASSERT_EQ(hr, S_OK);
    ASSERT_NE(adapter, nullptr);
    DXGI_ADAPTER_DESC1 desc = {};
    EXPECT_EQ(adapter->GetDesc1(&desc), S_OK);
    matched = matched ||
              (desc.AdapterLuid.HighPart == device_luid.HighPart &&
               desc.AdapterLuid.LowPart == device_luid.LowPart);
    release_object(adapter);
  }
  EXPECT_TRUE(matched);
  release_object(factory);
}

TEST_F(D3D12DeviceSpec, ReportsDescriptorHeapVisibilityAndStableHandles) {
  struct HeapCase {
    D3D12_DESCRIPTOR_HEAP_TYPE type;
    D3D12_DESCRIPTOR_HEAP_FLAGS flags;
  };
  constexpr std::array cases = {
      HeapCase{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
               D3D12_DESCRIPTOR_HEAP_FLAG_NONE},
      HeapCase{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
               D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE},
      HeapCase{D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
               D3D12_DESCRIPTOR_HEAP_FLAG_NONE},
      HeapCase{D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
               D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE},
      HeapCase{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
               D3D12_DESCRIPTOR_HEAP_FLAG_NONE},
      HeapCase{D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
               D3D12_DESCRIPTOR_HEAP_FLAG_NONE},
  };

  for (const auto &test_case : cases) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = test_case.type;
    desc.NumDescriptors = 3;
    desc.Flags = test_case.flags;
    ID3D12DescriptorHeap *heap = nullptr;
    ASSERT_TRUE(HResultSucceeded(device_->CreateDescriptorHeap(
        &desc, __uuidof(ID3D12DescriptorHeap),
        reinterpret_cast<void **>(&heap))));
    ASSERT_NE(heap, nullptr);

    const auto actual = heap->GetDesc();
    EXPECT_EQ(actual.Type, desc.Type);
    EXPECT_EQ(actual.NumDescriptors, desc.NumDescriptors);
    EXPECT_EQ(actual.Flags, desc.Flags);
    if (actual.NodeMask != 0) {
      EXPECT_EQ(actual.NodeMask & (actual.NodeMask - 1), 0u);
      EXPECT_LT(std::countr_zero(actual.NodeMask), device_->GetNodeCount());
    }
    const UINT increment =
        device_->GetDescriptorHandleIncrementSize(test_case.type);
    EXPECT_GT(increment, 0u);
    const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    EXPECT_NE(cpu.ptr, 0u);
    EXPECT_EQ(heap->GetCPUDescriptorHandleForHeapStart().ptr, cpu.ptr);
    const auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    if (test_case.flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
      EXPECT_NE(gpu.ptr, 0u);
    }
    EXPECT_EQ(heap->GetGPUDescriptorHandleForHeapStart().ptr, gpu.ptr);

    ID3D12Device *owner = nullptr;
    ASSERT_TRUE(HResultSucceeded(heap->GetDevice(
        __uuidof(ID3D12Device), reinterpret_cast<void **>(&owner))));
    EXPECT_EQ(owner, device_);
    release_object(owner);
    release_object(heap);
  }
}

TEST_F(D3D12DeviceSpec, RejectsShaderVisibleRtvAndDsvHeaps) {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.NumDescriptors = 1;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  for (const auto type : {D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                          D3D12_DESCRIPTOR_HEAP_TYPE_DSV}) {
    desc.Type = type;
    ID3D12DescriptorHeap *heap = nullptr;
    EXPECT_EQ(device_->CreateDescriptorHeap(
                  &desc, __uuidof(ID3D12DescriptorHeap),
                  reinterpret_cast<void **>(&heap)),
              E_INVALIDARG);
    EXPECT_EQ(heap, nullptr);
  }
}

TEST_F(D3D12DeviceSpec, CreatesDirectCommandQueue) {
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.NodeMask = 0;

  ID3D12CommandQueue* queue = nullptr;

  HRESULT hr = device_->CreateCommandQueue(
      &desc, __uuidof(ID3D12CommandQueue),
      reinterpret_cast<void**>(&queue));

  EXPECT_TRUE(HResultSucceeded(hr));
  EXPECT_NE(queue, nullptr);

  release_object(queue);
}

TEST_F(D3D12DeviceSpec, SingleNodeAcceptsNodeMaskZeroAndOne) {
  ASSERT_EQ(device_->GetNodeCount(), 1u);
  for (const UINT node_mask : {0u, 1u}) {
    SCOPED_TRACE(node_mask);
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = node_mask;
    ID3D12CommandQueue *queue = nullptr;
    ASSERT_EQ(device_->CreateCommandQueue(
                  &desc, __uuidof(ID3D12CommandQueue),
                  reinterpret_cast<void **>(&queue)),
              S_OK);
    ASSERT_NE(queue, nullptr);
    const auto actual = queue->GetDesc();
    EXPECT_EQ(actual.Type, D3D12_COMMAND_LIST_TYPE_DIRECT);
    // NodeMask 0 means "default node" on a single-node device and may be
    // reported back as either 0 or the concrete node bit 0x1.
    EXPECT_TRUE(actual.NodeMask == 0u || actual.NodeMask == 1u);
    release_object(queue);
  }
}

TEST_F(D3D12DeviceSpec, RejectsBundleCommandQueue) {
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_BUNDLE;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  ID3D12CommandQueue *queue = nullptr;
  EXPECT_EQ(device_->CreateCommandQueue(
                &desc, __uuidof(ID3D12CommandQueue),
                reinterpret_cast<void **>(&queue)),
            E_INVALIDARG);
  EXPECT_EQ(queue, nullptr);
}

TEST_F(D3D12DeviceSpec, CreatesDirectCommandAllocator) {
  ID3D12CommandAllocator* allocator = nullptr;

  HRESULT hr = device_->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
      reinterpret_cast<void**>(&allocator));

  EXPECT_TRUE(HResultSucceeded(hr));
  EXPECT_NE(allocator, nullptr);

  release_object(allocator);
}

TEST_F(D3D12DeviceSpec, CreatesFenceWithInitialValue) {
  ID3D12Fence* fence = nullptr;

  HRESULT hr = device_->CreateFence(7, D3D12_FENCE_FLAG_NONE,
                                    __uuidof(ID3D12Fence),
                                    reinterpret_cast<void**>(&fence));

  ASSERT_TRUE(HResultSucceeded(hr));
  ASSERT_NE(fence, nullptr);

  EXPECT_EQ(fence->GetCompletedValue(), 7ull);

  release_object(fence);
}

TEST_F(D3D12DeviceSpec, CreatesEveryPublicQueryHeapType) {
  for (D3D12_QUERY_HEAP_TYPE type : {
           D3D12_QUERY_HEAP_TYPE_OCCLUSION,
           D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
           D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,
           D3D12_QUERY_HEAP_TYPE_SO_STATISTICS}) {
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = type;
    desc.Count = 1;
    ID3D12QueryHeap *heap = nullptr;
    EXPECT_TRUE(HResultSucceeded(device_->CreateQueryHeap(
        &desc, __uuidof(ID3D12QueryHeap),
        reinterpret_cast<void **>(&heap))));
    EXPECT_NE(heap, nullptr);
    release_object(heap);
  }
}

TEST_F(D3D12DeviceSpec, DoesNotAdvertiseUnimplementedDepthStencilResolve) {
  for (DXGI_FORMAT format : {DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D32_FLOAT,
                             DXGI_FORMAT_D32_FLOAT_S8X24_UINT}) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    ASSERT_EQ(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                           &support, sizeof(support)),
              S_OK)
        << "format " << format;
    EXPECT_EQ(support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE, 0u)
        << "format " << format;
  }
}

TEST_F(D3D12DeviceSpec, CreatesStateChangingIndirectSignatures) {
  D3D12_INDIRECT_ARGUMENT_DESC arguments[2] = {};
  arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
  arguments[0].VertexBuffer.Slot = 0;
  arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  D3D12_COMMAND_SIGNATURE_DESC desc = {};
  desc.ByteStride = sizeof(D3D12_VERTEX_BUFFER_VIEW) +
                    sizeof(D3D12_DRAW_ARGUMENTS);
  desc.NumArgumentDescs = 2;
  desc.pArgumentDescs = arguments;
  ID3D12CommandSignature *signature = nullptr;
  EXPECT_EQ(device_->CreateCommandSignature(
                &desc, nullptr, __uuidof(ID3D12CommandSignature),
                reinterpret_cast<void **>(&signature)),
            S_OK);
  EXPECT_NE(signature, nullptr);
  release_object(signature);

  arguments[0] = {};
  arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
  desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
  desc.NumArgumentDescs = 1;
  desc.pArgumentDescs = arguments;
  EXPECT_TRUE(HResultSucceeded(device_->CreateCommandSignature(
      &desc, nullptr, __uuidof(ID3D12CommandSignature),
      reinterpret_cast<void **>(&signature))));
  EXPECT_NE(signature, nullptr);
  release_object(signature);
}

TEST_F(D3D12DeviceSpec, RejectsUnadvertisedRootSignature12Inputs) {
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
  desc.Version = static_cast<D3D_ROOT_SIGNATURE_VERSION>(3);

  ID3DBlob *blob = nullptr;
  ID3DBlob *error = nullptr;
  HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &error);
  EXPECT_EQ(hr, E_INVALIDARG);
  EXPECT_EQ(blob, nullptr);
  release_object(error);

  // Structurally valid DXBC container with one empty RTS0 1.2 part. Keep the
  // checksum zero, matching the runtime serializer's accepted container form.
  constexpr std::uint32_t dxbc = 0x43425844;
  constexpr std::uint32_t rts0 = 0x30535452;
  const std::array<std::uint32_t, 17> root_signature_1_2 = {
      dxbc, 0, 0, 0, 0, 1, 68, 1, 36,
      rts0, 24, 3, 0, 0, 0, 0, 0};
  ID3D12RootSignature *root_signature = nullptr;
  hr = device_->CreateRootSignature(
      0, root_signature_1_2.data(), sizeof(root_signature_1_2),
      __uuidof(ID3D12RootSignature),
      reinterpret_cast<void **>(&root_signature));
  EXPECT_EQ(hr, E_INVALIDARG);
  EXPECT_EQ(root_signature, nullptr);
}

TEST_F(D3D12DeviceSpec, PipelineLibraryResultMatchesShaderCacheCapability) {
  D3D12_FEATURE_DATA_SHADER_CACHE feature = {};
  ASSERT_EQ(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &feature,
                                         sizeof(feature)),
            S_OK);
  constexpr auto known_support = static_cast<D3D12_SHADER_CACHE_SUPPORT_FLAGS>(
      D3D12_SHADER_CACHE_SUPPORT_SINGLE_PSO |
      D3D12_SHADER_CACHE_SUPPORT_LIBRARY |
      D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_INPROC_CACHE |
      D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE |
      D3D12_SHADER_CACHE_SUPPORT_DRIVER_MANAGED_CACHE |
      D3D12_SHADER_CACHE_SUPPORT_SHADER_CONTROL_CLEAR |
      D3D12_SHADER_CACHE_SUPPORT_SHADER_SESSION_DELETE);
  EXPECT_EQ(feature.SupportFlags & ~known_support, 0u);

#ifdef __ID3D12Device1_INTERFACE_DEFINED__
  ID3D12Device1 *device1 = nullptr;
  ASSERT_EQ(device_->QueryInterface(__uuidof(ID3D12Device1),
                                    reinterpret_cast<void **>(&device1)),
            S_OK);
  ASSERT_NE(device1, nullptr);

  if (feature.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY) {
    ID3D12PipelineLibrary *library = nullptr;
    EXPECT_EQ(device1->CreatePipelineLibrary(
                  nullptr, 0, __uuidof(ID3D12PipelineLibrary),
                  reinterpret_cast<void **>(&library)),
              S_OK);
    EXPECT_NE(library, nullptr);
    release_object(library);
    EXPECT_EQ(device1->CreatePipelineLibrary(
                  nullptr, 0, __uuidof(ID3D12PipelineLibrary), nullptr),
              S_FALSE);
  }
  release_object(device1);
#endif
}

TEST_F(D3D12DeviceSpec, CreatesStreamOutputPipelineFromPublicDescriptor) {
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = UINT_MAX;
  range.BaseShaderRegister = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  ID3D12RootSignature *root_signature =
      CreateRootSignature(device_, root_desc);
  ASSERT_NE(root_signature, nullptr);

  auto desc = BasicGraphicsPipelineDesc(root_signature);
  ID3D12PipelineState *pipeline = nullptr;
  ASSERT_EQ(device_->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline)),
            S_OK);
  release_object(pipeline);

  D3D12_SO_DECLARATION_ENTRY entry = {};
  entry.SemanticName = "SV_Position";
  entry.ComponentCount = 4;
  UINT stride = sizeof(float) * 4;
  desc.StreamOutput.pSODeclaration = &entry;
  desc.StreamOutput.NumEntries = 1;
  desc.StreamOutput.pBufferStrides = &stride;
  desc.StreamOutput.NumStrides = 1;
  ASSERT_EQ(device_->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline)),
            S_OK);
  ASSERT_NE(pipeline, nullptr);
  release_object(pipeline);
  release_object(root_signature);
}

TEST_F(D3D12DeviceSpec, CreatesEveryAdvertisedMsaaPso) {
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = UINT_MAX;
  range.BaseShaderRegister = 1;
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameter.DescriptorTable.NumDescriptorRanges = 1;
  parameter.DescriptorTable.pDescriptorRanges = &range;
  parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &parameter;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ID3D12RootSignature *root_signature =
      CreateRootSignature(device_, root_desc);
  ASSERT_NE(root_signature, nullptr);

  UINT advertised = 0;
  for (const UINT sample_count : {2u, 4u, 8u, 16u}) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {};
    levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    levels.SampleCount = sample_count;
    ASSERT_EQ(device_->CheckFeatureSupport(
                  D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels,
                  sizeof(levels)),
              S_OK);
    if (!levels.NumQualityLevels)
      continue;
    ++advertised;

    SCOPED_TRACE(::testing::Message() << "sample_count=" << sample_count);
    auto desc = BasicGraphicsPipelineDesc(root_signature);
    desc.SampleDesc.Count = sample_count;
    ID3D12PipelineState *pipeline = nullptr;
    ASSERT_EQ(device_->CreateGraphicsPipelineState(
                  &desc, __uuidof(ID3D12PipelineState),
                  reinterpret_cast<void **>(&pipeline)),
              S_OK);
    ASSERT_NE(pipeline, nullptr);
    release_object(pipeline);
  }
  EXPECT_GT(advertised, 0u);
  release_object(root_signature);
}

TEST_F(D3D12DeviceSpec, IgnoresSharedBlendStateForReportedNonBlendableTarget) {
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = UINT_MAX;
  range.BaseShaderRegister = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ID3D12RootSignature *root_signature =
      CreateRootSignature(device_, root_desc);
  ASSERT_NE(root_signature, nullptr);

  DXGI_FORMAT non_blendable_format = DXGI_FORMAT_UNKNOWN;
  for (const DXGI_FORMAT format : {
           DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT,
           DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT,
           DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT}) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    support.Format = format;
    ASSERT_EQ(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,
                                           &support, sizeof(support)),
              S_OK);
    if ((support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) &&
        !(support.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE)) {
      non_blendable_format = format;
      break;
    }
  }
  ASSERT_NE(non_blendable_format, DXGI_FORMAT_UNKNOWN);

  auto desc = BasicGraphicsPipelineDesc(root_signature);
  desc.NumRenderTargets = 2;
  desc.RTVFormats[1] = non_blendable_format;
  desc.BlendState.IndependentBlendEnable = FALSE;
  auto &shared_blend = desc.BlendState.RenderTarget[0];
  shared_blend.BlendEnable = TRUE;
  shared_blend.SrcBlend = D3D12_BLEND_ONE;
  shared_blend.DestBlend = D3D12_BLEND_ZERO;
  shared_blend.BlendOp = D3D12_BLEND_OP_ADD;
  shared_blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  shared_blend.DestBlendAlpha = D3D12_BLEND_ZERO;
  shared_blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;

  ID3D12PipelineState *pipeline = nullptr;
  EXPECT_EQ(device_->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline)),
            S_OK);
  EXPECT_NE(pipeline, nullptr);
  release_object(pipeline);
  release_object(root_signature);
}

TEST_F(D3D12DeviceSpec, IgnoresBlendStateForUnwrittenPixelShaderTarget) {
  D3D12_DESCRIPTOR_RANGE range = {};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  range.NumDescriptors = UINT_MAX;
  range.BaseShaderRegister = 1;
  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.Num32BitValues = 1;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.NumParameters = 2;
  root_desc.pParameters = parameters;
  ID3D12RootSignature *root_signature =
      CreateRootSignature(device_, root_desc);
  ASSERT_NE(root_signature, nullptr);

  auto desc = BasicGraphicsPipelineDesc(root_signature);
  desc.NumRenderTargets = 2;
  desc.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BlendState.IndependentBlendEnable = FALSE;
  auto &shared_blend = desc.BlendState.RenderTarget[0];
  shared_blend.BlendEnable = TRUE;
  shared_blend.SrcBlend = D3D12_BLEND_ONE;
  shared_blend.DestBlend = D3D12_BLEND_ZERO;
  shared_blend.BlendOp = D3D12_BLEND_OP_ADD;
  shared_blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  shared_blend.DestBlendAlpha = D3D12_BLEND_ZERO;
  shared_blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;

  ID3D12PipelineState *pipeline = nullptr;
  EXPECT_EQ(device_->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline)),
            S_OK);
  EXPECT_NE(pipeline, nullptr);
  release_object(pipeline);
  release_object(root_signature);
}

TEST_F(D3D12DeviceSpec, CreatesDualSourceBlendPipelineForTwoPixelOutputs) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  ID3D12RootSignature *root_signature =
      CreateRootSignature(device_, root_desc);
  ASSERT_NE(root_signature, nullptr);

  auto dual_source = dxmt::test::CompileShader(R"(
    struct Output {
      float4 source0 : SV_Target0;
      float4 source1 : SV_Target1;
    };
    Output main() {
      Output output;
      output.source0 = float4(1.0, 0.0, 0.0, 1.0);
      output.source1 = float4(0.25, 0.25, 0.25, 0.25);
      return output;
    })",
                                               "ps_5_0");
  ASSERT_EQ(dual_source.result, S_OK) << dual_source.diagnostic_text();

  auto desc = BasicGraphicsPipelineDesc(root_signature);
  auto &blend = desc.BlendState.RenderTarget[0];
  blend.BlendEnable = TRUE;
  blend.SrcBlend = D3D12_BLEND_ONE;
  blend.DestBlend = D3D12_BLEND_SRC1_COLOR;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_SRC1_ALPHA;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;

  desc.PS = {dual_source.bytecode->GetBufferPointer(),
             dual_source.bytecode->GetBufferSize()};
  ID3D12PipelineState *dual_source_pipeline = nullptr;
  ASSERT_EQ(device_->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&dual_source_pipeline)),
            S_OK);
  ASSERT_NE(dual_source_pipeline, nullptr);
  release_object(dual_source_pipeline);
  release_object(root_signature);
}

#ifdef __ID3D12Device2_INTERFACE_DEFINED__
TEST_F(D3D12DeviceSpec, RejectsPipelineStreamMixingComputeAndPixelShaders) {
  ID3D12Device2 *device2 = nullptr;
  ASSERT_EQ(device_->QueryInterface(__uuidof(ID3D12Device2),
                                    reinterpret_cast<void **>(&device2)),
            S_OK);
  ASSERT_NE(device2, nullptr);

  struct PipelineStream {
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,
                      D3D12_SHADER_BYTECODE>
        compute;
    PipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,
                      D3D12_SHADER_BYTECODE>
        pixel;
  } stream;
  stream.compute.value = dxmt::test::CopyTextureComputeShader();
  stream.pixel.value = dxmt::test::TextureUavPixelShader();
  D3D12_PIPELINE_STATE_STREAM_DESC stream_desc = {sizeof(stream), &stream};

  ID3D12PipelineState *pipeline = nullptr;
  EXPECT_EQ(device2->CreatePipelineState(&stream_desc,
                                         __uuidof(ID3D12PipelineState),
                                         reinterpret_cast<void **>(&pipeline)),
            E_INVALIDARG);
  EXPECT_EQ(pipeline, nullptr);
  release_object(device2);
}
#endif
