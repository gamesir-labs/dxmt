#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d12_test_context.hpp"

#include <DXBCParser/d3d12tokenizedprogramformat.hpp>

#include <array>
#include <cstdint>
#include <cstring>

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;
using dxmt::test::TextureReadback;

class D3D12LodSemanticSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  ComPtr<ID3D12RootSignature> CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE texture = {};
    texture.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    texture.NumDescriptors = 1;
    D3D12_DESCRIPTOR_RANGE sampler = {};
    sampler.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    sampler.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &texture;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &sampler;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = parameters;
    return context_.CreateRootSignature(desc);
  }

  static bool PatchSampleToLod(ID3DBlob *bytecode) {
    // The Wine HLSL frontend does not expose CalculateLevelOfDetail, but DXBC
    // sample and lod instructions have the same four operands and token length.
    // Compile the former, then replace only its opcode to exercise lod lowering.
    constexpr std::uint32_t kShex =
        std::uint32_t('S') | (std::uint32_t('H') << 8) |
        (std::uint32_t('E') << 16) | (std::uint32_t('X') << 24);
    constexpr std::uint32_t kShdr =
        std::uint32_t('S') | (std::uint32_t('H') << 8) |
        (std::uint32_t('D') << 16) | (std::uint32_t('R') << 24);
    auto *bytes = static_cast<std::uint8_t *>(bytecode->GetBufferPointer());
    const auto size = bytecode->GetBufferSize();
    auto load32 = [&](std::size_t offset) {
      std::uint32_t value = 0;
      if (offset + sizeof(value) <= size)
        std::memcpy(&value, bytes + offset, sizeof(value));
      return value;
    };
    constexpr std::size_t kPartCountOffset = 28;
    constexpr std::size_t kPartTableOffset = 32;
    const auto part_count = load32(kPartCountOffset);
    if (kPartTableOffset + part_count * sizeof(std::uint32_t) > size)
      return false;
    for (std::uint32_t part = 0; part < part_count; ++part) {
      const auto part_offset =
          load32(kPartTableOffset + part * sizeof(std::uint32_t));
      if (part_offset + 2 * sizeof(std::uint32_t) > size)
        return false;
      const auto tag = load32(part_offset);
      if (tag != kShex && tag != kShdr)
        continue;
      const auto part_size = load32(part_offset + sizeof(std::uint32_t));
      const auto program_offset = part_offset + 2 * sizeof(std::uint32_t);
      if (program_offset + part_size > size ||
          part_size < 2 * sizeof(std::uint32_t))
        return false;
      const auto token_count = load32(program_offset + sizeof(std::uint32_t));
      if (token_count > part_size / sizeof(std::uint32_t))
        return false;
      for (std::uint32_t token_index = 2; token_index < token_count;) {
        const auto token_offset =
            program_offset + token_index * sizeof(std::uint32_t);
        auto token = load32(token_offset);
        const auto opcode = static_cast<microsoft::D3D10_SB_OPCODE_TYPE>(
            token & D3D10_SB_OPCODE_TYPE_MASK);
        const auto length = (token >> 24) & 0x7fu;
        if (!length || token_index + length > token_count)
          return false;
        if (opcode == microsoft::D3D10_SB_OPCODE_SAMPLE) {
          token = (token & ~D3D10_SB_OPCODE_TYPE_MASK) |
                  microsoft::D3D10_1_SB_OPCODE_LOD;
          std::memcpy(bytes + token_offset, &token, sizeof(token));
          return true;
        }
        token_index += length;
      }
    }
    return false;
  }

  std::array<float, 4> Run(const char *source, float min_lod,
                           float mip_lod_bias = 0.0f,
                           bool patch_sample_to_lod = false) {
    const auto shader = CompileShader(source, "ps_5_0");
    EXPECT_EQ(shader.result, S_OK) << shader.diagnostic_text();
    if (shader.result != S_OK)
      return {};
    if (patch_sample_to_lod) {
      const bool patched = PatchSampleToLod(shader.bytecode.get());
      EXPECT_TRUE(patched);
      if (!patched)
        return {};
    }
    auto root = CreateRootSignature();
    EXPECT_TRUE(root);
    if (!root)
      return {};
    const D3D12_SHADER_BYTECODE bytecode = {
        shader.bytecode->GetBufferPointer(), shader.bytecode->GetBufferSize()};
    auto pipeline = context_.CreateGraphicsPipeline(
        root.get(), DXGI_FORMAT_R32G32B32A32_FLOAT, bytecode);

    constexpr UINT kSize = 8;
    std::array<float, 64> mip0 = {};
    std::array<float, 16> mip1 = {};
    std::array<float, 4> mip2 = {};
    std::array<float, 1> mip3 = {};
    mip0.fill(1.0f);
    mip1.fill(2.0f);
    mip2.fill(4.0f);
    mip3.fill(8.0f);
    auto texture = context_.CreateTexture2D(
        kSize, kSize, 4, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST);
    auto target = context_.CreateTexture2D(
        kSize, kSize, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto textures = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
    auto samplers = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true);
    auto targets = context_.CreateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    EXPECT_TRUE(pipeline);
    EXPECT_TRUE(texture);
    EXPECT_TRUE(target);
    EXPECT_TRUE(textures);
    EXPECT_TRUE(samplers);
    EXPECT_TRUE(targets);
    if (!pipeline || !texture || !target || !textures || !samplers ||
        !targets)
      return {};

    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), mip0.data(),
                                             8 * sizeof(float), sizeof(mip0),
                                             0),
              S_OK);
    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), mip1.data(),
                                             4 * sizeof(float), sizeof(mip1),
                                             1),
              S_OK);
    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), mip2.data(),
                                             2 * sizeof(float), sizeof(mip2),
                                             2),
              S_OK);
    EXPECT_EQ(context_.UploadTextureAndReset(texture.get(), mip3.data(),
                                             sizeof(float), sizeof(mip3), 3),
              S_OK);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 4;
    context_.device()->CreateShaderResourceView(
        texture.get(), &srv, textures->GetCPUDescriptorHandleForHeapStart());
    D3D12_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MipLODBias = mip_lod_bias;
    sampler.MinLOD = min_lod;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    context_.device()->CreateSampler(
        &sampler, samplers->GetCPUDescriptorHandleForHeapStart());
    const auto rtv = targets->GetCPUDescriptorHandleForHeapStart();
    context_.device()->CreateRenderTargetView(target.get(), nullptr, rtv);

    D3D12TestContext::Transition(
        context_.list(), texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    ID3D12DescriptorHeap *heaps[] = {textures.get(), samplers.get()};
    context_.list()->SetDescriptorHeaps(2, heaps);
    context_.list()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    context_.list()->SetGraphicsRootSignature(root.get());
    context_.list()->SetPipelineState(pipeline.get());
    context_.list()->SetGraphicsRootDescriptorTable(
        0, textures->GetGPUDescriptorHandleForHeapStart());
    context_.list()->SetGraphicsRootDescriptorTable(
        1, samplers->GetGPUDescriptorHandleForHeapStart());
    context_.list()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT viewport = {0, 0, static_cast<FLOAT>(kSize),
                                     static_cast<FLOAT>(kSize), 0, 1};
    const D3D12_RECT scissor = {0, 0, kSize, kSize};
    context_.list()->RSSetViewports(1, &viewport);
    context_.list()->RSSetScissorRects(1, &scissor);
    context_.list()->DrawInstanced(3, 1, 0, 0);
    D3D12TestContext::Transition(
        context_.list(), target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    TextureReadback readback;
    EXPECT_EQ(context_.ReadbackTexture(target.get(), &readback), S_OK);
    if (readback.data.size() < 4 * sizeof(float))
      return {};
    std::array<float, 4> result = {};
    std::memcpy(result.data(), readback.data.data(), sizeof(result));
    return result;
  }

  D3D12TestContext context_;
};

