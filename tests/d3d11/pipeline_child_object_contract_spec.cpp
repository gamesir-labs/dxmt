#include <dxmt_test.hpp>
#include <dxmt_test_shader.hpp>

#include "d3d11_test_context.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

// Public D3D11 device-child coverage for programmable pipeline objects. Shader
// compilation and object creation are test-local and parallel-safe.

namespace {

using dxmt::test::CompileShader;
using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kPipelineChildPrivateDataKey = {
    0x913752f8,
    0x3c64,
    0x48ca,
    {0xab, 0x11, 0x72, 0x5e, 0xcf, 0x46, 0x05, 0xb9}};

enum class PipelineChildKind {
  InputLayout,
  VertexShader,
  PixelShader,
  GeometryShader,
  GeometryShaderWithStreamOutput,
  HullShader,
  DomainShader,
  ComputeShader,
};

struct PipelineChildCase {
  PipelineChildKind kind;
  const GUID *iid;
  const char *target;
  const char *name;
};

constexpr std::array kPipelineChildCases = {
    PipelineChildCase{PipelineChildKind::InputLayout,
                      &__uuidof(ID3D11InputLayout), "vs_5_0", "InputLayout"},
    PipelineChildCase{PipelineChildKind::VertexShader,
                      &__uuidof(ID3D11VertexShader), "vs_5_0", "VertexShader"},
    PipelineChildCase{PipelineChildKind::PixelShader,
                      &__uuidof(ID3D11PixelShader), "ps_5_0", "PixelShader"},
    PipelineChildCase{PipelineChildKind::GeometryShader,
                      &__uuidof(ID3D11GeometryShader), "gs_5_0",
                      "GeometryShader"},
    PipelineChildCase{PipelineChildKind::GeometryShaderWithStreamOutput,
                      &__uuidof(ID3D11GeometryShader), "gs_5_0",
                      "GeometryShaderWithStreamOutput"},
    PipelineChildCase{PipelineChildKind::HullShader,
                      &__uuidof(ID3D11HullShader), "hs_5_0", "HullShader"},
    PipelineChildCase{PipelineChildKind::DomainShader,
                      &__uuidof(ID3D11DomainShader), "ds_5_0", "DomainShader"},
    PipelineChildCase{PipelineChildKind::ComputeShader,
                      &__uuidof(ID3D11ComputeShader), "cs_5_0",
                      "ComputeShader"},
};

const dxmt::test::LogicalCaseFamilyRegistration kPipelineChildRegistration(
    "D3D11PipelineChildObjectContractSpec.PreservesPublicObjectContracts",
    "D3D11.PipelineChild.Object.", kPipelineChildCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "5_0", "Device",
      "D3DCompile,CreateInputLayout,CreateVertexShader,CreatePixelShader,"
      "CreateGeometryShader,CreateGeometryShaderWithStreamOutput,"
      "CreateHullShader,CreateDomainShader,CreateComputeShader,"
      "ID3D11DeviceChild,QueryInterface,ComObjectIdentity,GetDevice,"
      "SetPrivateData,GetPrivateData"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device, one compiled shader blob, and one live "
     "pipeline child object per selected logical case",
     "create an input layout, all six programmable shader stage objects, and "
     "a stream-output geometry shader through public D3D11 APIs",
     "each object preserves its concrete and device-child COM identity, "
     "returns its creating device, rejects unrelated interfaces, and "
     "round-trips then deletes private data",
     "logical ID, selected-case count, object kind, compile and creation "
     "HRESULTs, identities, owner, private-data values, and exact replay "
     "argument"});

const dxmt::test::TestCostRegistration kPipelineChildCost(
    "D3D11PipelineChildObjectContractSpec.PreservesPublicObjectContracts",
    dxmt::test::kResourceTestCost);

