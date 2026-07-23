#include <dxmt_test.hpp>

#include "d3d11_test_context.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10_1.h>
#include <d3d11_4.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Public D3D11 COM / object contract matrix: QueryInterface versioning,
// device-child identity, private data, debug names, and state-object GetDesc.

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D11TestContext;

::testing::AssertionResult HResultSucceeded(HRESULT hr) {
  if (SUCCEEDED(hr))
    return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure()
         << "HRESULT failed: 0x" << std::hex << static_cast<unsigned long>(hr);
}

// Stable GUID keys for private-data contract tests (not public interface IIDs).
constexpr GUID kPrivateDataValueKey = {
    0xa1b2c3d4,
    0xe5f6,
    0x4789,
    {0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89}};
constexpr GUID kPrivateDataInterfaceKey = {
    0x11223344,
    0x5566,
    0x7788,
    {0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00}};
constexpr GUID kPrivateDataAltValueKey = {
    0x0f1e2d3c,
    0x4b5a,
    0x6978,
    {0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0}};

class LifetimeProbe final : public IUnknown {
public:
  explicit LifetimeProbe(std::shared_ptr<std::atomic_bool> destroyed)
      : destroyed_(std::move(destroyed)) {}

  ~LifetimeProbe() { destroyed_->store(true, std::memory_order_release); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_POINTER;
    *object = nullptr;
    if (iid != __uuidof(IUnknown))
      return E_NOINTERFACE;
    *object = this;
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return ref_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG references =
        ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (!references)
      delete this;
    return references;
  }

private:
  std::atomic_ulong ref_count_{1};
  std::shared_ptr<std::atomic_bool> destroyed_;
};

struct DeviceInterfaceCase {
  const GUID *iid;
  const char *name;
};

std::vector<DeviceInterfaceCase> BuildDeviceInterfaceCases() {
  return {
      {&__uuidof(ID3D11Device1), "ID3D11Device1"},
      {&__uuidof(ID3D11Device2), "ID3D11Device2"},
      {&__uuidof(ID3D11Device3), "ID3D11Device3"},
      {&__uuidof(ID3D11Device4), "ID3D11Device4"},
      {&__uuidof(ID3D11Device5), "ID3D11Device5"},
  };
}

template <typename T>
bool MemEqual(const T &left, const T &right) {
  return std::memcmp(&left, &right, sizeof(T)) == 0;
}

void ExpectDepthStencilDescEqual(const D3D11_DEPTH_STENCIL_DESC &actual,
                                 const D3D11_DEPTH_STENCIL_DESC &expected) {
  EXPECT_EQ(actual.DepthEnable, expected.DepthEnable);
  EXPECT_EQ(actual.DepthWriteMask, expected.DepthWriteMask);
  EXPECT_EQ(actual.DepthFunc, expected.DepthFunc);
  EXPECT_EQ(actual.StencilEnable, expected.StencilEnable);
  EXPECT_EQ(actual.StencilReadMask, expected.StencilReadMask);
  EXPECT_EQ(actual.StencilWriteMask, expected.StencilWriteMask);
  EXPECT_EQ(actual.FrontFace.StencilFailOp,
            expected.FrontFace.StencilFailOp);
  EXPECT_EQ(actual.FrontFace.StencilDepthFailOp,
            expected.FrontFace.StencilDepthFailOp);
  EXPECT_EQ(actual.FrontFace.StencilPassOp,
            expected.FrontFace.StencilPassOp);
  EXPECT_EQ(actual.FrontFace.StencilFunc, expected.FrontFace.StencilFunc);
  EXPECT_EQ(actual.BackFace.StencilFailOp, expected.BackFace.StencilFailOp);
  EXPECT_EQ(actual.BackFace.StencilDepthFailOp,
            expected.BackFace.StencilDepthFailOp);
  EXPECT_EQ(actual.BackFace.StencilPassOp, expected.BackFace.StencilPassOp);
  EXPECT_EQ(actual.BackFace.StencilFunc, expected.BackFace.StencilFunc);
}

