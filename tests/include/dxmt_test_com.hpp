#pragma once

namespace dxmt::test {

template <typename T> class ComPtr {
public:
  ComPtr() = default;
  explicit ComPtr(T *object) : object_(object) {}

  ~ComPtr() { reset(); }

  ComPtr(const ComPtr &) = delete;
  ComPtr &operator=(const ComPtr &) = delete;

  ComPtr(ComPtr &&other) noexcept : object_(other.release()) {}

  ComPtr &operator=(ComPtr &&other) noexcept {
    if (this != &other) {
      reset();
      object_ = other.release();
    }
    return *this;
  }

  T *get() const { return object_; }
  T **put() {
    reset();
    return &object_;
  }
  T *operator->() const { return object_; }
  explicit operator bool() const { return object_ != nullptr; }

  T *release() {
    T *object = object_;
    object_ = nullptr;
    return object;
  }

  void reset(T *object = nullptr) {
    if (object_)
      object_->Release();
    object_ = object;
  }

private:
  T *object_ = nullptr;
};

} // namespace dxmt::test