TEST_F(D3D12LodSemanticSpec, SampleBiasSelectsBiasedMipLevel) {
  const auto values = Run(R"(
    Texture2D<float> input : register(t0);
    SamplerState input_sampler : register(s0);
    float4 main(float4 position : SV_Position) : SV_Target {
      float2 uv = position.xy / 8.0f;
      return float4(input.SampleLevel(input_sampler, uv, 0.0f),
                    input.SampleBias(input_sampler, uv, 2.0f), 0.0f, 1.0f);
    }
  )", 0.0f);
  EXPECT_NEAR(values[0], 1.0f, 1.0e-5f);
  EXPECT_NEAR(values[1], 4.0f, 1.0e-5f);
}

TEST_F(D3D12LodSemanticSpec, ImplicitSampleAppliesSamplerMipLodBias) {
  const auto values = Run(R"(
    Texture2D<float> input : register(t0);
    SamplerState input_sampler : register(s0);
    float4 main(float4 position : SV_Position) : SV_Target {
      float2 uv = position.xy / 8.0f;
      return float4(input.Sample(input_sampler, uv), 0.0f, 0.0f, 1.0f);
    }
  )", 0.0f, 2.0f);
  EXPECT_NEAR(values[0], 4.0f, 1.0e-5f);
}

TEST_F(D3D12LodSemanticSpec, CalculateLodReportsClampedAndUnclampedValues) {
  const auto values = Run(R"(
    Texture2D input : register(t0);
    SamplerState input_sampler : register(s0);
    float4 main(float4 position : SV_Position) : SV_Target {
      float2 uv = position.xy / 8.0f;
      return input.Sample(input_sampler, uv);
    }
  )", 1.0f, 0.0f, true);
  EXPECT_NEAR(values[0], 1.0f, 1.0e-5f);
  EXPECT_NEAR(values[1], 0.0f, 1.0e-5f);
}

} // namespace
