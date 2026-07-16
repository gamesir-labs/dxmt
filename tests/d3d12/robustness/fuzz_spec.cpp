#include <dxmt_test.hpp>

#include "d3d12_test_context.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using dxmt::test::ComPtr;
using dxmt::test::D3D12TestContext;

enum class CommandAction : std::uint8_t {
  Close,
  ResetList,
  ResetAllocator,
};

struct ReplayFailure {
  std::size_t action_index = 0;
  CommandAction action = CommandAction::Close;
  HRESULT expected = S_OK;
  HRESULT actual = S_OK;
};

std::uint32_t NextRandom(std::uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

std::vector<CommandAction> GenerateCommandGrammar(std::uint32_t seed,
                                                  std::size_t action_count) {
  std::vector<CommandAction> actions;
  actions.reserve(action_count);
  for (std::size_t index = 0; index < action_count; ++index) {
    actions.push_back(static_cast<CommandAction>(NextRandom(seed) % 3));
  }
  return actions;
}

const char *CommandActionName(CommandAction action) {
  switch (action) {
  case CommandAction::Close:
    return "close";
  case CommandAction::ResetList:
    return "reset-list";
  case CommandAction::ResetAllocator:
    return "reset-allocator";
  }
  return "unknown";
}

std::string FormatActions(const std::vector<CommandAction> &actions) {
  std::ostringstream output;
  for (std::size_t index = 0; index < actions.size(); ++index) {
    if (index)
      output << ',';
    output << CommandActionName(actions[index]);
  }
  return output.str();
}

template <typename Predicate>
std::vector<CommandAction>
ShrinkFailingSequence(std::vector<CommandAction> actions,
                      Predicate still_fails) {
  if (actions.size() < 2 || !still_fails(actions))
    return actions;

  std::size_t granularity = 2;
  while (actions.size() >= 2) {
    const std::size_t chunk_size =
        (actions.size() + granularity - 1) / granularity;
    bool reduced = false;
    for (std::size_t begin = 0; begin < actions.size(); begin += chunk_size) {
      const std::size_t end = std::min(actions.size(), begin + chunk_size);
      std::vector<CommandAction> candidate;
      candidate.reserve(actions.size() - (end - begin));
      candidate.insert(candidate.end(), actions.begin(),
                       actions.begin() + begin);
      candidate.insert(candidate.end(), actions.begin() + end, actions.end());
      if (!candidate.empty() && still_fails(candidate)) {
        actions = std::move(candidate);
        granularity = 2;
        reduced = true;
        break;
      }
    }
    if (reduced)
      continue;
    if (granularity >= actions.size())
      break;
    granularity = std::min(actions.size(), granularity * 2);
  }
  return actions;
}

std::optional<ReplayFailure>
ReplayCommandGrammar(ID3D12Device *device,
                     const std::vector<CommandAction> &actions) {
  ComPtr<ID3D12CommandAllocator> allocator;
  HRESULT result = device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put()));
  if (FAILED(result))
    return ReplayFailure{0, CommandAction::ResetAllocator, S_OK, result};

  ComPtr<ID3D12GraphicsCommandList> list;
  result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                     allocator.get(), nullptr,
                                     IID_PPV_ARGS(list.put()));
  if (FAILED(result))
    return ReplayFailure{0, CommandAction::ResetList, S_OK, result};

  bool recording = true;
  for (std::size_t index = 0; index < actions.size(); ++index) {
    const auto action = actions[index];
    HRESULT expected = S_OK;
    HRESULT actual = S_OK;
    switch (action) {
    case CommandAction::Close:
      expected = recording ? S_OK : E_FAIL;
      actual = list->Close();
      if (SUCCEEDED(actual))
        recording = false;
      break;
    case CommandAction::ResetList:
      expected = recording ? E_FAIL : S_OK;
      actual = list->Reset(allocator.get(), nullptr);
      if (SUCCEEDED(actual))
        recording = true;
      break;
    case CommandAction::ResetAllocator:
      expected = recording ? E_FAIL : S_OK;
      actual = allocator->Reset();
      break;
    }
    if (actual != expected)
      return ReplayFailure{index, action, expected, actual};
  }
  return std::nullopt;
}

