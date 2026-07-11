#include <dxmt_test.hpp>

#include "dxmt_command_list.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace {

struct CommandContext {
  std::vector<int> values;
};

struct CommandStorage {
  alignas(16) std::array<std::byte, 128> bytes{};

  void *data() { return bytes.data(); }
};

TEST(CommandList, ExecutesCommandsInInsertionOrderAndResetsLifetime) {
  dxmt::CommandList<CommandContext> list;
  CommandStorage first_storage;
  CommandStorage second_storage;
  CommandContext context;

  auto lifetime = std::make_shared<int>(7);
  std::weak_ptr<int> observed_lifetime = lifetime;
  auto first = [lifetime = std::move(lifetime)](CommandContext &target) {
    target.values.push_back(*lifetime);
  };
  const auto first_size = list.calculateCommandSize<decltype(first)>();
  EXPECT_LE(first_size, first_storage.bytes.size());
  EXPECT_EQ(list.emit(std::move(first), first_storage.data()), first_size);
  list.emit([](CommandContext &target) { target.values.push_back(11); },
            second_storage.data());

  list.execute(context);
  EXPECT_EQ(context.values, (std::vector<int>{7, 11}));
  EXPECT_FALSE(observed_lifetime.expired());

  list.reset();
  EXPECT_TRUE(observed_lifetime.expired());
  context.values.clear();
  list.execute(context);
  EXPECT_TRUE(context.values.empty());
}

TEST(CommandList, MovingAnEmptyListKeepsDestinationReusable) {
  dxmt::CommandList<CommandContext> source;
  dxmt::CommandList<CommandContext> destination(std::move(source));
  CommandStorage source_storage;
  CommandStorage destination_storage;
  CommandContext context;

  destination.emit([](CommandContext &target) { target.values.push_back(3); },
                   destination_storage.data());
  source.emit([](CommandContext &target) { target.values.push_back(5); },
              source_storage.data());

  destination.execute(context);
  source.execute(context);
  EXPECT_EQ(context.values, (std::vector<int>{3, 5}));
}

TEST(CommandList, MoveAssignmentTransfersCommandsAndHandlesSelfMove) {
  dxmt::CommandList<CommandContext> source;
  dxmt::CommandList<CommandContext> destination;
  CommandStorage source_storage;
  CommandStorage replacement_storage;
  CommandContext context;

  source.emit([](CommandContext &target) { target.values.push_back(13); },
              source_storage.data());
  destination = std::move(source);
  auto *same_list = &destination;
  destination = std::move(*same_list);
  destination.execute(context);
  EXPECT_EQ(context.values, (std::vector<int>{13}));

  source.emit([](CommandContext &target) { target.values.push_back(17); },
              replacement_storage.data());
  source.execute(context);
  EXPECT_EQ(context.values, (std::vector<int>{13, 17}));
}

TEST(CommandList, AppendPreservesOrderAndIgnoresEmptyLists) {
  dxmt::CommandList<CommandContext> destination;
  dxmt::CommandList<CommandContext> source;
  dxmt::CommandList<CommandContext> empty;
  CommandStorage first_storage;
  CommandStorage second_storage;
  CommandStorage third_storage;
  CommandContext context;

  destination.emit([](CommandContext &target) { target.values.push_back(19); },
                   first_storage.data());
  source.emit([](CommandContext &target) { target.values.push_back(23); },
              second_storage.data());
  destination.append(std::move(source));
  destination.append(std::move(empty));
  destination.emit([](CommandContext &target) { target.values.push_back(29); },
                   third_storage.data());

  destination.execute(context);
  EXPECT_EQ(context.values, (std::vector<int>{19, 23, 29}));

  context.values.clear();
  source.execute(context);
  empty.execute(context);
  EXPECT_TRUE(context.values.empty());
}

} // namespace
