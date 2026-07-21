// Drives Wine's own d3d9 conformance modules, the mature reference suite this
// frontend is measured against.
//
// Same envelope as the shader oracle next to it: spawn the module as a separate
// process, bound the wait, and gate on a baseline diff. The reasons differ in
// one way worth stating. Wine's harness exits with a FAILURE COUNT capped at
// 255, so a nonzero status carries no more information than "some assertions
// failed" and cannot be a gate on a corpus that is not yet fully green. What
// distinguishes a crash from failures is the absence of the trailing summary
// line, exactly as for the shader oracle.
//
// The modules run with WINETEST_PLATFORM=windows on every platform. Wine's
// todo_wine marks record where Wine itself is known to deviate, and a todo
// block that SUCCEEDS is counted as a failure, so running as "wine" would score
// this frontend being more correct than Wine as a regression. Holding the
// platform to windows keeps the assertions at their strict meaning and lets the
// baseline carry what is not yet met.

#include <dxmt_test.hpp>

#include <support/wine_process.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr const char *kWineModules[] = {
    "stateblock",
};

constexpr DWORD kDefaultTimeoutMs = 600000;

DWORD TimeoutMs() {
  if (const char *value = std::getenv("DXMT_D3D9_WINE_TIMEOUT_MS")) {
    const auto parsed = std::strtoul(value, nullptr, 10);
    if (parsed != 0)
      return static_cast<DWORD>(parsed);
  }
  return kDefaultTimeoutMs;
}

bool RuntimeRequired() {
  const char *value = std::getenv("DXMT_TEST_REQUIRE_RUNTIME");
  return value != nullptr && value[0] == '1';
}

std::wstring ExecutableDirectory() {
  auto path = dxmt::test::WineExecutablePath();
  const auto separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos)
    return L".";
  return path.substr(0, separator);
}

bool PathExists(const std::wstring &path) {
  return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring ModulePath(const char *module) {
  const auto directory = ExecutableDirectory();
  const auto name =
      L"dx9-wine-" + dxmt::test::WidenWineArgument(module) + L"-test.exe";
  for (const auto &candidate :
       {directory + L"\\" + name, directory + L"\\d3d9\\" + name})
    if (PathExists(candidate))
      return candidate;
  return {};
}

std::vector<std::string> LoadBaseline(const char *module) {
  std::vector<std::string> entries;
  const auto directory = ExecutableDirectory();
  std::wstring path;
  for (const auto &candidate :
       {directory + L"\\wine_conformance_baseline.txt",
        directory + L"\\d3d9\\wine_conformance_baseline.txt"})
    if (PathExists(candidate)) {
      path = candidate;
      break;
    }
  if (path.empty())
    return entries;

  // Entries are "<module> <substring>", so one file covers every module and a
  // baselined failure cannot leak across modules.
  const std::string prefix = std::string(module) + " ";
  std::ifstream file(path.c_str());
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty() || line.front() == '#')
      continue;
    if (line.compare(0, prefix.size(), prefix) == 0)
      entries.push_back(line.substr(prefix.size()));
  }
  return entries;
}

struct RunResult {
  bool started = false;
  bool timed_out = false;
  DWORD exit_code = 0;
  std::string output;
};