class D3D12CommandGrammarFuzzSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12CommandGrammarFuzzSpec,
       CommandLifecycleMatchesModelAcrossFixedSeeds) {
  std::uint32_t first_seed = 0x13579bdfu;
  std::size_t seed_count = 128;
  if (const char *replay_seed = std::getenv("DXMT_D3D12_FUZZ_SEED")) {
    char *end = nullptr;
    const auto parsed = std::strtoul(replay_seed, &end, 0);
    ASSERT_NE(end, replay_seed) << "invalid DXMT_D3D12_FUZZ_SEED";
    ASSERT_EQ(*end, '\0') << "invalid DXMT_D3D12_FUZZ_SEED";
    first_seed = static_cast<std::uint32_t>(parsed);
    seed_count = 1;
  }

  for (std::size_t seed_index = 0; seed_index < seed_count; ++seed_index) {
    const std::uint32_t seed =
        first_seed + static_cast<std::uint32_t>(seed_index * 0x9e3779b9u);
    const auto actions = GenerateCommandGrammar(seed, 32);
    const auto failure = ReplayCommandGrammar(context_.device(), actions);
    if (!failure)
      continue;

    const auto minimized =
        ShrinkFailingSequence(actions, [&](const auto &candidate) {
          return ReplayCommandGrammar(context_.device(), candidate).has_value();
        });
    ADD_FAILURE() << "seed=0x" << std::hex << seed << std::dec
                  << " action=" << failure->action_index << " ("
                  << CommandActionName(failure->action) << ") expected=0x"
                  << std::hex << static_cast<unsigned long>(failure->expected)
                  << " actual=0x" << static_cast<unsigned long>(failure->actual)
                  << std::dec << " minimized=" << FormatActions(minimized);
    return;
  }
}

TEST_F(D3D12CommandGrammarFuzzSpec, TimeBudgetedStressSeedsWhenRequested) {
  const char *budget_text = std::getenv("DXMT_D3D12_FUZZ_STRESS_MILLISECONDS");
  if (!budget_text)
    GTEST_SKIP() << "set DXMT_D3D12_FUZZ_STRESS_MILLISECONDS for stress mode";
  char *end = nullptr;
  const auto budget = std::strtoul(budget_text, &end, 10);
  ASSERT_NE(end, budget_text);
  ASSERT_EQ(*end, '\0');
  ASSERT_GT(budget, 0u);

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);
  std::uint32_t seed = 0x9e3779b9u;
  std::size_t replay_count = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const std::size_t action_count = 20 + NextRandom(seed) % 481;
    const auto actions = GenerateCommandGrammar(seed, action_count);
    const auto failure = ReplayCommandGrammar(context_.device(), actions);
    ASSERT_FALSE(failure.has_value())
        << "seed=0x" << std::hex << seed << std::dec
        << " actions=" << action_count;
    seed += 0x9e3779b9u;
    ++replay_count;
  }
  EXPECT_GT(replay_count, 0u);
}

TEST(D3D12FailureShrinkerSpec, DeltaDebuggingKeepsMinimalTrigger) {
  const std::vector<CommandAction> input = {
      CommandAction::ResetAllocator, CommandAction::Close,
      CommandAction::ResetList,      CommandAction::Close,
      CommandAction::ResetAllocator, CommandAction::ResetList,
  };
  const auto requires_close_reset_close = [](const auto &actions) {
    std::size_t state = 0;
    for (const auto action : actions) {
      if (state == 0 && action == CommandAction::Close)
        state = 1;
      else if (state == 1 && action == CommandAction::ResetList)
        state = 2;
      else if (state == 2 && action == CommandAction::Close)
        return true;
    }
    return false;
  };

  const auto minimized =
      ShrinkFailingSequence(input, requires_close_reset_close);
  EXPECT_EQ(minimized, (std::vector<CommandAction>{CommandAction::Close,
                                                   CommandAction::ResetList,
                                                   CommandAction::Close}));
}

struct ScenarioSize {
  std::size_t buffer_size;
  std::size_t texture_width;
  std::size_t texture_height;
  std::size_t resource_count;
  std::size_t descriptor_count;
  std::size_t queue_count;
  std::size_t barrier_count;
};

template <typename Predicate>
ScenarioSize ShrinkScenarioSizes(ScenarioSize sizes, Predicate still_fails) {
  auto shrink_dimension = [&](std::size_t ScenarioSize::*field) {
    while (sizes.*field > 1) {
      ScenarioSize candidate = sizes;
      candidate.*field = std::max<std::size_t>(1, candidate.*field / 2);
      if (!still_fails(candidate))
        break;
      sizes = candidate;
    }
  };
  shrink_dimension(&ScenarioSize::resource_count);
  shrink_dimension(&ScenarioSize::descriptor_count);
  shrink_dimension(&ScenarioSize::queue_count);
  shrink_dimension(&ScenarioSize::barrier_count);
  shrink_dimension(&ScenarioSize::texture_width);
  shrink_dimension(&ScenarioSize::texture_height);
  shrink_dimension(&ScenarioSize::buffer_size);
  return sizes;
}

