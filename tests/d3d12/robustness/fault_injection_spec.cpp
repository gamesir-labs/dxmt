#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include <d3d12.h>

#include <cstdlib>
#include <cstring>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;

UINT ConfiguredOccurrence(const char *name) {
  const char *value = std::getenv(name);
  if (!value || !*value)
    return 0;
  char *end = nullptr;
  const auto parsed = std::strtoul(value, &end, 0);
  return end != value && !*end && parsed <= UINT_MAX
             ? static_cast<UINT>(parsed)
             : 0;
}

bool ConfiguredAlways(const char *name) {
  const char *value = std::getenv(name);
  return value && (std::strcmp(value, "always") == 0 ||
                   std::strcmp(value, "all") == 0);
}

class D3D12CreationFaultInjectionSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(device_.put())),
              S_OK);
  }

  ComPtr<ID3DBlob> SerializeEmptyRootSignature() const {
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> diagnostics;
    if (FAILED(D3D12SerializeRootSignature(
            &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, blob.put(),
            diagnostics.put())))
      return {};
    return blob;
  }

  ComPtr<ID3D12RootSignature> CreateEmptyRootSignature() const {
    auto blob = SerializeEmptyRootSignature();
    if (!blob)
      return {};
    ComPtr<ID3D12RootSignature> root;
    if (FAILED(device_->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(root.put()))))
      return {};
    return root;
  }

  ComPtr<ID3D12Device> device_;
};