std::string_view ShaderSource(PipelineChildKind kind) {
  switch (kind) {
  case PipelineChildKind::InputLayout:
  case PipelineChildKind::VertexShader:
    return "float4 main(float4 position : POSITION) : SV_Position { return "
           "position; }";
  case PipelineChildKind::PixelShader:
    return "float4 main() : SV_Target { return float4(0.25, 0.5, 0.75, 1.0); "
           "}";
  case PipelineChildKind::GeometryShader:
  case PipelineChildKind::GeometryShaderWithStreamOutput:
    return R"(
struct Vertex { float4 position : SV_Position; };
[maxvertexcount(1)]
void main(point Vertex input[1], inout PointStream<Vertex> stream) {
  stream.Append(input[0]);
}
)";
  case PipelineChildKind::HullShader:
    return R"(
struct ControlPoint { float4 position : POSITION; };
struct PatchConstants {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};
PatchConstants patch_constants(InputPatch<ControlPoint, 3> patch) {
  PatchConstants output;
  output.edges[0] = 1.0;
  output.edges[1] = 1.0;
  output.edges[2] = 1.0;
  output.inside = 1.0;
  return output;
}
[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("patch_constants")]
ControlPoint main(InputPatch<ControlPoint, 3> patch,
                  uint id : SV_OutputControlPointID) {
  return patch[id];
}
)";
  case PipelineChildKind::DomainShader:
    return R"(
struct ControlPoint { float4 position : POSITION; };
struct PatchConstants {
  float edges[3] : SV_TessFactor;
  float inside : SV_InsideTessFactor;
};
[domain("tri")]
float4 main(PatchConstants constants, float3 location : SV_DomainLocation,
            const OutputPatch<ControlPoint, 3> patch) : SV_Position {
  return patch[0].position * location.x
       + patch[1].position * location.y
       + patch[2].position * location.z;
}
)";
  case PipelineChildKind::ComputeShader:
    return "[numthreads(1, 1, 1)] void main(uint3 id : "
           "SV_DispatchThreadID) {}";
  }
  return {};
}

template <typename T>
HRESULT QueryAsDeviceChild(HRESULT create_result, ComPtr<T> &object,
                           ID3D11DeviceChild **child) {
  if (FAILED(create_result) || !object)
    return FAILED(create_result) ? create_result : E_UNEXPECTED;
  return object->QueryInterface(__uuidof(ID3D11DeviceChild),
                                reinterpret_cast<void **>(child));
}

