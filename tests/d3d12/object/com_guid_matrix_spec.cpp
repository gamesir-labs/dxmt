#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <string>
#include <vector>

// Public D3D12 QueryInterface / SetName matrix over common IIDs and names.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

struct GuidCase {
  const GUID *iid;
  bool expect_ok_on_device;
  const char *name;
};

// Use __uuidof to avoid depending on IID_* symbols that mingw may not export.
#define QI_CASE(Type, ok)                                                      \
  GuidCase { &__uuidof(Type), ok, #Type }

std::vector<GuidCase> BuildGuidCases() {
  return {
      QI_CASE(ID3D12Device, true),
      QI_CASE(ID3D12Object, true),
      QI_CASE(ID3D12DeviceChild, false),
      QI_CASE(IUnknown, true),
      QI_CASE(ID3D12CommandQueue, false),
      QI_CASE(ID3D12Fence, false),
      QI_CASE(ID3D12Resource, false),
      QI_CASE(ID3D12DescriptorHeap, false),
      QI_CASE(ID3D12RootSignature, false),
      QI_CASE(ID3D12PipelineState, false),
      QI_CASE(ID3D12CommandAllocator, false),
      QI_CASE(ID3D12GraphicsCommandList, false),
      QI_CASE(ID3D12QueryHeap, false),
      QI_CASE(ID3D12Heap, false),
      QI_CASE(ID3D12Device1, true),
      QI_CASE(ID3D12Device2, true),
      QI_CASE(ID3D12Device3, true),
      QI_CASE(ID3D12Device4, true),
      QI_CASE(ID3D12Device5, true),
  };
}

class DeviceQueryInterfaceMatrixSpec
    : public ::testing::TestWithParam<GuidCase> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(DeviceQueryInterfaceMatrixSpec, QueryInterfaceMatchesExpectation) {
  const auto &test = GetParam();
  IUnknown *object = nullptr;
  const HRESULT hr = context_.device()->QueryInterface(
      *test.iid, reinterpret_cast<void **>(&object));
  if (test.expect_ok_on_device) {
    ASSERT_EQ(hr, S_OK) << test.name;
    ASSERT_NE(object, nullptr);
    object->Release();
  } else {
    EXPECT_HRESULT_FAILED(hr) << test.name;
    EXPECT_EQ(object, nullptr);
  }
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

INSTANTIATE_TEST_SUITE_P(
    DeviceQiMatrix, DeviceQueryInterfaceMatrixSpec,
    ::testing::ValuesIn(BuildGuidCases()),
    [](const ::testing::TestParamInfo<GuidCase> &info) {
      std::string name = info.param.name;
      for (char &c : name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
          c = '_';
      }
      return name;
    });

class ObjectNameMatrixSpec : public ::testing::TestWithParam<std::wstring> {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_P(ObjectNameMatrixSpec, DeviceSetNameRoundTrips) {
  const std::wstring &name = GetParam();
  ASSERT_EQ(context_.device()->SetName(name.c_str()), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

std::vector<std::wstring> BuildNames() {
  std::vector<std::wstring> names;
  names.emplace_back(L"");
  names.emplace_back(L"a");
  names.emplace_back(L"dxmt-test");
  names.emplace_back(L"name with spaces and 123");
  for (int i = 0; i < 128; ++i)
    names.emplace_back(std::wstring(static_cast<size_t>(i + 1), L'x'));
  return names;
}

INSTANTIATE_TEST_SUITE_P(NameMatrix, ObjectNameMatrixSpec,
                         ::testing::ValuesIn(BuildNames()),
                         [](const ::testing::TestParamInfo<std::wstring> &info) {
                           return "Len" + std::to_string(info.param.size()) +
                                  "N" + std::to_string(info.index);
                         });

} // namespace
