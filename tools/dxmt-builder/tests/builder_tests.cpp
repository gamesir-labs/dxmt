#include "builder.hpp"
#include "sha256.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

void Check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

} // namespace

int main() {
  try {
    using namespace dxmt::builder;
    Check(Sha256("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty SHA-256 vector failed");
    Check(Sha256("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc SHA-256 vector failed");
    Check(Profiles().size() == 4, "unexpected profile count");
    Check(FindProfile("gcc-x64-release-full") != nullptr,
          "default profile missing");
    Check(FindProfile("apple-clang-x86_64-release") != nullptr,
          "native x86_64 profile missing");
    Check(FindProfile("gcc-x86-release") == nullptr,
          "native i386 DXMT profile must not be registered");
    for (const auto &profile : Profiles())
      Check(profile.target_arch == "x64" || profile.target_arch == "x86_64",
            "only x64 PE / native x86_64 profiles are supported");
    Check(FindProfile("not-a-profile") == nullptr,
          "unknown profile was accepted");
    Check(JsonEscape("a\n\"b") == "a\\n\\\"b", "JSON escaping failed");
    Check(IsPathWithin("/tmp/root/child", "/tmp/root"),
          "managed child was rejected");
    Check(!IsPathWithin("/tmp/root-other", "/tmp/root"),
          "outside path was accepted");
    const auto properties = testing::ParseProperties(
        "# metadata\nschema=1\nprofile=gcc-x64-release-full\n");
    Check(properties.at("schema") == "1" &&
              properties.at("profile") == "gcc-x64-release-full",
          "metadata parsing failed");
    const auto config = testing::ParseJsonStringObject(
        "{\n  \"cache_root\": \"../cache\",\n"
        "  \"profile_namespace\": \"feature\\/cache\"\n}\n");
    Check(config.at("cache_root") == "../cache" &&
              config.at("profile_namespace") == "feature/cache",
          "builder config JSON parsing failed");
    bool rejected_non_string = false;
    try {
      static_cast<void>(testing::ParseJsonStringObject(
          "{\"cache_root\": 1}"));
    } catch (const std::runtime_error &) {
      rejected_non_string = true;
    }
    Check(rejected_non_string, "non-string builder config value was accepted");

    const auto temp = std::filesystem::temp_directory_path() /
                      ("dxmt-builder-tests-" + std::to_string(getpid()));
    std::filesystem::remove_all(temp);
    const auto repo = temp / "parent/worktree";
    std::filesystem::create_directories(repo / ".dxmt-builder");
    testing::WriteFileAtomic(temp / "parent/.dxmt-builder/config.json", "{}");
    Check(!testing::DiscoverConfigPath(repo),
          "parent worktree config leaked into the current checkout");
    const auto local_config = repo / ".dxmt-builder/config.json";
    testing::WriteFileAtomic(local_config, "{}");
    Check(testing::DiscoverConfigPath(repo) ==
              std::filesystem::absolute(local_config).lexically_normal(),
          "checkout-local builder config was not discovered");
    const auto explicit_config = temp / "explicit-config.json";
    testing::WriteFileAtomic(explicit_config, "{}");
    Check(testing::DiscoverConfigPath(repo, explicit_config) ==
              std::filesystem::absolute(explicit_config).lexically_normal(),
          "explicit builder config did not override the checkout default");

    const auto atomic_file = temp / "atomic.txt";
    testing::WriteFileAtomic(atomic_file, "complete");
    Check(ReadFile(atomic_file) == "complete", "atomic publication failed");

    const auto counter = temp / "counter.txt";
    testing::WriteFileAtomic(counter, "0");
    std::vector<pid_t> children;
    for (int index = 0; index < 2; ++index) {
      const auto child = fork();
      Check(child >= 0, "fork failed");
      if (child == 0) {
        testing::WithFileLock(temp / "counter.lock", [&] {
          const auto value = std::stoi(ReadFile(counter));
          usleep(50000);
          testing::WriteFileAtomic(counter, std::to_string(value + 1));
        });
        _exit(0);
      }
      children.push_back(child);
    }
    for (const auto child : children) {
      int status = 0;
      Check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                WEXITSTATUS(status) == 0,
            "locked child failed");
    }
    Check(ReadFile(counter) == "2", "file lock did not serialize writers");
    std::filesystem::remove_all(temp);
    std::cout << "dxmt-builder tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "dxmt-builder test failure: " << error.what() << '\n';
    return 1;
  }
}