class ObjectComMatrixSpec : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(HResultSucceeded(context_.Initialize()));
    ASSERT_NE(context_.device(), nullptr);
  }

  ComPtr<ID3D11Buffer> CreateTestBuffer(UINT byte_width = 256) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = byte_width;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    ComPtr<ID3D11Buffer> buffer;
    EXPECT_TRUE(HResultSucceeded(
        context_.device()->CreateBuffer(&desc, nullptr, buffer.put())));
    return buffer;
  }

  ComPtr<ID3D11Texture2D> CreateTestTexture() {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 8;
    desc.Height = 8;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    EXPECT_TRUE(HResultSucceeded(
        context_.device()->CreateTexture2D(&desc, nullptr, texture.put())));
    return texture;
  }

  static ComPtr<IUnknown> AsIUnknown(IUnknown *object) {
    ComPtr<IUnknown> identity;
    EXPECT_EQ(object->QueryInterface(__uuidof(IUnknown),
                                     reinterpret_cast<void **>(identity.put())),
              S_OK);
    return identity;
  }

  // Works for both ID3D11Device and ID3D11DeviceChild (same private-data API).
  template <typename ObjectT>
  void ExpectPrivateDataRoundTrip(ObjectT *object) {
    ASSERT_NE(object, nullptr);
    constexpr std::array<std::uint8_t, 8> initial = {1, 2, 3, 4, 5, 6, 7, 8};
    constexpr std::array<std::uint8_t, 4> replacement = {0xaa, 0xbb, 0xcc,
                                                         0xdd};

    ASSERT_EQ(object->SetPrivateData(kPrivateDataValueKey, initial.size(),
                                     initial.data()),
              S_OK);

    std::array<std::uint8_t, 8> actual = {};
    UINT size = static_cast<UINT>(actual.size());
    ASSERT_EQ(object->GetPrivateData(kPrivateDataValueKey, &size, actual.data()),
              S_OK);
    EXPECT_EQ(size, initial.size());
    EXPECT_EQ(actual, initial);

    UINT required = 0;
    ASSERT_EQ(object->GetPrivateData(kPrivateDataValueKey, &required, nullptr),
              S_OK);
    EXPECT_EQ(required, initial.size());

    ASSERT_EQ(object->SetPrivateData(kPrivateDataValueKey, replacement.size(),
                                     replacement.data()),
              S_OK);
    std::array<std::uint8_t, 4> overwritten = {};
    size = static_cast<UINT>(overwritten.size());
    ASSERT_EQ(
        object->GetPrivateData(kPrivateDataValueKey, &size, overwritten.data()),
        S_OK);
    EXPECT_EQ(size, replacement.size());
    EXPECT_EQ(overwritten, replacement);

    // Independent GUID must not collide with the first key.
    constexpr std::uint32_t alt_value = 0xfeedfacu;
    ASSERT_EQ(object->SetPrivateData(kPrivateDataAltValueKey, sizeof(alt_value),
                                     &alt_value),
              S_OK);
    std::uint32_t alt_actual = 0;
    size = sizeof(alt_actual);
    ASSERT_EQ(object->GetPrivateData(kPrivateDataAltValueKey, &size, &alt_actual),
              S_OK);
    EXPECT_EQ(alt_actual, alt_value);

    // size 0 + nullptr removes the entry (COM private-data contract).
    ASSERT_EQ(object->SetPrivateData(kPrivateDataValueKey, 0, nullptr), S_OK);
    size = sizeof(overwritten);
    EXPECT_EQ(
        object->GetPrivateData(kPrivateDataValueKey, &size, overwritten.data()),
        DXGI_ERROR_NOT_FOUND);

    // Alternate key must still be present after deleting the first.
    size = sizeof(alt_actual);
    alt_actual = 0;
    ASSERT_EQ(object->GetPrivateData(kPrivateDataAltValueKey, &size, &alt_actual),
              S_OK);
    EXPECT_EQ(alt_actual, alt_value);
    EXPECT_EQ(object->SetPrivateData(kPrivateDataAltValueKey, 0, nullptr),
              S_OK);
  }

  template <typename ObjectT>
  void ExpectPrivateDataInterfaceRefcount(ObjectT *object) {
    ASSERT_NE(object, nullptr);
    auto destroyed = std::make_shared<std::atomic_bool>(false);
    auto *probe = new LifetimeProbe(destroyed);
    ASSERT_EQ(object->SetPrivateDataInterface(kPrivateDataInterfaceKey, probe),
              S_OK);
    probe->Release();
    ASSERT_FALSE(destroyed->load(std::memory_order_acquire));

    IUnknown *retrieved = nullptr;
    UINT size = sizeof(retrieved);
    ASSERT_EQ(object->GetPrivateData(kPrivateDataInterfaceKey, &size,
                                     &retrieved),
              S_OK);
    EXPECT_EQ(size, sizeof(retrieved));
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, static_cast<IUnknown *>(probe));
    retrieved->Release();
    EXPECT_FALSE(destroyed->load(std::memory_order_acquire));

    auto second_destroyed = std::make_shared<std::atomic_bool>(false);
    auto *second = new LifetimeProbe(second_destroyed);
    ASSERT_EQ(object->SetPrivateDataInterface(kPrivateDataInterfaceKey, second),
              S_OK);
    second->Release();
    EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
    EXPECT_FALSE(second_destroyed->load(std::memory_order_acquire));

    ASSERT_EQ(
        object->SetPrivateDataInterface(kPrivateDataInterfaceKey, nullptr),
        S_OK);
    EXPECT_TRUE(second_destroyed->load(std::memory_order_acquire));
  }

  D3D11TestContext context_;
};

