#include <dxmt_test.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>

#include <atomic>
#include <iomanip>
#include <thread>

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

class D3D11DeviceSpec : public ::testing::Test {
protected:
  void SetUp() override {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   0, nullptr, 0, D3D11_SDK_VERSION, &device_,
                                   &feature_level_, &context_);

    ASSERT_TRUE(HResultSucceeded(hr));
    ASSERT_NE(device_, nullptr);
    ASSERT_NE(context_, nullptr);
  }

  void TearDown() override {
    release_object(context_);
    release_object(device_);
  }

  ID3D11Device *device_ = nullptr;
  ID3D11DeviceContext *context_ = nullptr;
  D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL(0);
};

} // namespace

TEST(D3D11DeviceCreationSpec, RejectsMissingOutputs) {
  HRESULT hr =
      D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr,
                        0, D3D11_SDK_VERSION, nullptr, nullptr, nullptr);

  EXPECT_EQ(hr, S_FALSE);
}

TEST_F(D3D11DeviceSpec, ReportsSupportedFeatureLevel) {
  EXPECT_GE(feature_level_, D3D_FEATURE_LEVEL_11_0);
  EXPECT_GE(device_->GetFeatureLevel(), D3D_FEATURE_LEVEL_11_0);
}

TEST_F(D3D11DeviceSpec, ReturnsImmediateContextFromDevice) {
  ID3D11DeviceContext *context = nullptr;

  device_->GetImmediateContext(&context);

  EXPECT_NE(context, nullptr);
  release_object(context);
}

TEST_F(D3D11DeviceSpec, CreatesDefaultVertexBuffer) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = 256;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  ID3D11Buffer *buffer = nullptr;

  HRESULT hr = device_->CreateBuffer(&desc, nullptr, &buffer);

  EXPECT_TRUE(HResultSucceeded(hr));
  EXPECT_NE(buffer, nullptr);

  release_object(buffer);
}

TEST_F(D3D11DeviceSpec, RemovesDebugNameWithNullData) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = 256;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

  ID3D11Buffer *buffer = nullptr;
  ASSERT_TRUE(HResultSucceeded(device_->CreateBuffer(&desc, nullptr, &buffer)));
  ASSERT_NE(buffer, nullptr);

  constexpr char name[] = "buffer";
  EXPECT_EQ(buffer->SetPrivateData(WKPDID_D3DDebugObjectName,
                                   sizeof(name) - 1, name),
            S_OK);
  EXPECT_EQ(buffer->SetPrivateData(WKPDID_D3DDebugObjectName,
                                   sizeof(name) - 1, nullptr),
            S_OK);

  UINT size = 0;
  EXPECT_EQ(buffer->GetPrivateData(WKPDID_D3DDebugObjectName, &size, nullptr),
            DXGI_ERROR_NOT_FOUND);
  release_object(buffer);
}

TEST_F(D3D11DeviceSpec, ReportsPipelineStatisticsQueryCapability) {
  D3D11_QUERY_DESC desc = {};
  desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
  ID3D11Query *query = nullptr;
  const HRESULT statistics_hr = device_->CreateQuery(&desc, &query);
  EXPECT_TRUE(HResultSucceeded(statistics_hr));
  EXPECT_NE(query, nullptr);
  release_object(query);

  desc.Query = D3D11_QUERY_OCCLUSION;
  EXPECT_TRUE(HResultSucceeded(device_->CreateQuery(&desc, &query)));
  EXPECT_NE(query, nullptr);
  release_object(query);
}

TEST_F(D3D11DeviceSpec, AllowsRecursiveContextEntryWhileAnotherThreadWaits) {
  ID3D11Multithread *multithread = nullptr;
  ASSERT_TRUE(HResultSucceeded(context_->QueryInterface(
      __uuidof(ID3D11Multithread),
      reinterpret_cast<void **>(&multithread))));
  ASSERT_NE(multithread, nullptr);
  multithread->SetMultithreadProtected(TRUE);
  ASSERT_TRUE(multithread->GetMultithreadProtected());

  std::atomic_bool contender_started = false;
  std::atomic_bool contender_completed = false;
  multithread->Enter();
  std::thread contender([&] {
    contender_started.store(true, std::memory_order_release);
    multithread->Enter();
    multithread->Leave();
    contender_completed.store(true, std::memory_order_release);
  });
  while (!contender_started.load(std::memory_order_acquire))
    Sleep(0);

  // Give the contender time to block on the outer Enter, then exercise an
  // immediate-context method that recursively acquires the same protection
  // lock. A waiter must not hold a transition gate needed by this recursion.
  Sleep(25);
  context_->Flush();
  multithread->Leave();
  contender.join();

  EXPECT_TRUE(contender_completed.load(std::memory_order_acquire));
  multithread->Release();
}
