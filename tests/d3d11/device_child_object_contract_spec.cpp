#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#include <d3d11_4.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

// Public ID3D11DeviceChild coverage across resources, views, states, and
// asynchronous objects. Every object and private-data entry is test-local, so
// the matrix is safe under the default parallel scheduler.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

constexpr GUID kChildPrivateDataKey = {
    0xb2068462,
    0xb112,
    0x4f35,
    {0xa2, 0xe5, 0x91, 0x2b, 0x99, 0xf4, 0xb7, 0x63}};

enum class ChildKind {
  Buffer,
  Texture1D,
  Texture2D,
  Texture3D,
  ShaderResourceView,
  RenderTargetView,
  UnorderedAccessView,
  DepthStencilView,
  SamplerState,
  BlendState,
  DepthStencilState,
  RasterizerState,
  Query,
  Predicate,
};

struct ChildCase {
  ChildKind kind;
  const GUID *iid;
  const char *name;
};

constexpr std::array kChildCases = {
    ChildCase{ChildKind::Buffer, &__uuidof(ID3D11Buffer), "Buffer"},
    ChildCase{ChildKind::Texture1D, &__uuidof(ID3D11Texture1D), "Texture1D"},
    ChildCase{ChildKind::Texture2D, &__uuidof(ID3D11Texture2D), "Texture2D"},
    ChildCase{ChildKind::Texture3D, &__uuidof(ID3D11Texture3D), "Texture3D"},
    ChildCase{ChildKind::ShaderResourceView,
              &__uuidof(ID3D11ShaderResourceView), "ShaderResourceView"},
    ChildCase{ChildKind::RenderTargetView, &__uuidof(ID3D11RenderTargetView),
              "RenderTargetView"},
    ChildCase{ChildKind::UnorderedAccessView,
              &__uuidof(ID3D11UnorderedAccessView), "UnorderedAccessView"},
    ChildCase{ChildKind::DepthStencilView, &__uuidof(ID3D11DepthStencilView),
              "DepthStencilView"},
    ChildCase{ChildKind::SamplerState, &__uuidof(ID3D11SamplerState),
              "SamplerState"},
    ChildCase{ChildKind::BlendState, &__uuidof(ID3D11BlendState), "BlendState"},
    ChildCase{ChildKind::DepthStencilState, &__uuidof(ID3D11DepthStencilState),
              "DepthStencilState"},
    ChildCase{ChildKind::RasterizerState, &__uuidof(ID3D11RasterizerState),
              "RasterizerState"},
    ChildCase{ChildKind::Query, &__uuidof(ID3D11Query), "Query"},
    ChildCase{ChildKind::Predicate, &__uuidof(ID3D11Predicate), "Predicate"},
};

const dxmt::test::LogicalCaseFamilyRegistration kChildRegistration(
    "D3D11DeviceChildObjectContractSpec.PreservesPublicObjectContracts",
    "D3D11.DeviceChild.Object.", kChildCases.size(), 1,
    {dxmt::test::TestClass::Conformance,
     dxmt::test::ExecutionPath::Auto,
     {"11_0", "None", "Device",
      "ID3D11DeviceChild,ID3D11Resource,ID3D11View,ID3D11StateObject,"
      "ID3D11Asynchronous,QueryInterface,ComObjectIdentity,GetDevice,"
      "SetPrivateData,GetPrivateData"},
     dxmt::test::kResourceTestCost,
     "one test-local D3D11 device and one live resource, view, state, query, "
     "or predicate per selected logical case",
     "create fourteen representative public device-child object types and "
     "exercise their concrete interface, device-child identity, ownership, "
     "private data, and invalid QueryInterface paths",
     "every object preserves one canonical COM identity, returns its creating "
     "device, isolates and deletes private data, and clears unsupported QI "
     "outputs",
     "logical ID, selected-case count, object type, creation and QI HRESULTs, "
     "identities, owner, private-data values, and exact replay argument"});

const dxmt::test::TestCostRegistration kChildCost(
    "D3D11DeviceChildObjectContractSpec.PreservesPublicObjectContracts",
    dxmt::test::kResourceTestCost);

template <typename T>
HRESULT QueryAsChild(HRESULT create_result, ComPtr<T> &object,
                     ID3D11DeviceChild **child) {
  if (FAILED(create_result) || !object)
    return FAILED(create_result) ? create_result : E_UNEXPECTED;
  return object->QueryInterface(__uuidof(ID3D11DeviceChild),
                                reinterpret_cast<void **>(child));
}

HRESULT CreateTexture2D(ID3D11Device *device, DXGI_FORMAT format,
                        UINT bind_flags, ID3D11Texture2D **texture) {
  const D3D11_TEXTURE2D_DESC desc = {
      8, 8, 1, 1, format, {1, 0}, D3D11_USAGE_DEFAULT, bind_flags, 0, 0,
  };
  return device->CreateTexture2D(&desc, nullptr, texture);
}