// ---------------------------------------------------------------------------
// 1. Device QueryInterface matrix for versioned ID3D11DeviceN interfaces
// ---------------------------------------------------------------------------

class DeviceVersionQiMatrixSpec
    : public ObjectComMatrixSpec,
      public ::testing::WithParamInterface<DeviceInterfaceCase> {};

TEST_P(DeviceVersionQiMatrixSpec, ExposesVersionedDeviceInterface) {
  const auto &test = GetParam();
  IUnknown *versioned = nullptr;
  const HRESULT hr = context_.device()->QueryInterface(
      *test.iid, reinterpret_cast<void **>(&versioned));

  ASSERT_EQ(hr, S_OK) << test.name;
  ASSERT_NE(versioned, nullptr) << test.name;

  // QI back to base ID3D11Device must succeed.
  ComPtr<ID3D11Device> base_device;
  ASSERT_EQ(versioned->QueryInterface(
                __uuidof(ID3D11Device),
                reinterpret_cast<void **>(base_device.put())),
            S_OK)
      << test.name;
  ASSERT_NE(base_device.get(), nullptr) << test.name;

  // IUnknown identity: original device and versioned interface share identity.
  const auto device_identity = AsIUnknown(context_.device());
  const auto versioned_identity = AsIUnknown(versioned);
  const auto base_identity = AsIUnknown(base_device.get());
  ASSERT_TRUE(device_identity);
  ASSERT_TRUE(versioned_identity);
  ASSERT_TRUE(base_identity);
  EXPECT_EQ(device_identity.get(), versioned_identity.get()) << test.name;
  EXPECT_EQ(device_identity.get(), base_identity.get()) << test.name;

  // Repeated QI of the same IID yields the same IUnknown identity.
  IUnknown *again = nullptr;
  ASSERT_EQ(context_.device()->QueryInterface(
                *test.iid, reinterpret_cast<void **>(&again)),
            S_OK)
      << test.name;
  ASSERT_NE(again, nullptr);
  const auto again_identity = AsIUnknown(again);
  EXPECT_EQ(versioned_identity.get(), again_identity.get()) << test.name;
  again->Release();

  versioned->Release();
}

INSTANTIATE_TEST_SUITE_P(
    DeviceQiMatrix, DeviceVersionQiMatrixSpec,
    ::testing::ValuesIn(BuildDeviceInterfaceCases()),
    [](const ::testing::TestParamInfo<DeviceInterfaceCase> &info) {
      return info.param.name;
    });

TEST_F(ObjectComMatrixSpec, DeviceQueryInterfaceRejectsUnrelatedInterface) {
  void *output = reinterpret_cast<void *>(uintptr_t{1});
  EXPECT_EQ(context_.device()->QueryInterface(__uuidof(ID3D11Buffer), &output),
            E_NOINTERFACE);
  EXPECT_EQ(output, nullptr);
}

TEST_F(ObjectComMatrixSpec, DeviceQueryInterfaceNullOutputIsRejected) {
  EXPECT_EQ(context_.device()->QueryInterface(__uuidof(IUnknown), nullptr),
            E_POINTER);
}