RunResult RunModule(const std::wstring &executable, const char *module) {
  RunResult result;
  std::vector<char> temp(MAX_PATH + 1);
  const DWORD size = GetTempPathA(static_cast<DWORD>(temp.size()), temp.data());
  std::string log_path(temp.data(), size);
  log_path += "dxmt-d3d9-wine-" + std::to_string(GetCurrentProcessId()) + "-" +
              module + ".log";

  SECURITY_ATTRIBUTES attributes = {};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE log = CreateFileA(log_path.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (log == INVALID_HANDLE_VALUE)
    return result;

  DWORD error = 0;
  auto process = dxmt::test::StartWineProcess(executable, {module}, &error, log);
  if (!process) {
    CloseHandle(log);
    DeleteFileA(log_path.c_str());
    return result;
  }
  result.started = true;

  if (WaitForSingleObject(process->handle, TimeoutMs()) == WAIT_TIMEOUT) {
    result.timed_out = true;
    TerminateProcess(process->handle, 1);
    WaitForSingleObject(process->handle, INFINITE);
  }
  GetExitCodeProcess(process->handle, &result.exit_code);
  CloseHandle(process->handle);
  CloseHandle(log);

  std::ifstream file(log_path.c_str());
  result.output.assign(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
  DeleteFileA(log_path.c_str());
  return result;
}

bool ReachedSummary(const std::string &output) {
  return output.find("tests executed") != std::string::npos;
}

// Every failing form routes through the same context printer, so each carries
// its file and line. A todo block that succeeds is included deliberately: it
// means the assertion now passes where Wine records a known deviation, which is
// a result worth surfacing rather than discarding.
std::vector<std::string> FailureLines(const std::string &output) {
  static const char *kMarkers[] = {
      "Test failed:",
      "Test succeeded inside todo block:",
      "Test marked flaky:",
      "Test succeeded inside flaky todo block:",
  };
  std::vector<std::string> failures;
  std::size_t begin = 0;
  while (begin < output.size()) {
    auto end = output.find('\n', begin);
    if (end == std::string::npos)
      end = output.size();
    std::string line = output.substr(begin, end - begin);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    for (const auto *marker : kMarkers)
      if (line.find(marker) != std::string::npos) {
        failures.push_back(line);
        break;
      }
    begin = end + 1;
  }
  return failures;
}

bool IsBaselined(const std::string &failure,
                 const std::vector<std::string> &baseline) {
  for (const auto &entry : baseline)
    if (failure.find(entry) != std::string::npos)
      return true;
  return false;
}

std::string Tail(const std::string &output, std::size_t lines) {
  std::size_t position = output.size();
  for (std::size_t i = 0; i < lines && position != 0; ++i) {
    const auto previous = output.find_last_of('\n', position - 1);
    if (previous == std::string::npos)
      return output;
    position = previous;
  }
  return output.substr(position);
}

class WineConformanceTest : public testing::TestWithParam<const char *> {};

TEST_P(WineConformanceTest, MatchesBaseline) {
  const char *module = GetParam();

  // Not having the corpus at all is a build configuration, not a defect, so it
  // stays a skip even in a staged run. Having been built and then not found is
  // a staging failure and is reported as one.
#ifndef DXMT_D3D9_WINE_TESTS_BUILT
  GTEST_SKIP() << "Wine d3d9 conformance modules were not built; point "
                  "wine_source_path at a Wine source tree (the external/wine "
                  "submodule) to enable them";
#else
  const auto executable = ModulePath(module);
  if (executable.empty()) {
    if (RuntimeRequired())
      FAIL() << "Wine d3d9 conformance module " << module
             << " was built but is not next to the test image";
    GTEST_SKIP() << "Wine d3d9 conformance module " << module << " not found";
  }

  const auto result = RunModule(executable, module);
  ASSERT_TRUE(result.started) << "failed to start " << module;
  ASSERT_FALSE(result.timed_out)
      << module << " timed out after " << TimeoutMs() << " ms\n"
      << Tail(result.output, 20);
  ASSERT_TRUE(ReachedSummary(result.output))
      << module
      << " produced no summary line, so it did not finish; the exit status is a "
         "failure count and cannot distinguish this (exit "
      << result.exit_code << ")\n"
      << Tail(result.output, 20);

  const auto baseline = LoadBaseline(module);
  std::vector<std::string> unexpected;
  for (const auto &failure : FailureLines(result.output))
    if (!IsBaselined(failure, baseline))
      unexpected.push_back(failure);

  std::string report;
  for (const auto &failure : unexpected)
    report += "  " + failure + "\n";
  EXPECT_TRUE(unexpected.empty())
      << unexpected.size() << " assertion failure(s) not in the baseline for "
      << module << ":\n"
      << report;
#endif
}

INSTANTIATE_TEST_SUITE_P(Wine, WineConformanceTest,
                         testing::ValuesIn(kWineModules),
                         [](const testing::TestParamInfo<const char *> &info) {
                           return std::string(info.param);
                         });

} // namespace
