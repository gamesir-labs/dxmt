#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include "shaders/runtime_test_shaders.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

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

struct ScopedArchiveTestEnvironment {
  ScopedArchiveTestEnvironment(const char *suffix) {
    std::ostringstream name;
    name << "dxmt-pso-archive-" << GetCurrentProcessId() << "-" << suffix;
    root = "C:\\" + name.str();
    unix_cache_root = "/tmp/" + name.str();
    windows_cache_root = "Z:\\tmp\\" + name.str();
    marker = root + "\\marker.txt";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA(windows_cache_root.c_str(), nullptr);
    SetEnvironmentVariableA("DXMT_SHADER_CACHE", "1");
    SetEnvironmentVariableA("DXMT_SHADER_CACHE_PATH", unix_cache_root.c_str());
    SetEnvironmentVariableA("DXMT_PSO_BINARY_ARCHIVE", "1");
    SetEnvironmentVariableA("DXMT_PSO_ARCHIVE_SERIALIZE_EVERY", "1");
    SetEnvironmentVariableA("DXMT_PSO_ARCHIVE_MARKER", marker.c_str());
  }

  ~ScopedArchiveTestEnvironment() {
    SetEnvironmentVariableA("DXMT_PSO_ARCHIVE_MARKER", nullptr);
    SetEnvironmentVariableA("DXMT_PSO_ARCHIVE_SERIALIZE_EVERY", nullptr);
    SetEnvironmentVariableA("DXMT_PSO_BINARY_ARCHIVE", nullptr);
    SetEnvironmentVariableA("DXMT_SHADER_CACHE_PATH", nullptr);
    SetEnvironmentVariableA("DXMT_SHADER_CACHE", nullptr);
  }

  std::string ReadMarker() const {
    std::ifstream input(marker);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
  }

  std::string root;
  std::string unix_cache_root;
  std::string windows_cache_root;
  std::string marker;
};

ID3D12PipelineState *CreateBasicGraphicsPipeline(ID3D12Device *device) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto *root_signature = CreateRootSignature(device, root_desc);
  if (!root_signature)
    return nullptr;
  auto desc = BasicGraphicsPipelineDesc(root_signature);
  ID3D12PipelineState *pipeline = nullptr;
  const auto hr = device->CreateGraphicsPipelineState(
      &desc, __uuidof(ID3D12PipelineState),
      reinterpret_cast<void **>(&pipeline));
  release_object(root_signature);
  return SUCCEEDED(hr) ? pipeline : nullptr;
}

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type>
struct alignas(void *) ShaderPipelineSubobject {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
  D3D12_SHADER_BYTECODE shader = {};
};

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

TEST(D3D12PipelineArchiveSpec, AttachesAndSerializesPipelineArchive) {
  ScopedArchiveTestEnvironment environment("attached");
  ID3D12Device *device = nullptr;
  ASSERT_TRUE(HResultSucceeded(D3D12CreateDevice(
      nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
      reinterpret_cast<void **>(&device))));
  ASSERT_NE(device, nullptr);

  auto *pipeline = CreateBasicGraphicsPipeline(device);
  ASSERT_NE(pipeline, nullptr);
  release_object(pipeline);
  release_object(device);

  const auto marker = environment.ReadMarker();
  EXPECT_NE(marker.find("serialize reason=periodic count=1 ok=1"),
            std::string::npos)
      << marker;
}