TEST_F(ObjectComMatrixSpec,
       OptionalD3D10InterfacesShareIdentityAndPrivateData) {
  ComPtr<ID3D10Device> device10;
  ComPtr<ID3D10Device1> device10_1;
  const HRESULT device10_result = context_.device()->QueryInterface(
      __uuidof(ID3D10Device), reinterpret_cast<void **>(device10.put()));
  const HRESULT device10_1_result = context_.device()->QueryInterface(
      __uuidof(ID3D10Device1), reinterpret_cast<void **>(device10_1.put()));
  ASSERT_TRUE(device10_result == S_OK || device10_result == E_NOINTERFACE);
  ASSERT_EQ(device10_1_result, device10_result);
  if (device10_result == E_NOINTERFACE) {
    EXPECT_EQ(device10.get(), nullptr);
    EXPECT_EQ(device10_1.get(), nullptr);
    EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
    return;
  }
  ASSERT_NE(device10.get(), nullptr);
  ASSERT_NE(device10_1.get(), nullptr);

  const auto device11_identity = AsIUnknown(context_.device());
  const auto device10_identity = AsIUnknown(device10.get());
  const auto device10_1_identity = AsIUnknown(device10_1.get());
  ASSERT_TRUE(device11_identity);
  ASSERT_TRUE(device10_identity);
  ASSERT_TRUE(device10_1_identity);
  EXPECT_EQ(device10_identity.get(), device11_identity.get());
  EXPECT_EQ(device10_1_identity.get(), device11_identity.get());

  ComPtr<ID3D11Device5> device11_5;
  ASSERT_EQ(
      device10_1->QueryInterface(__uuidof(ID3D11Device5),
                                 reinterpret_cast<void **>(device11_5.put())),
      S_OK);
  EXPECT_EQ(AsIUnknown(device11_5.get()).get(), device11_identity.get());
  EXPECT_EQ(device10->GetCreationFlags(),
            context_.device()->GetCreationFlags());

  constexpr std::array<std::uint8_t, 5> kD3D10Value = {2, 3, 5, 7, 11};
  ASSERT_EQ(device10->SetPrivateData(kPrivateDataValueKey, kD3D10Value.size(),
                                     kD3D10Value.data()),
            S_OK);
  std::array<std::uint8_t, kD3D10Value.size()> d3d11_value = {};
  UINT data_size = d3d11_value.size();
  ASSERT_EQ(context_.device()->GetPrivateData(kPrivateDataValueKey, &data_size,
                                              d3d11_value.data()),
            S_OK);
  EXPECT_EQ(data_size, kD3D10Value.size());
  EXPECT_EQ(d3d11_value, kD3D10Value);

  constexpr std::array<std::uint8_t, 6> kD3D11Value = {13, 17, 19, 23, 29, 31};
  ASSERT_EQ(context_.device()->SetPrivateData(
                kPrivateDataValueKey, kD3D11Value.size(), kD3D11Value.data()),
            S_OK);
  std::array<std::uint8_t, kD3D11Value.size()> d3d10_value = {};
  data_size = d3d10_value.size();
  ASSERT_EQ(device10_1->GetPrivateData(kPrivateDataValueKey, &data_size,
                                       d3d10_value.data()),
            S_OK);
  EXPECT_EQ(data_size, kD3D11Value.size());
  EXPECT_EQ(d3d10_value, kD3D11Value);

  EXPECT_EQ(device10_1->SetPrivateData(kPrivateDataValueKey, 0, nullptr), S_OK);
  data_size = d3d11_value.size();
  EXPECT_EQ(context_.device()->GetPrivateData(kPrivateDataValueKey, &data_size,
                                              d3d11_value.data()),
            DXGI_ERROR_NOT_FOUND);
  EXPECT_EQ(device10->GetDeviceRemovedReason(), S_OK);
  EXPECT_EQ(context_.device()->GetDeviceRemovedReason(), S_OK);
}

// ---------------------------------------------------------------------------
// 2. Device-child QI to ID3D11Resource / concrete type + GetDevice identity
// ---------------------------------------------------------------------------