HRESULT CreateChild(ID3D11Device *device, ChildKind kind,
                    ID3D11DeviceChild **child) {
  if (!device || !child)
    return E_INVALIDARG;
  *child = nullptr;

  switch (kind) {
  case ChildKind::Buffer: {
    const D3D11_BUFFER_DESC desc = {
        64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0,
    };
    ComPtr<ID3D11Buffer> object;
    const HRESULT result = device->CreateBuffer(&desc, nullptr, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::Texture1D: {
    const D3D11_TEXTURE1D_DESC desc = {
        8,
        1,
        1,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_SHADER_RESOURCE,
        0,
        0,
    };
    ComPtr<ID3D11Texture1D> object;
    const HRESULT result =
        device->CreateTexture1D(&desc, nullptr, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::Texture2D: {
    ComPtr<ID3D11Texture2D> object;
    const HRESULT result =
        CreateTexture2D(device, DXGI_FORMAT_R8G8B8A8_UNORM,
                        D3D11_BIND_SHADER_RESOURCE, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::Texture3D: {
    const D3D11_TEXTURE3D_DESC desc = {
        4,
        4,
        4,
        1,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_SHADER_RESOURCE,
        0,
        0,
    };
    ComPtr<ID3D11Texture3D> object;
    const HRESULT result =
        device->CreateTexture3D(&desc, nullptr, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::ShaderResourceView: {
    ComPtr<ID3D11Texture2D> texture;
    HRESULT result = CreateTexture2D(device, DXGI_FORMAT_R8G8B8A8_UNORM,
                                     D3D11_BIND_SHADER_RESOURCE, texture.put());
    if (FAILED(result))
      return result;
    ComPtr<ID3D11ShaderResourceView> object;
    result =
        device->CreateShaderResourceView(texture.get(), nullptr, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::RenderTargetView: {
    ComPtr<ID3D11Texture2D> texture;
    HRESULT result = CreateTexture2D(device, DXGI_FORMAT_R8G8B8A8_UNORM,
                                     D3D11_BIND_RENDER_TARGET, texture.put());
    if (FAILED(result))
      return result;
    ComPtr<ID3D11RenderTargetView> object;
    result =
        device->CreateRenderTargetView(texture.get(), nullptr, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::UnorderedAccessView: {
    ComPtr<ID3D11Texture2D> texture;
    HRESULT result =
        CreateTexture2D(device, DXGI_FORMAT_R8G8B8A8_UNORM,
                        D3D11_BIND_UNORDERED_ACCESS, texture.put());
    if (FAILED(result))
      return result;
    ComPtr<ID3D11UnorderedAccessView> object;
    result =
        device->CreateUnorderedAccessView(texture.get(), nullptr, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::DepthStencilView: {
    ComPtr<ID3D11Texture2D> texture;
    HRESULT result = CreateTexture2D(device, DXGI_FORMAT_D24_UNORM_S8_UINT,
                                     D3D11_BIND_DEPTH_STENCIL, texture.put());
    if (FAILED(result))
      return result;
    ComPtr<ID3D11DepthStencilView> object;
    result =
        device->CreateDepthStencilView(texture.get(), nullptr, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::SamplerState: {
    const D3D11_SAMPLER_DESC desc = {
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        0.0f,
        1,
        D3D11_COMPARISON_NEVER,
        {0.0f, 0.0f, 0.0f, 0.0f},
        0.0f,
        std::numeric_limits<FLOAT>::max(),
    };
    ComPtr<ID3D11SamplerState> object;
    const HRESULT result = device->CreateSamplerState(&desc, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::BlendState: {
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> object;
    const HRESULT result = device->CreateBlendState(&desc, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::DepthStencilState: {
    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = TRUE;
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    desc.DepthFunc = D3D11_COMPARISON_LESS;
    desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    ComPtr<ID3D11DepthStencilState> object;
    const HRESULT result = device->CreateDepthStencilState(&desc, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::RasterizerState: {
    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_BACK;
    desc.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> object;
    const HRESULT result = device->CreateRasterizerState(&desc, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::Query: {
    const D3D11_QUERY_DESC desc = {D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> object;
    const HRESULT result = device->CreateQuery(&desc, object.put());
    return QueryAsChild(result, object, child);
  }
  case ChildKind::Predicate: {
    const D3D11_QUERY_DESC desc = {D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    ComPtr<ID3D11Predicate> object;
    const HRESULT result = device->CreatePredicate(&desc, object.put());
    return QueryAsChild(result, object, child);
  }
  }
  return E_INVALIDARG;
}

class D3D11DeviceChildObjectContractSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(context_.Initialize(), S_OK);
    ASSERT_NE(context_.device(), nullptr);
  }

  D3D11TestContext context_;
};

TEST_F(D3D11DeviceChildObjectContractSpec, PreservesPublicObjectContracts) {
  std::vector<std::uint32_t> selected_cases;
  for (std::uint32_t logical = 0; logical < kChildCases.size(); ++logical) {
    if (dxmt::test::LogicalCaseSelected(kChildRegistration.family(), logical))
      selected_cases.push_back(logical);
  }
  ASSERT_FALSE(selected_cases.empty());

  RecordProperty("logical_cases_executed", selected_cases.size());
  RecordProperty("logical_case_prefix",
                 kChildRegistration.family().case_id_prefix);
  for (const std::uint32_t logical : selected_cases) {
    const ChildCase &test_case = kChildCases[logical];
    ComPtr<ID3D11DeviceChild> child;
    const HRESULT create_result =
        CreateChild(context_.device(), test_case.kind, child.put());
    ComPtr<IUnknown> canonical_identity;
    ComPtr<IUnknown> concrete;
    ComPtr<IUnknown> concrete_identity;
    ComPtr<ID3D11Device> owner;
    HRESULT identity_result = E_FAIL;
    HRESULT concrete_result = E_FAIL;
    HRESULT concrete_identity_result = E_FAIL;
    HRESULT unsupported_result = E_FAIL;
    void *unsupported_output =
        reinterpret_cast<void *>(static_cast<std::uintptr_t>(1));
    HRESULT null_result = E_FAIL;
    HRESULT set_result = E_FAIL;
    HRESULT get_result = E_FAIL;
    HRESULT delete_result = E_FAIL;
    HRESULT missing_result = E_FAIL;
    std::array<std::uint8_t, 4> expected_value = {
        static_cast<std::uint8_t>(logical),
        static_cast<std::uint8_t>(logical + 17),
        static_cast<std::uint8_t>(logical + 73),
        static_cast<std::uint8_t>(logical + 149),
    };
    std::array<std::uint8_t, 4> actual_value = {};
    UINT actual_size = actual_value.size();
    bool value_matches = false;
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
      unsupported_result =
          child->QueryInterface(__uuidof(ID3D11Device), &unsupported_output);
      null_result = child->QueryInterface(__uuidof(IUnknown), nullptr);
      set_result = child->SetPrivateData(
          kChildPrivateDataKey, expected_value.size(), expected_value.data());
      get_result = child->GetPrivateData(kChildPrivateDataKey, &actual_size,
                                         actual_value.data());
      value_matches = actual_size == expected_value.size() &&
                      actual_value == expected_value;
      delete_result = child->SetPrivateData(kChildPrivateDataKey, 0, nullptr);
      actual_size = actual_value.size();
      missing_result = child->GetPrivateData(kChildPrivateDataKey, &actual_size,
                                             actual_value.data());
    }

    const bool valid =
        create_result == S_OK && child && identity_result == S_OK &&
        canonical_identity && concrete_result == S_OK && concrete &&
        concrete_identity_result == S_OK &&
        concrete_identity.get() == canonical_identity.get() &&
        owner.get() == context_.device() &&
        unsupported_result == E_NOINTERFACE && unsupported_output == nullptr &&
        null_result == E_POINTER && set_result == S_OK && get_result == S_OK &&
        value_matches && delete_result == S_OK &&
        missing_result == DXGI_ERROR_NOT_FOUND;
    if (valid)
      continue;

    const auto case_id =
        dxmt::test::LogicalCaseId(kChildRegistration.family(), logical);
    ADD_FAILURE() << "LogicalCaseId: " << case_id << '\n'
                  << "Parameters: logical=" << logical
                  << " object=" << test_case.name
                  << " selected_cases=" << selected_cases.size() << '\n'
                  << "Expected: create_hresult=" << S_OK
                  << " identity_hresult=" << S_OK
                  << " concrete_hresult=" << S_OK
                  << " unsupported_hresult=" << E_NOINTERFACE
                  << " null_hresult=" << E_POINTER << " private_data_hresults=("
                  << S_OK << ',' << S_OK << ',' << S_OK << ','
                  << DXGI_ERROR_NOT_FOUND << ") owner=" << context_.device()
                  << '\n'
                  << "Observed: create_hresult=" << create_result
                  << " identity_hresult=" << identity_result
                  << " concrete_hresult=" << concrete_result
                  << " concrete_identity_hresult=" << concrete_identity_result
                  << " unsupported_hresult=" << unsupported_result
                  << " unsupported_output=" << unsupported_output
                  << " null_hresult=" << null_result
                  << " private_data_hresults=(" << set_result << ','
                  << get_result << ',' << delete_result << ',' << missing_result
                  << ") child=" << child.get() << " concrete=" << concrete.get()
                  << " canonical_identity=" << canonical_identity.get()
                  << " concrete_identity=" << concrete_identity.get()
                  << " owner=" << owner.get() << '\n'
                  << "Replay: --dxmt-case-id=" << case_id;
    break;
  }

  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

} // namespace
