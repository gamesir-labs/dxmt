#include <dxmt_test.hpp>

#include <dxmt_argument_buffer.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

struct DrawArguments {
  std::uint32_t vertex_count = 0;
  std::uint32_t instance_count = 0;
};

struct FakeArgumentBufferSlice {
  void *mapped = nullptr;
  bool gpu_buffer = false;
  std::size_t length = 0;
};

class FakeArgumentBufferContext {
public:
  template <typename T, bool ComputeCommandEncoder = false>
  T *getMappedArgumentBuffer(std::size_t offset) {
    used_compute_command_encoder_ = ComputeCommandEncoder;
    if (!mapping_available_ || offset != 64)
      return nullptr;
    return reinterpret_cast<T *>(&storage_);
  }

  void setMappingAvailable(bool available) { mapping_available_ = available; }
  DrawArguments &storage() { return storage_; }
  bool usedComputeCommandEncoder() const {
    return used_compute_command_encoder_;
  }

private:
  bool mapping_available_ = false;
  bool used_compute_command_encoder_ = false;
  DrawArguments storage_ = {};
};

} // namespace

TEST(ArgumentBufferMapping, SkipsWritesWhenTheMappedRangeIsUnavailable) {
  DrawArguments storage = {};
  FakeArgumentBufferSlice slice = {nullptr, true, sizeof(storage)};
  EXPECT_EQ(dxmt::MappedArgumentBufferSlice<DrawArguments>(slice, 1), nullptr);

  slice.mapped = &storage;
  slice.gpu_buffer = false;
  EXPECT_EQ(dxmt::MappedArgumentBufferSlice<DrawArguments>(slice, 1), nullptr);

  slice.gpu_buffer = true;
  slice.length = sizeof(storage) - 1;
  EXPECT_EQ(dxmt::MappedArgumentBufferSlice<DrawArguments>(slice, 1), nullptr);
}

TEST(ArgumentBufferMapping, WritesOnlyAfterTheMappedRangeIsValidated) {
  DrawArguments storage[2] = {};
  FakeArgumentBufferSlice slice = {storage, true, sizeof(storage)};
  auto *mapped = dxmt::MappedArgumentBufferSlice<DrawArguments>(slice, 2);
  ASSERT_NE(mapped, nullptr);
  mapped[1].vertex_count = 3;
  mapped[1].instance_count = 2;
  EXPECT_EQ(storage[1].vertex_count, 3u);
  EXPECT_EQ(storage[1].instance_count, 2u);
}

TEST(ArgumentBufferMapping, RejectsElementCountOverflow) {
  std::size_t size = 19;
  EXPECT_FALSE(dxmt::ArgumentBufferByteSize<DrawArguments>(
      std::numeric_limits<std::size_t>::max(), size));
  EXPECT_EQ(size, 19u);
}

TEST(ArgumentBufferMapping, ComputesSuccessfulByteSizesIncludingZero) {
  std::size_t size = 19;
  EXPECT_TRUE(dxmt::ArgumentBufferByteSize<DrawArguments>(0, size));
  EXPECT_EQ(size, 0u);
  EXPECT_TRUE(dxmt::ArgumentBufferByteSize<DrawArguments>(3, size));
  EXPECT_EQ(size, 3u * sizeof(DrawArguments));
}

TEST(ArgumentBufferMapping, InvokesWriterOnlyForAValidContextMapping) {
  FakeArgumentBufferContext context;
  bool writer_called = false;
  EXPECT_FALSE(dxmt::TryWriteMappedArgumentBuffer<DrawArguments>(
      context, 64, [&](DrawArguments &) { writer_called = true; }));
  EXPECT_FALSE(writer_called);

  context.setMappingAvailable(true);
  EXPECT_TRUE(dxmt::TryWriteMappedArgumentBuffer<DrawArguments>(
      context, 64, [&](DrawArguments &arguments) {
        writer_called = true;
        arguments.vertex_count = 7;
      }));
  EXPECT_TRUE(writer_called);
  EXPECT_EQ(context.storage().vertex_count, 7u);

  EXPECT_TRUE((dxmt::TryWriteMappedArgumentBuffer<DrawArguments, true>(
      context, 64,
      [](DrawArguments &arguments) { arguments.instance_count = 5; })));
  EXPECT_TRUE(context.usedComputeCommandEncoder());
  EXPECT_EQ(context.storage().instance_count, 5u);
}