TEST_F(ObjectComMatrixSpec, BufferQueriesAsResourceAndBackToBuffer) {
  auto buffer = CreateTestBuffer();
  ASSERT_TRUE(buffer);

  ComPtr<ID3D11Resource> resource;
  ASSERT_EQ(buffer->QueryInterface(__uuidof(ID3D11Resource),
                                   reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_NE(resource.get(), nullptr);

  ComPtr<ID3D11Buffer> buffer_again;
  ASSERT_EQ(resource->QueryInterface(
                __uuidof(ID3D11Buffer),
                reinterpret_cast<void **>(buffer_again.put())),
            S_OK);
  ASSERT_NE(buffer_again.get(), nullptr);

  const auto buffer_identity = AsIUnknown(buffer.get());
  const auto resource_identity = AsIUnknown(resource.get());
  const auto again_identity = AsIUnknown(buffer_again.get());
  EXPECT_EQ(buffer_identity.get(), resource_identity.get());
  EXPECT_EQ(buffer_identity.get(), again_identity.get());

  // Resource dimension must match buffer.
  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  resource->GetType(&dimension);
  EXPECT_EQ(dimension, D3D11_RESOURCE_DIMENSION_BUFFER);
}

TEST_F(ObjectComMatrixSpec, TextureQueriesAsResourceAndBackToTexture2D) {
  auto texture = CreateTestTexture();
  ASSERT_TRUE(texture);

  ComPtr<ID3D11Resource> resource;
  ASSERT_EQ(texture->QueryInterface(__uuidof(ID3D11Resource),
                                    reinterpret_cast<void **>(resource.put())),
            S_OK);
  ASSERT_NE(resource.get(), nullptr);

  ComPtr<ID3D11Texture2D> texture_again;
  ASSERT_EQ(resource->QueryInterface(
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void **>(texture_again.put())),
            S_OK);
  ASSERT_NE(texture_again.get(), nullptr);

  const auto texture_identity = AsIUnknown(texture.get());
  const auto resource_identity = AsIUnknown(resource.get());
  const auto again_identity = AsIUnknown(texture_again.get());
  EXPECT_EQ(texture_identity.get(), resource_identity.get());
  EXPECT_EQ(texture_identity.get(), again_identity.get());

  D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
  resource->GetType(&dimension);
  EXPECT_EQ(dimension, D3D11_RESOURCE_DIMENSION_TEXTURE2D);

  // Concrete type QI for a different resource type must fail.
  ComPtr<ID3D11Buffer> as_buffer;
  EXPECT_EQ(resource->QueryInterface(
                __uuidof(ID3D11Buffer),
                reinterpret_cast<void **>(as_buffer.put())),
            E_NOINTERFACE);
  EXPECT_FALSE(as_buffer);
}

TEST_F(ObjectComMatrixSpec, DeviceChildrenReportCreatingDevice) {
  auto buffer = CreateTestBuffer();
  auto texture = CreateTestTexture();
  ASSERT_TRUE(buffer);
  ASSERT_TRUE(texture);

  const auto expected = AsIUnknown(context_.device());
  ASSERT_TRUE(expected);

  for (ID3D11DeviceChild *child :
       {static_cast<ID3D11DeviceChild *>(buffer.get()),
        static_cast<ID3D11DeviceChild *>(texture.get())}) {
    ComPtr<ID3D11Device> device;
    child->GetDevice(device.put());
    ASSERT_NE(device.get(), nullptr);
    const auto actual = AsIUnknown(device.get());
    EXPECT_EQ(actual.get(), expected.get());
  }
}

// ---------------------------------------------------------------------------
// 3. SetPrivateData / GetPrivateData / SetPrivateDataInterface contracts
// ---------------------------------------------------------------------------

TEST_F(ObjectComMatrixSpec, DevicePrivateDataStoresReadsOverwritesAndDeletes) {
  // ID3D11Device inherits ID3D11DeviceChild private-data methods.
  ExpectPrivateDataRoundTrip(context_.device());
}

TEST_F(ObjectComMatrixSpec, BufferPrivateDataStoresReadsOverwritesAndDeletes) {
  auto buffer = CreateTestBuffer();
  ASSERT_TRUE(buffer);
  ExpectPrivateDataRoundTrip(buffer.get());
}

TEST_F(ObjectComMatrixSpec, DevicePrivateDataInterfaceRetainsAndReleases) {
  ExpectPrivateDataInterfaceRefcount(context_.device());
}

TEST_F(ObjectComMatrixSpec, BufferPrivateDataInterfaceRetainsAndReleases) {
  auto buffer = CreateTestBuffer();
  ASSERT_TRUE(buffer);
  ExpectPrivateDataInterfaceRefcount(buffer.get());
}

TEST_F(ObjectComMatrixSpec, BufferDestructionReleasesPrivateInterface) {
  auto destroyed = std::make_shared<std::atomic_bool>(false);
  auto *probe = new LifetimeProbe(destroyed);
  {
    auto buffer = CreateTestBuffer();
    ASSERT_TRUE(buffer);
    ASSERT_EQ(
        buffer->SetPrivateDataInterface(kPrivateDataInterfaceKey, probe), S_OK);
    probe->Release();
    ASSERT_FALSE(destroyed->load(std::memory_order_acquire));
  }
  EXPECT_TRUE(destroyed->load(std::memory_order_acquire));
}

// ---------------------------------------------------------------------------
// 4. WKPDID_D3DDebugObjectName via public SetPrivateData GUID
// ---------------------------------------------------------------------------

TEST_F(ObjectComMatrixSpec, DeviceDebugObjectNameRoundTrips) {
  constexpr char name[] = "dxmt-device-com-matrix";
  ASSERT_EQ(context_.device()->SetPrivateData(WKPDID_D3DDebugObjectName,
                                              sizeof(name) - 1, name),
            S_OK);

  char actual[sizeof(name)] = {};
  UINT size = sizeof(name) - 1;
  ASSERT_EQ(context_.device()->GetPrivateData(WKPDID_D3DDebugObjectName, &size,
                                              actual),
            S_OK);
  EXPECT_EQ(size, sizeof(name) - 1);
  EXPECT_EQ(std::string(actual, size), std::string(name, sizeof(name) - 1));

  ASSERT_EQ(
      context_.device()->SetPrivateData(WKPDID_D3DDebugObjectName, 0, nullptr),
      S_OK);
  size = 0;
  EXPECT_EQ(context_.device()->GetPrivateData(WKPDID_D3DDebugObjectName, &size,
                                              nullptr),
            DXGI_ERROR_NOT_FOUND);
}

TEST_F(ObjectComMatrixSpec, BufferDebugObjectNameRoundTrips) {
  auto buffer = CreateTestBuffer();
  ASSERT_TRUE(buffer);

  constexpr char name[] = "dxmt-buffer-com-matrix";
  ASSERT_EQ(buffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof(name) - 1,
                                   name),
            S_OK);

  UINT required = 0;
  ASSERT_EQ(
      buffer->GetPrivateData(WKPDID_D3DDebugObjectName, &required, nullptr),
      S_OK);
  EXPECT_EQ(required, sizeof(name) - 1);

  std::vector<char> actual(required);
  UINT size = required;
  ASSERT_EQ(buffer->GetPrivateData(WKPDID_D3DDebugObjectName, &size,
                                   actual.data()),
            S_OK);
  EXPECT_EQ(size, required);
  EXPECT_EQ(std::string(actual.data(), size),
            std::string(name, sizeof(name) - 1));

  // Overwrite with a different name.
  constexpr char renamed[] = "renamed-buffer";
  ASSERT_EQ(buffer->SetPrivateData(WKPDID_D3DDebugObjectName,
                                   sizeof(renamed) - 1, renamed),
            S_OK);
  char renamed_actual[sizeof(renamed)] = {};
  size = sizeof(renamed) - 1;
  ASSERT_EQ(buffer->GetPrivateData(WKPDID_D3DDebugObjectName, &size,
                                   renamed_actual),
            S_OK);
  EXPECT_EQ(std::string(renamed_actual, size),
            std::string(renamed, sizeof(renamed) - 1));
}

// ---------------------------------------------------------------------------
// 5. Create*State GetDesc round-trip matrix for several flag combinations
// ---------------------------------------------------------------------------

TEST_F(ObjectComMatrixSpec, BlendStateGetDescRoundTripMatrix) {
  const auto make_rt = [](BOOL enable, D3D11_BLEND src, D3D11_BLEND dst,
                          D3D11_BLEND_OP op, D3D11_BLEND src_a, D3D11_BLEND dst_a,
                          D3D11_BLEND_OP op_a, UINT8 write_mask) {
    D3D11_RENDER_TARGET_BLEND_DESC rt = {};
    rt.BlendEnable = enable;
    rt.SrcBlend = src;
    rt.DestBlend = dst;
    rt.BlendOp = op;
    rt.SrcBlendAlpha = src_a;
    rt.DestBlendAlpha = dst_a;
    rt.BlendOpAlpha = op_a;
    rt.RenderTargetWriteMask = write_mask;
    return rt;
  };
  const auto default_rt = make_rt(
      FALSE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD,
      D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD,
      D3D11_COLOR_WRITE_ENABLE_ALL);

  const std::array<D3D11_BLEND_DESC, 3> descs = [&] {
    std::array<D3D11_BLEND_DESC, 3> cases = {};

    // Default-like: no blending, full write mask.
    cases[0].RenderTarget[0] = default_rt;

    // Classic alpha blend.
    cases[1].AlphaToCoverageEnable = FALSE;
    cases[1].IndependentBlendEnable = FALSE;
    cases[1].RenderTarget[0] = make_rt(
        TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
        D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO,
        D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL);

    // Independent RT + alpha-to-coverage + partial write mask.
    // Fill every RT slot with a valid desc when IndependentBlendEnable is set.
    cases[2].AlphaToCoverageEnable = TRUE;
    cases[2].IndependentBlendEnable = TRUE;
    for (auto &rt : cases[2].RenderTarget)
      rt = default_rt;
    cases[2].RenderTarget[0] = make_rt(
        TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD,
        D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD,
        D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN);
    cases[2].RenderTarget[1] = default_rt;
    return cases;
  }();

  for (size_t index = 0; index < descs.size(); ++index) {
    SCOPED_TRACE(index);
    ComPtr<ID3D11BlendState> state;
    ASSERT_TRUE(HResultSucceeded(
        context_.device()->CreateBlendState(&descs[index], state.put())));
    ASSERT_NE(state.get(), nullptr);

    D3D11_BLEND_DESC actual = {};
    state->GetDesc(&actual);
    EXPECT_EQ(actual.AlphaToCoverageEnable, descs[index].AlphaToCoverageEnable);
    EXPECT_EQ(actual.IndependentBlendEnable,
              descs[index].IndependentBlendEnable);

    // Same observable contract as pipeline_spec
    // CreatesStateObjectsWithRequestedDescriptions (flags + RT0 SrcBlend /
    // BlendEnable / write mask). Full Dest/alpha field round-trip is covered
    // behaviorally by clear_blend_matrix_spec draws.
    const auto &expected_rt0 = descs[index].RenderTarget[0];
    const auto &actual_rt0 = actual.RenderTarget[0];
    EXPECT_EQ(actual_rt0.BlendEnable, expected_rt0.BlendEnable);
    EXPECT_EQ(actual_rt0.SrcBlend, expected_rt0.SrcBlend);
    EXPECT_EQ(actual_rt0.RenderTargetWriteMask,
              expected_rt0.RenderTargetWriteMask);
  }
}

TEST_F(ObjectComMatrixSpec, RasterizerStateGetDescRoundTripMatrix) {
  const std::array<D3D11_RASTERIZER_DESC, 3> descs = [] {
    std::array<D3D11_RASTERIZER_DESC, 3> cases = {};

    cases[0].FillMode = D3D11_FILL_SOLID;
    cases[0].CullMode = D3D11_CULL_BACK;
    cases[0].FrontCounterClockwise = FALSE;
    cases[0].DepthClipEnable = TRUE;

    cases[1].FillMode = D3D11_FILL_WIREFRAME;
    cases[1].CullMode = D3D11_CULL_FRONT;
    cases[1].FrontCounterClockwise = TRUE;
    cases[1].DepthBias = 2;
    cases[1].DepthBiasClamp = 0.5f;
    cases[1].SlopeScaledDepthBias = 1.25f;
    cases[1].DepthClipEnable = FALSE;
    cases[1].ScissorEnable = TRUE;
    cases[1].MultisampleEnable = TRUE;
    cases[1].AntialiasedLineEnable = TRUE;

    cases[2].FillMode = D3D11_FILL_SOLID;
    cases[2].CullMode = D3D11_CULL_NONE;
    cases[2].FrontCounterClockwise = FALSE;
    cases[2].DepthClipEnable = TRUE;
    cases[2].ScissorEnable = TRUE;
    cases[2].MultisampleEnable = FALSE;
    cases[2].AntialiasedLineEnable = FALSE;
    return cases;
  }();

  for (size_t index = 0; index < descs.size(); ++index) {
    SCOPED_TRACE(index);
    ComPtr<ID3D11RasterizerState> state;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateRasterizerState(
        &descs[index], state.put())));
    ASSERT_NE(state.get(), nullptr);

    D3D11_RASTERIZER_DESC actual = {};
    state->GetDesc(&actual);
    EXPECT_EQ(actual.FillMode, descs[index].FillMode);
    EXPECT_EQ(actual.CullMode, descs[index].CullMode);
    EXPECT_EQ(actual.FrontCounterClockwise, descs[index].FrontCounterClockwise);
    EXPECT_EQ(actual.DepthBias, descs[index].DepthBias);
    EXPECT_FLOAT_EQ(actual.DepthBiasClamp, descs[index].DepthBiasClamp);
    EXPECT_FLOAT_EQ(actual.SlopeScaledDepthBias,
                    descs[index].SlopeScaledDepthBias);
    EXPECT_EQ(actual.DepthClipEnable, descs[index].DepthClipEnable);
    EXPECT_EQ(actual.ScissorEnable, descs[index].ScissorEnable);
    EXPECT_EQ(actual.MultisampleEnable, descs[index].MultisampleEnable);
    EXPECT_EQ(actual.AntialiasedLineEnable,
              descs[index].AntialiasedLineEnable);
  }
}

