#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Public-D3D11 output-merger binding-state coverage. Independent pools of 64
// RTVs and 64 UAVs form 4096 pairs, while each case varies the sparse RTV slot
// and the adjacent legal pixel-UAV slot.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kOutputMergerBindingCaseCount = 4096;
constexpr std::uint32_t kViewPoolSize = 64;
constexpr UINT kRtvSlotCount = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
constexpr UINT kUavSlotCount = D3D11_PS_CS_UAV_REGISTER_COUNT;

const dxmt::test::LogicalCaseFamilyRegistration kOutputMergerBindingCases(
    "D3D11OutputMergerBindingMatrixSpec."
    "RoundTrips4096RenderTargetAndUavPairs",
    "D3D11.OMGetRenderTargetsAndUAVs.Binding.", kOutputMergerBindingCaseCount,
    4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Immediate",
      "OMSetRenderTargetsAndUnorderedAccessViews,"
      "OMGetRenderTargetsAndUnorderedAccessViews,ComReferenceState"},
     dxmt::test::kResourceTestCost,
     "sixty-four test-local render-target textures and views plus sixty-four "
     "test-local structured buffers and unordered-access views",
     "bind every selected RTV/UAV identity pair at a varying sparse RTV slot "
     "and its adjacent legal pixel-UAV slot, query every RTV and the legal UAV "
     "range, release getter references, then unbind all output views",
     "only the selected RTV and UAV occupy their expected output slots, no DSV "
     "is returned, and all output slots are null after unbinding",
     "logical ID, selected-case count, view indexes, RTV and UAV slots, "
     "failing phase and slot, expected and actual addresses, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration
    kOutputMergerBindingCost("D3D11OutputMergerBindingMatrixSpec."
                             "RoundTrips4096RenderTargetAndUavPairs",
                             dxmt::test::kResourceTestCost);

struct OutputBinding {
  std::uint32_t rtv_index;
  std::uint32_t uav_index;
  UINT num_rtvs;
  UINT rtv_slot;
  UINT uav_slot;
};

OutputBinding BindingForCase(std::uint32_t logical) {
  const std::uint32_t rtv_index = logical & 63u;
  const std::uint32_t uav_index = (logical >> 6u) & 63u;
  const UINT num_rtvs =
      1u + static_cast<UINT>((rtv_index * 3u + uav_index * 5u) % 7u);
  const UINT rtv_slot = static_cast<UINT>((rtv_index ^ uav_index) % num_rtvs);
  return {rtv_index, uav_index, num_rtvs, rtv_slot, num_rtvs};
}