TEST_F(D3D12CreationFaultInjectionSpec,
       ResourceFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_RESOURCE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "resource creation fault injection is disabled";

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 4096;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device_->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS(resource.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(resource);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(resource);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       RepeatedResourceFailuresRecoverAfterFaultIsDisabled) {
  constexpr const char *fault = "DXMT_TEST_FAIL_RESOURCE_CREATION_AT";
  if (!ConfiguredAlways(fault))
    GTEST_SKIP() << "repeated resource creation fault injection is disabled";

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = 4096;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (UINT attempt = 0; attempt < 3; ++attempt) {
    void *output = reinterpret_cast<void *>(uintptr_t{1});
    EXPECT_EQ(device_->CreateCommittedResource(
                  &heap, D3D12_HEAP_FLAG_NONE, &desc,
                  D3D12_RESOURCE_STATE_COMMON, nullptr,
                  __uuidof(ID3D12Resource), &output),
              E_OUTOFMEMORY)
        << "attempt=" << attempt;
    EXPECT_EQ(output, nullptr) << "attempt=" << attempt;
  }

  ASSERT_TRUE(SetEnvironmentVariableA(fault, nullptr));
  ComPtr<ID3D12Resource> recovered;
  EXPECT_EQ(device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(recovered.put())),
            S_OK);
  EXPECT_TRUE(recovered);
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       DescriptorHeapFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_DESCRIPTOR_HEAP_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "descriptor heap fault injection is disabled";

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 4;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12DescriptorHeap> heap;
    const HRESULT hr = device_->CreateDescriptorHeap(
        &desc, IID_PPV_ARGS(heap.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(heap);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(heap);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       RepeatedDescriptorHeapFailuresRecoverAfterFaultIsDisabled) {
  constexpr const char *fault =
      "DXMT_TEST_FAIL_DESCRIPTOR_HEAP_CREATION_AT";
  if (!ConfiguredAlways(fault))
    GTEST_SKIP()
        << "repeated descriptor heap creation fault injection is disabled";

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 4;
  for (UINT attempt = 0; attempt < 3; ++attempt) {
    void *output = reinterpret_cast<void *>(uintptr_t{1});
    EXPECT_EQ(device_->CreateDescriptorHeap(
                  &desc, __uuidof(ID3D12DescriptorHeap), &output),
              E_OUTOFMEMORY)
        << "attempt=" << attempt;
    EXPECT_EQ(output, nullptr) << "attempt=" << attempt;
  }

  ASSERT_TRUE(SetEnvironmentVariableA(fault, nullptr));
  ComPtr<ID3D12DescriptorHeap> recovered;
  EXPECT_EQ(device_->CreateDescriptorHeap(&desc,
                                           IID_PPV_ARGS(recovered.put())),
            S_OK);
  EXPECT_TRUE(recovered);
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       ComputePipelineFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_COMPUTE_PIPELINE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "compute pipeline fault injection is disabled";

  const auto shader =
      CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto root = CreateEmptyRootSignature();
  ASSERT_TRUE(root);
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.get();
  desc.CS = {shader.bytecode->GetBufferPointer(),
             shader.bytecode->GetBufferSize()};
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = device_->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(pipeline.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(pipeline);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(pipeline);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       RepeatedComputePipelineFailuresRecoverAfterFaultIsDisabled) {
  constexpr const char *fault =
      "DXMT_TEST_FAIL_COMPUTE_PIPELINE_CREATION_AT";
  if (!ConfiguredAlways(fault))
    GTEST_SKIP()
        << "repeated compute pipeline creation fault injection is disabled";

  const auto shader =
      CompileShader("[numthreads(1, 1, 1)] void main() {}", "cs_5_0");
  ASSERT_EQ(shader.result, S_OK) << shader.diagnostic_text();
  auto root = CreateEmptyRootSignature();
  ASSERT_TRUE(root);
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.get();
  desc.CS = {shader.bytecode->GetBufferPointer(),
             shader.bytecode->GetBufferSize()};
  for (UINT attempt = 0; attempt < 3; ++attempt) {
    void *output = reinterpret_cast<void *>(uintptr_t{1});
    EXPECT_EQ(device_->CreateComputePipelineState(
                  &desc, __uuidof(ID3D12PipelineState), &output),
              E_OUTOFMEMORY)
        << "attempt=" << attempt;
    EXPECT_EQ(output, nullptr) << "attempt=" << attempt;
  }

  ASSERT_TRUE(SetEnvironmentVariableA(fault, nullptr));
  ComPtr<ID3D12PipelineState> recovered;
  EXPECT_EQ(device_->CreateComputePipelineState(
                &desc, IID_PPV_ARGS(recovered.put())),
            S_OK);
  EXPECT_TRUE(recovered);
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       CommandQueueFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_COMMAND_QUEUE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "command queue creation fault injection is disabled";

  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12CommandQueue> queue;
    const HRESULT hr =
        device_->CreateCommandQueue(&desc, IID_PPV_ARGS(queue.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(queue);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(queue);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       GraphicsPipelineFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_GRAPHICS_PIPELINE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "graphics pipeline creation fault injection is disabled";

  const auto vertex = CompileShader(R"(
    float4 main(uint id : SV_VertexID) : SV_Position {
      const float2 positions[3] = {
        float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
      };
      return float4(positions[id], 0.0, 1.0);
    }
  )", "vs_5_0");
  const auto pixel = CompileShader(
      "float4 main() : SV_Target { return float4(1, 1, 1, 1); }",
      "ps_5_0");
  ASSERT_EQ(vertex.result, S_OK) << vertex.diagnostic_text();
  ASSERT_EQ(pixel.result, S_OK) << pixel.diagnostic_text();
  auto root = CreateEmptyRootSignature();
  ASSERT_TRUE(root);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root.get();
  desc.VS = {vertex.bytecode->GetBufferPointer(),
             vertex.bytecode->GetBufferSize()};
  desc.PS = {pixel.bytecode->GetBufferPointer(),
             pixel.bytecode->GetBufferSize()};
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.SampleMask = UINT_MAX;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12PipelineState> pipeline;
    const HRESULT hr = device_->CreateGraphicsPipelineState(
        &desc, IID_PPV_ARGS(pipeline.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(pipeline);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(pipeline);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       HeapFailureClearsOutputAndNextCallRecovers) {
  const UINT target = ConfiguredOccurrence("DXMT_TEST_FAIL_HEAP_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "heap creation fault injection is disabled";

  D3D12_HEAP_DESC desc = {};
  desc.SizeInBytes = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  desc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12Heap> heap;
    const HRESULT hr = device_->CreateHeap(&desc, IID_PPV_ARGS(heap.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(heap);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(heap);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       RootSignatureFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_ROOT_SIGNATURE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "root signature creation fault injection is disabled";

  auto blob = SerializeEmptyRootSignature();
  ASSERT_TRUE(blob);
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12RootSignature> root;
    const HRESULT hr = device_->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(root.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(root);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(root);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       FenceFailureClearsOutputAndNextCallRecovers) {
  const UINT target = ConfiguredOccurrence("DXMT_TEST_FAIL_FENCE_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "fence creation fault injection is disabled";

  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12Fence> fence;
    const HRESULT hr = device_->CreateFence(
        occurrence, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(fence);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(fence);
      EXPECT_EQ(fence->GetCompletedValue(), occurrence);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D12CreationFaultInjectionSpec,
       QueryHeapFailureClearsOutputAndNextCallRecovers) {
  const UINT target =
      ConfiguredOccurrence("DXMT_TEST_FAIL_QUERY_HEAP_CREATION_AT");
  if (!target)
    GTEST_SKIP() << "query heap creation fault injection is disabled";

  D3D12_QUERY_HEAP_DESC desc = {};
  desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  desc.Count = 2;
  for (UINT occurrence = 1; occurrence <= target + 1; ++occurrence) {
    ComPtr<ID3D12QueryHeap> heap;
    const HRESULT hr =
        device_->CreateQueryHeap(&desc, IID_PPV_ARGS(heap.put()));
    if (occurrence == target) {
      EXPECT_EQ(hr, E_OUTOFMEMORY);
      EXPECT_FALSE(heap);
    } else {
      EXPECT_EQ(hr, S_OK);
      EXPECT_TRUE(heap);
    }
  }
  EXPECT_EQ(device_->GetDeviceRemovedReason(), S_OK);
}

} // namespace
