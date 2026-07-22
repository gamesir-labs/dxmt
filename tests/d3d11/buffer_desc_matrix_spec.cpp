#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <cstdint>
#include <vector>

// Public D3D11 buffer creation and descriptor coverage. Constant-buffer sizes
// from 16 bytes through the classic 64 KiB limit form exactly 4096 cases.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr std::uint32_t kBufferDescCaseCount = 4096;

const dxmt::test::LogicalCaseFamilyRegistration kBufferDescCases(
    "D3D11BufferDescMatrixSpec.RoundTrips4096ConstantBufferSizes",
    "D3D11.Buffer.Description.", kBufferDescCaseCount, 4,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "CreateBuffer,ID3D11BufferGetDesc,ID3D11ResourceGetType,"
      "ID3D11DeviceChildGetDevice"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live constant buffer per selected "
     "logical case",
     "create every 16-byte-aligned constant-buffer size from 16 through "
     "65536 bytes and query its complete public resource description, type, "
     "and owning device",
     "creation succeeds and public queries preserve every requested field, "
     "report buffer resource type, and return the creating device",
     "logical ID, selected-case count, requested and returned descriptions, "
     "resource type, owner and device addresses, HRESULT, failure phase, and "
     "exact replay argument"});

const dxmt::test::TestCostRegistration kBufferDescCost(
    "D3D11BufferDescMatrixSpec.RoundTrips4096ConstantBufferSizes",
    dxmt::test::kResourceTestCost);

bool BufferDescsEqual(const D3D11_BUFFER_DESC &actual,
                      const D3D11_BUFFER_DESC &expected) {
  return actual.ByteWidth == expected.ByteWidth &&
         actual.Usage == expected.Usage &&
         actual.BindFlags == expected.BindFlags &&
         actual.CPUAccessFlags == expected.CPUAccessFlags &&
         actual.MiscFlags == expected.MiscFlags &&
         actual.StructureByteStride == expected.StructureByteStride;
}

class D3D11BufferDescMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
    ASSERT_NE(context_.context(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11BufferDescMatrixSpec, RoundTrips4096ConstantBufferSizes) {
  std::vector<std::uint32_t> selected_cases;
  selected_cases.reserve(kBufferDescCaseCount);
  for (std::uint32_t logical = 0; logical < kBufferDescCaseCount; ++logical) {
    if (dxmt::test::LogicalCaseSelected(kBufferDescCases.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kBufferDescCases.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    D3D11_BUFFER_DESC expected = {};
    expected.ByteWidth = (logical + 1u) * 16u;
    expected.Usage = D3D11_USAGE_DEFAULT;
    expected.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    ComPtr<ID3D11Buffer> buffer;
    const HRESULT create_result =
        context_.device()->CreateBuffer(&expected, nullptr, buffer.put());
    D3D11_BUFFER_DESC actual = {};
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    ComPtr<ID3D11Device> owner;
    if (create_result == S_OK && buffer) {
      buffer->GetDesc(&actual);
      buffer->GetType(&dimension);
      buffer->GetDevice(owner.put());
    }

    const bool desc_matches = BufferDescsEqual(actual, expected);
    const bool type_matches = dimension == D3D11_RESOURCE_DIMENSION_BUFFER;
    const bool owner_matches = owner.get() == context_.device();
    if (create_result == S_OK && buffer && desc_matches && type_matches &&
        owner_matches)
      continue;

    const char *failure_phase = "owner";
    if (create_result != S_OK || !buffer)
      failure_phase = "create";
    else if (!desc_matches)
      failure_phase = "get_desc";
    else if (!type_matches)
      failure_phase = "get_type";

    const auto case_id =
        dxmt::test::LogicalCaseId(kBufferDescCases.family(), logical);
    ADD_FAILURE()
        << "LogicalCaseId: " << case_id << '\n'
        << "Class: "
        << dxmt::test::TestClassName(
               kBufferDescCases.family().traits.test_class)
        << '\n'
        << "Requirements: feature_level=11_0 shader_model=None queue=Device "
           "capability=CreateBuffer,BufferGetDesc,ResourceGetType,"
           "DeviceChildGetDevice\n"
        << "ExecutionPath: "
        << dxmt::test::ExecutionPathName(
               kBufferDescCases.family().traits.execution_path)
        << '\n'
        << "Parameters: logical=" << logical
        << " selected_cases=" << selected_cases.size() << '\n'
        << "Expected: byte_width=" << expected.ByteWidth
        << " usage=" << static_cast<UINT>(expected.Usage)
        << " bind_flags=" << expected.BindFlags
        << " cpu_access=" << expected.CPUAccessFlags
        << " misc_flags=" << expected.MiscFlags
        << " stride=" << expected.StructureByteStride
        << " dimension=" << D3D11_RESOURCE_DIMENSION_BUFFER
        << " owner=" << context_.device() << '\n'
        << "Observed: create_hresult=" << create_result
        << " failure_phase=" << failure_phase
        << " byte_width=" << actual.ByteWidth
        << " usage=" << static_cast<UINT>(actual.Usage)
        << " bind_flags=" << actual.BindFlags
        << " cpu_access=" << actual.CPUAccessFlags
        << " misc_flags=" << actual.MiscFlags
        << " stride=" << actual.StructureByteStride
        << " dimension=" << static_cast<UINT>(dimension)
        << " owner=" << owner.get() << " buffer=" << buffer.get() << '\n'
        << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

TEST_F(D3D11BufferDescMatrixSpec,
       ValidatesNullAlignmentBindingAndImmutableContracts) {
  ID3D11Buffer *buffer =
      reinterpret_cast<ID3D11Buffer *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateBuffer(nullptr, nullptr, &buffer),
            E_INVALIDARG);
  EXPECT_EQ(buffer, nullptr);

  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = 16;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  EXPECT_EQ(context_.device()->CreateBuffer(&desc, nullptr, nullptr), S_FALSE);

  desc.ByteWidth = 15;
  buffer = reinterpret_cast<ID3D11Buffer *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateBuffer(&desc, nullptr, &buffer),
            E_INVALIDARG);
  EXPECT_EQ(buffer, nullptr);

  desc.ByteWidth = 16;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER | D3D11_BIND_VERTEX_BUFFER;
  buffer = reinterpret_cast<ID3D11Buffer *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateBuffer(&desc, nullptr, &buffer),
            E_INVALIDARG);
  EXPECT_EQ(buffer, nullptr);

  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  desc.Usage = D3D11_USAGE_IMMUTABLE;
  buffer = reinterpret_cast<ID3D11Buffer *>(static_cast<std::uintptr_t>(1));
  EXPECT_EQ(context_.device()->CreateBuffer(&desc, nullptr, &buffer),
            E_INVALIDARG);
  EXPECT_EQ(buffer, nullptr);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