HRESULT CreatePipelineChild(ID3D11Device *device, PipelineChildKind kind,
                            ID3DBlob *bytecode, ID3D11DeviceChild **child) {
  if (!device || !bytecode || !child)
    return E_INVALIDARG;
  *child = nullptr;
  const void *code = bytecode->GetBufferPointer();
  const SIZE_T size = bytecode->GetBufferSize();

  switch (kind) {
  case PipelineChildKind::InputLayout: {
    const D3D11_INPUT_ELEMENT_DESC element = {
        "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
        0,          0, D3D11_INPUT_PER_VERTEX_DATA,
        0};
    ComPtr<ID3D11InputLayout> object;
    const HRESULT hr =
        device->CreateInputLayout(&element, 1, code, size, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  case PipelineChildKind::VertexShader: {
    ComPtr<ID3D11VertexShader> object;
    const HRESULT hr =
        device->CreateVertexShader(code, size, nullptr, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  case PipelineChildKind::PixelShader: {
    ComPtr<ID3D11PixelShader> object;
    const HRESULT hr =
        device->CreatePixelShader(code, size, nullptr, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  case PipelineChildKind::GeometryShader: {
    ComPtr<ID3D11GeometryShader> object;
    const HRESULT hr =
        device->CreateGeometryShader(code, size, nullptr, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  case PipelineChildKind::GeometryShaderWithStreamOutput: {
    const D3D11_SO_DECLARATION_ENTRY declaration = {
        0, "SV_Position", 0, 0, 4, 0};
    constexpr UINT stride = sizeof(FLOAT) * 4;
    ComPtr<ID3D11GeometryShader> object;
    const HRESULT hr = device->CreateGeometryShaderWithStreamOutput(
        code, size, &declaration, 1, &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM,
        nullptr, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  case PipelineChildKind::HullShader: {
    ComPtr<ID3D11HullShader> object;
    const HRESULT hr =
        device->CreateHullShader(code, size, nullptr, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  case PipelineChildKind::DomainShader: {
    ComPtr<ID3D11DomainShader> object;
    const HRESULT hr =
        device->CreateDomainShader(code, size, nullptr, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  case PipelineChildKind::ComputeShader: {
    ComPtr<ID3D11ComputeShader> object;
    const HRESULT hr =
        device->CreateComputeShader(code, size, nullptr, object.put());
    return QueryAsDeviceChild(hr, object, child);
  }
  }
  return E_INVALIDARG;
}

class D3D11PipelineChildObjectContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11PipelineChildObjectContractSpec, PreservesPublicObjectContracts) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kPipelineChildCases.size();
       ++logical) {
    if (dxmt::test::LogicalCaseSelected(kPipelineChildRegistration.family(),
                                        logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kPipelineChildRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const auto &test_case = kPipelineChildCases[logical];
    const auto compilation =
        CompileShader(ShaderSource(test_case.kind), test_case.target);
    ASSERT_EQ(compilation.result, S_OK)
        << "object=" << test_case.name << ' ' << compilation.diagnostic_text();

    ComPtr<ID3D11DeviceChild> child;
    const HRESULT create_result =
        CreatePipelineChild(context_.device(), test_case.kind,
                            compilation.bytecode.get(), child.put());
    ComPtr<IUnknown> canonical_identity;
    ComPtr<IUnknown> concrete;
    ComPtr<IUnknown> concrete_identity;
    ComPtr<ID3D11Device> owner;
    HRESULT identity_result = E_FAIL;
    HRESULT concrete_result = E_FAIL;
    HRESULT concrete_identity_result = E_FAIL;
    HRESULT unrelated_result = E_FAIL;
    HRESULT null_result = E_FAIL;
    void *unrelated_output =
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));

    constexpr std::array<std::uint8_t, 5> kPrivateValue = {3, 1, 4, 1, 5};
    std::array<std::uint8_t, kPrivateValue.size()> returned_value = {};
    UINT returned_size = returned_value.size();
    HRESULT set_result = E_FAIL;
    HRESULT get_result = E_FAIL;
    HRESULT delete_result = E_FAIL;
    HRESULT missing_result = E_FAIL;
    bool private_value_matches = false;

    if (create_result == S_OK && child) {
      child->GetDevice(owner.put());
      identity_result = child->QueryInterface(
          __uuidof(IUnknown),
          reinterpret_cast<void **>(canonical_identity.put()));
      concrete_result = child->QueryInterface(
          *test_case.iid, reinterpret_cast<void **>(concrete.put()));
      if (concrete_result == S_OK && concrete) {
        concrete_identity_result = concrete->QueryInterface(
            __uuidof(IUnknown),
            reinterpret_cast<void **>(concrete_identity.put()));
      }
      unrelated_result =
          child->QueryInterface(__uuidof(ID3D11Resource), &unrelated_output);
      null_result = child->QueryInterface(__uuidof(IUnknown), nullptr);
      set_result =
          child->SetPrivateData(kPipelineChildPrivateDataKey,
                                kPrivateValue.size(), kPrivateValue.data());
      get_result = child->GetPrivateData(kPipelineChildPrivateDataKey,
                                         &returned_size, returned_value.data());
      private_value_matches = returned_size == returned_value.size() &&
                              returned_value == kPrivateValue;
      delete_result =
          child->SetPrivateData(kPipelineChildPrivateDataKey, 0, nullptr);
      returned_size = returned_value.size();
      missing_result = child->GetPrivateData(
          kPipelineChildPrivateDataKey, &returned_size, returned_value.data());
    }

    const bool valid =
        create_result == S_OK && child && identity_result == S_OK &&
        canonical_identity && concrete_result == S_OK && concrete &&
        concrete_identity_result == S_OK &&
        concrete_identity.get() == canonical_identity.get() &&
        owner.get() == context_.device() && unrelated_result == E_NOINTERFACE &&
        unrelated_output == nullptr && null_result == E_POINTER &&
        set_result == S_OK && get_result == S_OK && private_value_matches &&
        delete_result == S_OK && missing_result == DXGI_ERROR_NOT_FOUND;
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kPipelineChildRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " object=" << test_case.name
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " identity_hresult=" << S_OK
                  << " concrete_hresult=" << S_OK
                  << " unrelated_hresult=" << E_NOINTERFACE
                  << " null_hresult=" << E_POINTER
                  << " owner=" << context_.device() << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " identity_hresult=" << identity_result
                  << " concrete_hresult=" << concrete_result
                  << " concrete_identity_hresult=" << concrete_identity_result
                  << " unrelated_hresult=" << unrelated_result
                  << " unrelated_output=" << unrelated_output
                  << " null_hresult=" << null_result
                  << " private_data_hresults=(" << set_result << ','
                  << get_result << ',' << delete_result << ',' << missing_result
                  << ") child=" << child.get()
                  << " canonical_identity=" << canonical_identity.get()
                  << " concrete_identity=" << concrete_identity.get()
                  << " owner=" << owner.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
