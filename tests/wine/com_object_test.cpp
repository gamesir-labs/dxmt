#include <dxmt_test.hpp>

#include "com/com_aggregatable.hpp"
#include "com/com_object.hpp"

#include <cstdint>

namespace {

struct ObjectState {
  bool destroyed = false;
};

template <typename Base> class UnknownObject : public Base {
public:
  explicit UnknownObject(ObjectState &state) : state_(state) {}
  ~UnknownObject() override { state_.destroyed = true; }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
    if (!object)
      return E_POINTER;
    *object = nullptr;
    if (iid != IID_IUnknown)
      return E_NOINTERFACE;
    *object = static_cast<IUnknown *>(this);
    this->AddRef();
    return S_OK;
  }

private:
  ObjectState &state_;
};

using DeferredUnknown = UnknownObject<dxmt::ComObject<IUnknown>>;
using InitialUnknown = UnknownObject<dxmt::ComObjectWithInitialRef<IUnknown>>;
using ClampedUnknown = UnknownObject<dxmt::ComObjectClamp<IUnknown>>;

class OuterUnknown final : public IUnknown {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
    ++queries;
    if (!object)
      return E_POINTER;
    *object = nullptr;
    if (iid != IID_IUnknown)
      return E_NOINTERFACE;
    *object = this;
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
  ULONG STDMETHODCALLTYPE Release() override { return --references; }

  ULONG references = 1;
  uint32_t queries = 0;
};

TEST(ComObject, PublicReferencesRetainThePrivateLifetime) {
  ObjectState state;
  auto *object = new DeferredUnknown(state);

  EXPECT_EQ(object->AddRef(), 1u);
  EXPECT_EQ(object->GetPrivateRefCount(), 1u);
  object->AddRefPrivate();
  EXPECT_EQ(object->GetPrivateRefCount(), 2u);
  EXPECT_EQ(object->Release(), 0u);
  EXPECT_FALSE(state.destroyed);

  object->ReleasePrivate();
  EXPECT_TRUE(state.destroyed);
}

TEST(ComObject, InitialReferenceOwnsTheObjectUntilFinalRelease) {
  ObjectState state;
  auto *object = new InitialUnknown(state);

  EXPECT_EQ(object->AddRef(), 2u);
  EXPECT_EQ(object->Release(), 1u);
  EXPECT_FALSE(state.destroyed);
  EXPECT_EQ(object->Release(), 0u);
  EXPECT_TRUE(state.destroyed);
}

TEST(ComObjectClamp, ReleaseAtZeroDoesNotUnderflow) {
  ObjectState state;
  auto *object = new ClampedUnknown(state);
  object->AddRefPrivate();

  EXPECT_EQ(object->Release(), 0u);
  EXPECT_FALSE(state.destroyed);
  EXPECT_EQ(object->AddRef(), 1u);
  EXPECT_EQ(object->Release(), 0u);
  EXPECT_FALSE(state.destroyed);

  object->ReleasePrivate();
  EXPECT_TRUE(state.destroyed);
}

TEST(ComAggregatedObject, DelegatesIUnknownToTheOuterObject) {
  OuterUnknown outer;
  dxmt::ComAggregatedObject<OuterUnknown, IUnknown> aggregate(&outer);

  void *queried = nullptr;
  EXPECT_EQ(aggregate.QueryInterface(IID_IUnknown, &queried), S_OK);
  EXPECT_EQ(queried, &outer);
  EXPECT_EQ(outer.queries, 1u);
  EXPECT_EQ(outer.references, 2u);
  EXPECT_EQ(aggregate.Release(), 1u);
  EXPECT_EQ(aggregate.AddRef(), 2u);
  EXPECT_EQ(aggregate.Release(), 1u);
}

TEST(ComObject, InitializesOptionalOutputPointers) {
  auto *value = reinterpret_cast<IUnknown *>(uintptr_t{1});
  dxmt::InitReturnPtr(&value);
  EXPECT_EQ(value, nullptr);
  dxmt::InitReturnPtr<IUnknown>(nullptr);
}

} // namespace
