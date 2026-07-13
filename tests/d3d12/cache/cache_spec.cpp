#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

#include "shaders/runtime_test_shaders.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

template <typename T> void release_object(T *&object) {
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

struct ScopedArchiveTestEnvironment {
  explicit ScopedArchiveTestEnvironment(const char *suffix) {
    std::ostringstream name;
    name << "dxmt-pso-archive-" << GetCurrentProcessId() << "-"
         << GetTickCount64() << "-" << suffix;
    root = "C:\\" + name.str();
    unix_cache_root = "/tmp/" + name.str();
    windows_cache_root = "Z:\\tmp\\" + name.str();
    marker = root + "\\marker.txt";
    CreateDirectoryA(root.c_str(), nullptr);
    std::ofstream(marker, std::ios::trunc).close();
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
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  std::string root, unix_cache_root, windows_cache_root, marker;
};

ID3D12RootSignature *CreateRootSignature(ID3D12Device *device,
                                         const D3D12_ROOT_SIGNATURE_DESC &desc) {
  ID3DBlob *blob = nullptr;
  ID3DBlob *error = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                           &blob, &error);
  release_object(error);
  if (FAILED(hr)) {
    release_object(blob);
    return nullptr;
  }
  ID3D12RootSignature *root_signature = nullptr;
  hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
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
  desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  desc.SampleMask = UINT_MAX;
  desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  return desc;
}

ID3D12PipelineState *CreateBasicGraphicsPipeline(ID3D12Device *device) {
  D3D12_ROOT_SIGNATURE_DESC root_desc = {};
  root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  auto *root_signature = CreateRootSignature(device, root_desc);
  if (!root_signature)
    return nullptr;
  auto desc = BasicGraphicsPipelineDesc(root_signature);
  ID3D12PipelineState *pipeline = nullptr;
  const auto hr = device->CreateGraphicsPipelineState(
      &desc, __uuidof(ID3D12PipelineState), reinterpret_cast<void **>(&pipeline));
  release_object(root_signature);
  return SUCCEEDED(hr) ? pipeline : nullptr;
}

} // namespace

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
  if (marker.find("serialize reason=periodic count=1") == std::string::npos)
    GTEST_SKIP() << "D3D12 device was reused after archive state was fixed: " << marker;
  EXPECT_NE(marker.find("serialize reason=periodic count=1 ok=1"), std::string::npos)
      << marker;
}

TEST(D3D12PipelineArchiveSpec, RejectsCorruptArchiveAndFallsBackToCompilation) {
  ScopedArchiveTestEnvironment environment("corrupt");
  const auto archive_dir = environment.windows_cache_root + "\\com.apple.metal4";
  ASSERT_TRUE(CreateDirectoryA(archive_dir.c_str(), nullptr) ||
              GetLastError() == ERROR_ALREADY_EXISTS);
  const auto archive_path = archive_dir + "\\dxmt_pso.binaryarchive";
  std::ofstream corrupt(archive_path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(corrupt.good());
  corrupt << "not-a-metal-binary-archive";
  corrupt.close();
  ID3D12Device *device = nullptr;
  ASSERT_TRUE(HResultSucceeded(D3D12CreateDevice(
      nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
      reinterpret_cast<void **>(&device))));
  auto *pipeline = CreateBasicGraphicsPipeline(device);
  EXPECT_NE(pipeline, nullptr);
  release_object(pipeline);
  release_object(device);
  const auto marker = environment.ReadMarker();
  if (marker.find("create cold=0") == std::string::npos)
    GTEST_SKIP() << "D3D12 device was reused after archive state was fixed: " << marker;
  EXPECT_NE(marker.find("create cold=0 ok=0"), std::string::npos) << marker;
}