TEST(D3D12FailureShrinkerSpec, ReducesResourcesDescriptorsQueuesAndDimensions) {
  const ScenarioSize input = {4096, 128, 64, 8, 64, 4, 32};
  const auto minimized = ShrinkScenarioSizes(input, [](const ScenarioSize &s) {
    return s.resource_count >= 2 && s.descriptor_count >= 4 &&
           s.queue_count >= 2 && s.barrier_count >= 2 && s.texture_width >= 4 &&
           s.texture_height >= 2 && s.buffer_size >= 16;
  });
  EXPECT_EQ(minimized.buffer_size, 16u);
  EXPECT_EQ(minimized.texture_width, 4u);
  EXPECT_EQ(minimized.texture_height, 2u);
  EXPECT_EQ(minimized.resource_count, 2u);
  EXPECT_EQ(minimized.descriptor_count, 4u);
  EXPECT_EQ(minimized.queue_count, 2u);
  EXPECT_EQ(minimized.barrier_count, 2u);
}

class D3D12RootSignatureFuzzSpec : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(context_.Initialize(), S_OK); }

  D3D12TestContext context_;
};

TEST_F(D3D12RootSignatureFuzzSpec,
       GeneratedVersion10CorpusRoundTripsAndCreates) {
  std::uint32_t random = 0xa5a5f00du;
  for (std::uint32_t case_index = 0; case_index < 128; ++case_index) {
    SCOPED_TRACE(::testing::Message() << "case=" << case_index);
    const auto bits = NextRandom(random);

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = static_cast<D3D12_DESCRIPTOR_RANGE_TYPE>((bits >> 3) % 4);
    range.NumDescriptors = 1 + ((bits >> 7) % 16);
    range.BaseShaderRegister = (bits >> 11) % 16;
    range.RegisterSpace = (bits >> 15) % 4;
    range.OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameter = {};
    parameter.ParameterType = static_cast<D3D12_ROOT_PARAMETER_TYPE>(bits % 5);
    parameter.ShaderVisibility =
        static_cast<D3D12_SHADER_VISIBILITY>((bits >> 20) % 6);
    if (parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
      parameter.DescriptorTable.NumDescriptorRanges = 1;
      parameter.DescriptorTable.pDescriptorRanges = &range;
    } else if (parameter.ParameterType ==
               D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
      parameter.Constants.ShaderRegister = (bits >> 6) % 16;
      parameter.Constants.RegisterSpace = (bits >> 10) % 4;
      parameter.Constants.Num32BitValues = 1 + ((bits >> 12) % 32);
    } else {
      parameter.Descriptor.ShaderRegister = (bits >> 6) % 16;
      parameter.Descriptor.RegisterSpace = (bits >> 10) % 4;
    }

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = (bits >> 24) % 16;
    sampler.RegisterSpace = (bits >> 28) % 4;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;
    if (case_index & 1) {
      desc.NumStaticSamplers = 1;
      desc.pStaticSamplers = &sampler;
    }

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    ASSERT_EQ(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1_0,
                                          blob.put(), error.put()),
              S_OK)
        << (error ? static_cast<const char *>(error->GetBufferPointer()) : "");
    ASSERT_TRUE(blob);

    ComPtr<ID3D12RootSignatureDeserializer> deserializer;
    ASSERT_EQ(D3D12CreateRootSignatureDeserializer(
                  blob->GetBufferPointer(), blob->GetBufferSize(),
                  IID_PPV_ARGS(deserializer.put())),
              S_OK);
    const auto *round_trip = deserializer->GetRootSignatureDesc();
    ASSERT_NE(round_trip, nullptr);
    ASSERT_EQ(round_trip->NumParameters, 1u);
    EXPECT_EQ(round_trip->pParameters[0].ParameterType,
              parameter.ParameterType);
    EXPECT_EQ(round_trip->pParameters[0].ShaderVisibility,
              parameter.ShaderVisibility);
    EXPECT_EQ(round_trip->NumStaticSamplers, desc.NumStaticSamplers);

    ComPtr<ID3D12RootSignature> runtime_signature;
    EXPECT_EQ(context_.device()->CreateRootSignature(
                  0, blob->GetBufferPointer(), blob->GetBufferSize(),
                  IID_PPV_ARGS(runtime_signature.put())),
              S_OK);
    EXPECT_TRUE(runtime_signature);
  }
}

} // namespace