TEST_F(ObjectComMatrixSpec, DepthStencilStateGetDescRoundTripMatrix) {
  const std::array<D3D11_DEPTH_STENCIL_DESC, 3> descs = [] {
    std::array<D3D11_DEPTH_STENCIL_DESC, 3> cases = {};

    // Depth only.
    cases[0].DepthEnable = TRUE;
    cases[0].DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    cases[0].DepthFunc = D3D11_COMPARISON_LESS;
    cases[0].StencilEnable = FALSE;
    cases[0].StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    cases[0].StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    cases[0].FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    cases[0].FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    cases[0].FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    cases[0].FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    cases[0].BackFace = cases[0].FrontFace;

    // Stencil with asymmetric faces.
    cases[1].DepthEnable = TRUE;
    cases[1].DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    cases[1].DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    cases[1].StencilEnable = TRUE;
    cases[1].StencilReadMask = 0x3f;
    cases[1].StencilWriteMask = 0x7f;
    cases[1].FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    cases[1].FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR_SAT;
    cases[1].FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    cases[1].FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    cases[1].BackFace.StencilFailOp = D3D11_STENCIL_OP_ZERO;
    cases[1].BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    cases[1].BackFace.StencilPassOp = D3D11_STENCIL_OP_INVERT;
    cases[1].BackFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;

    // Disabled depth/stencil fields are intentionally non-default on input;
    // the public runtime normalizes ignored fields in the returned descriptor.
    cases[2].DepthEnable = FALSE;
    cases[2].DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    cases[2].DepthFunc = D3D11_COMPARISON_ALWAYS;
    cases[2].StencilEnable = FALSE;
    cases[2].StencilReadMask = 0xff;
    cases[2].StencilWriteMask = 0x00;
    cases[2].FrontFace.StencilFailOp = D3D11_STENCIL_OP_INCR;
    cases[2].FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR_SAT;
    cases[2].FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    cases[2].FrontFace.StencilFunc = D3D11_COMPARISON_NEVER;
    cases[2].BackFace = cases[2].FrontFace;
    return cases;
  }();

  for (size_t index = 0; index < descs.size(); ++index) {
    SCOPED_TRACE(index);
    ComPtr<ID3D11DepthStencilState> state;
    ASSERT_TRUE(HResultSucceeded(context_.device()->CreateDepthStencilState(
        &descs[index], state.put())));
    ASSERT_NE(state.get(), nullptr);

    D3D11_DEPTH_STENCIL_DESC actual = {};
    state->GetDesc(&actual);
    D3D11_DEPTH_STENCIL_DESC expected = descs[index];
    if (!expected.DepthEnable) {
      expected.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
      expected.DepthFunc = D3D11_COMPARISON_LESS;
    }
    if (!expected.StencilEnable) {
      expected.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
      expected.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
      expected.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
      expected.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
      expected.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
      expected.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
      expected.BackFace = expected.FrontFace;
    }
    ExpectDepthStencilDescEqual(actual, expected);
  }
}

} // namespace