class D3D11OutputMergerBindingMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11OutputMergerBindingMatrixSpec,
       RoundTrips4096RenderTargetAndUavPairs) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kOutputMergerBindingCaseCount);
  for (std::uint32_t logical = 0; logical < kOutputMergerBindingCaseCount;
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kOutputMergerBindingCases.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  D3D11_TEXTURE2D_DESC texture_desc = {};
  texture_desc.Width = 1;
  texture_desc.Height = 1;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;

  std::vector<ComPtr<ID3D11Texture2D>> textures(kViewPoolSize);
  std::vector<ComPtr<ID3D11RenderTargetView>> rtvs(kViewPoolSize);
  for (std::uint32_t index = 0; index < kViewPoolSize; ++index) {
    ASSERT_EQ(context_.device()->CreateTexture2D(&texture_desc, nullptr,
                                                 textures[index].put()),
              S_OK)
        << "rtv_texture_index=" << index;
    ASSERT_EQ(context_.device()->CreateRenderTargetView(
                  textures[index].get(), nullptr, rtvs[index].put()),
              S_OK)
        << "rtv_index=" << index;
  }

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = 4u * sizeof(std::uint32_t);
  buffer_desc.Usage = D3D11_USAGE_DEFAULT;
  buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
  buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
  buffer_desc.StructureByteStride = sizeof(std::uint32_t);
  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
  uav_desc.Format = DXGI_FORMAT_UNKNOWN;
  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
  uav_desc.Buffer.NumElements = 4;

  std::vector<ComPtr<ID3D11Buffer>> buffers(kViewPoolSize);
  std::vector<ComPtr<ID3D11UnorderedAccessView>> uavs(kViewPoolSize);
  for (std::uint32_t index = 0; index < kViewPoolSize; ++index) {
    ASSERT_EQ(context_.device()->CreateBuffer(&buffer_desc, nullptr,
                                              buffers[index].put()),
              S_OK)
        << "uav_buffer_index=" << index;
    ASSERT_EQ(context_.device()->CreateUnorderedAccessView(
                  buffers[index].get(), &uav_desc, uavs[index].put()),
              S_OK)
        << "uav_index=" << index;
  }

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kOutputMergerBindingCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const OutputBinding binding = BindingForCase(logical);
    std::array<ID3D11RenderTargetView *, kRtvSlotCount> bound_rtvs = {};
    bound_rtvs[binding.rtv_slot] = rtvs[binding.rtv_index].get();
    ID3D11UnorderedAccessView *bound_uav = uavs[binding.uav_index].get();
    context_.context()->OMSetRenderTargetsAndUnorderedAccessViews(
        binding.num_rtvs, bound_rtvs.data(), nullptr, binding.uav_slot, 1,
        &bound_uav, nullptr);

    const char *failing_phase = "none";
    bool failed = false;
    UINT failing_slot = kRtvSlotCount;
    const void *expected_address = nullptr;
    const void *actual_address = nullptr;

    std::array<ID3D11RenderTargetView *, kRtvSlotCount> queried_rtvs = {};
    std::array<ID3D11UnorderedAccessView *, kUavSlotCount> queried_uavs = {};
    ID3D11DepthStencilView *queried_dsv = nullptr;
    context_.context()->OMGetRenderTargetsAndUnorderedAccessViews(
        kRtvSlotCount, queried_rtvs.data(), &queried_dsv, binding.uav_slot,
        kUavSlotCount - binding.uav_slot, queried_uavs.data());

    for (UINT slot = 0; slot < kRtvSlotCount; ++slot) {
      ID3D11RenderTargetView *expected =
          slot == binding.rtv_slot ? rtvs[binding.rtv_index].get() : nullptr;
      if (queried_rtvs[slot] != expected && !failed) {
        failed = true;
        failing_phase = "rtv_get";
        failing_slot = slot;
        expected_address = expected;
        actual_address = queried_rtvs[slot];
      }
      if (queried_rtvs[slot])
        queried_rtvs[slot]->Release();
    }
    if (queried_dsv && !failed) {
      failed = true;
      failing_phase = "dsv_get";
      actual_address = queried_dsv;
    }
    if (queried_dsv)
      queried_dsv->Release();

    for (UINT index = 0; index < kUavSlotCount - binding.uav_slot; ++index) {
      ID3D11UnorderedAccessView *expected =
          index == 0 ? uavs[binding.uav_index].get() : nullptr;
      if (queried_uavs[index] != expected && !failed) {
        failed = true;
        failing_phase = "uav_get";
        failing_slot = binding.uav_slot + index;
        expected_address = expected;
        actual_address = queried_uavs[index];
      }
      if (queried_uavs[index])
        queried_uavs[index]->Release();
    }

    std::array<ID3D11UnorderedAccessView *, kUavSlotCount> null_uavs = {};
    context_.context()->OMSetRenderTargetsAndUnorderedAccessViews(
        0, nullptr, nullptr, 0, kUavSlotCount, null_uavs.data(), nullptr);

    queried_rtvs = {};
    queried_uavs = {};
    queried_dsv = nullptr;
    context_.context()->OMGetRenderTargetsAndUnorderedAccessViews(
        kRtvSlotCount, queried_rtvs.data(), &queried_dsv, 0, kUavSlotCount,
        queried_uavs.data());
    for (UINT slot = 0; slot < kRtvSlotCount; ++slot) {
      if (queried_rtvs[slot] && !failed) {
        failed = true;
        failing_phase = "rtv_unbind";
        failing_slot = slot;
        actual_address = queried_rtvs[slot];
      }
      if (queried_rtvs[slot])
        queried_rtvs[slot]->Release();
    }
    if (queried_dsv && !failed) {
      failed = true;
      failing_phase = "dsv_unbind";
      actual_address = queried_dsv;
    }
    if (queried_dsv)
      queried_dsv->Release();
    for (UINT slot = 0; slot < kUavSlotCount; ++slot) {
      if (queried_uavs[slot] && !failed) {
        failed = true;
        failing_phase = "uav_unbind";
        failing_slot = slot;
        actual_address = queried_uavs[slot];
      }
      if (queried_uavs[slot])
        queried_uavs[slot]->Release();
    }

    if (!failed)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kOutputMergerBindingCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kOutputMergerBindingCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None "
           "queue=Immediate "
           "capability=OMSetRenderTargetsAndUnorderedAccessViews,"
           "OMGetRenderTargetsAndUnorderedAccessViews,ComReferenceState\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kOutputMergerBindingCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " rtv_index=" << binding.rtv_index
        << " uav_index=" << binding.uav_index
        << " num_rtvs=" << binding.num_rtvs << " rtv_slot=" << binding.rtv_slot
        << " uav_slot=" << binding.uav_slot
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Observed: phase=" << failing_phase << " slot=" << failing_slot
        << " expected_view=" << expected_address
        << " actual_view=" << actual_address << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  std::array<ID3D11UnorderedAccessView *, kUavSlotCount> null_uavs = {};
  context_.context()->OMSetRenderTargetsAndUnorderedAccessViews(
      0, nullptr, nullptr, 0, kUavSlotCount, null_uavs.data(), nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