TEST(D3D12PipelineArchiveSpec, RejectsCorruptArchiveAndFallsBackToCompilation) {
  ScopedArchiveTestEnvironment environment("corrupt");
  const auto archive_dir =
      environment.windows_cache_root + "\\com.apple.metal4";
  ASSERT_TRUE(CreateDirectoryA(archive_dir.c_str(), nullptr) ||
              GetLastError() == ERROR_ALREADY_EXISTS);
  const auto archive_path = archive_dir + "\\dxmt_pso.binaryarchive";
  {
    std::ofstream corrupt(archive_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(corrupt.good());
    corrupt << "not-a-metal-binary-archive";
  }

  ID3D12Device *device = nullptr;
  ASSERT_TRUE(HResultSucceeded(D3D12CreateDevice(
      nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
      reinterpret_cast<void **>(&device))));
  ASSERT_NE(device, nullptr);
  auto *pipeline = CreateBasicGraphicsPipeline(device);
  EXPECT_NE(pipeline, nullptr);
  release_object(pipeline);
  release_object(device);

  const auto marker = environment.ReadMarker();
  EXPECT_NE(marker.find("create cold=0 ok=0"), std::string::npos) << marker;
}

TEST_F(D3D12DeviceSpec, ReportsAtLeastOneNode) {
  EXPECT_GE(device_->GetNodeCount(), 1u);
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

TEST_F(D3D12DeviceSpec, RejectsUnsupportedStatisticsQueryHeaps) {
  for (D3D12_QUERY_HEAP_TYPE type : {
           D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,
           D3D12_QUERY_HEAP_TYPE_SO_STATISTICS}) {
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = type;
    desc.Count = 1;
    ID3D12QueryHeap *heap = nullptr;
    EXPECT_EQ(device_->CreateQueryHeap(
                  &desc, __uuidof(ID3D12QueryHeap),
                  reinterpret_cast<void **>(&heap)),
              E_NOTIMPL);
    EXPECT_EQ(heap, nullptr);
    release_object(heap);
  }

  for (D3D12_QUERY_HEAP_TYPE type : {D3D12_QUERY_HEAP_TYPE_OCCLUSION,
                                     D3D12_QUERY_HEAP_TYPE_TIMESTAMP}) {
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

TEST_F(D3D12DeviceSpec, RejectsStateChangingIndirectSignatures) {
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
            E_NOTIMPL);
  EXPECT_EQ(signature, nullptr);
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

TEST(D3D12RootSignatureSpec, AcceptsTheFull64DwordRootConstantBudget) {
  D3D12_ROOT_PARAMETER parameter = {};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.Num32BitValues = D3D12_MAX_ROOT_COST;
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 1;
  desc.pParameters = &parameter;

  ID3DBlob *blob = nullptr;
  ID3DBlob *error = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error);
  EXPECT_EQ(hr, S_OK);
  EXPECT_NE(blob, nullptr);
  release_object(blob);
  release_object(error);

  parameter.Constants.Num32BitValues = D3D12_MAX_ROOT_COST + 1;
  hr = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &blob, &error);
  EXPECT_EQ(hr, E_INVALIDARG);
  EXPECT_EQ(blob, nullptr);
  release_object(error);
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
  EXPECT_EQ(feature.SupportFlags, D3D12_SHADER_CACHE_SUPPORT_NONE);

#ifdef __ID3D12Device1_INTERFACE_DEFINED__
  ID3D12Device1 *device1 = nullptr;
  ASSERT_EQ(device_->QueryInterface(__uuidof(ID3D12Device1),
                                    reinterpret_cast<void **>(&device1)),
            S_OK);
  ASSERT_NE(device1, nullptr);

  ID3D12PipelineLibrary *library = nullptr;
  EXPECT_EQ(device1->CreatePipelineLibrary(
                nullptr, 0, __uuidof(ID3D12PipelineLibrary),
                reinterpret_cast<void **>(&library)),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(library, nullptr);

  const std::array<std::uint8_t, 8> invalid_blob = {
      0x44, 0x58, 0x4d, 0x54, 0xde, 0xad, 0xbe, 0xef};
  EXPECT_EQ(device1->CreatePipelineLibrary(
                invalid_blob.data(), invalid_blob.size(),
                __uuidof(ID3D12PipelineLibrary),
                reinterpret_cast<void **>(&library)),
            DXGI_ERROR_UNSUPPORTED);
  EXPECT_EQ(library, nullptr);
  release_object(device1);
#endif
}

TEST_F(D3D12DeviceSpec, RejectsStreamOutputAtPipelineCreation) {
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
  EXPECT_EQ(device_->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline)),
            E_NOTIMPL);
  EXPECT_EQ(pipeline, nullptr);

  desc.StreamOutput.pSODeclaration = nullptr;
  EXPECT_EQ(device_->CreateGraphicsPipelineState(
                &desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline)),
            E_INVALIDARG);
  EXPECT_EQ(pipeline, nullptr);
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

#ifdef __ID3D12Device2_INTERFACE_DEFINED__
TEST_F(D3D12DeviceSpec, RejectsPipelineStreamMixingComputeAndPixelShaders) {
  ID3D12Device2 *device2 = nullptr;
  ASSERT_EQ(device_->QueryInterface(__uuidof(ID3D12Device2),
                                    reinterpret_cast<void **>(&device2)),
            S_OK);
  ASSERT_NE(device2, nullptr);

  struct PipelineStream {
    ShaderPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS> compute;
    ShaderPipelineSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> pixel;
  } stream;
  stream.compute.shader = dxmt::test::CopyTextureComputeShader();
  stream.pixel.shader = dxmt::test::TextureUavPixelShader();
  D3D12_PIPELINE_STATE_STREAM_DESC stream_desc = {sizeof(stream), &stream};

  ID3D12PipelineState *pipeline = nullptr;
  EXPECT_EQ(device2->CreatePipelineState(
                &stream_desc, __uuidof(ID3D12PipelineState),
                reinterpret_cast<void **>(&pipeline)),
            E_INVALIDARG);
  EXPECT_EQ(pipeline, nullptr);
  release_object(device2);
}
#endif
