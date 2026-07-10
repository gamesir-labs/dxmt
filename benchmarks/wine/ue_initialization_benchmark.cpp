#include <dxmt_benchmark.hpp>

#include "wine_process.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct InitializationWorker {
  std::string_view name;
  std::wstring_view executable;
};

constexpr std::array<InitializationWorker, 2> kInitializationWorkers = {{
    {"D3D11", L"dxmt-ue-d3d11-initialization.exe"},
    {"D3D12", L"dxmt-ue-d3d12-initialization.exe"},
}};

std::wstring ExecutableDirectory() {
  std::wstring path = dxmt::test::WineExecutablePath();
  if (path.empty())
    return {};
  const auto separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos)
    return L".";
  return path.substr(0, separator);
}

std::optional<std::string>
RunInitializationWorker(const InitializationWorker &worker,
                        std::wstring_view directory) {
  std::wstring executable(directory);
  if (!executable.empty() && executable.back() != L'\\' &&
      executable.back() != L'/')
    executable.push_back(L'\\');
  executable.append(worker.executable);

  DWORD error = ERROR_SUCCESS;
  const std::vector<std::string> arguments;
  auto process = dxmt::test::StartWineProcess(executable, arguments, &error);
  if (!process) {
    return std::string(worker.name) +
           " initialization worker failed to start: " +
           dxmt::test::WineErrorMessage(error);
  }

  const DWORD wait_result = WaitForSingleObject(process->handle, INFINITE);
  if (wait_result != WAIT_OBJECT_0) {
    const DWORD wait_error = GetLastError();
    CloseHandle(process->handle);
    return std::string(worker.name) + " initialization worker wait failed: " +
           dxmt::test::WineErrorMessage(wait_error);
  }

  DWORD exit_code = 1;
  if (!GetExitCodeProcess(process->handle, &exit_code)) {
    const DWORD exit_error = GetLastError();
    CloseHandle(process->handle);
    return std::string(worker.name) +
           " initialization worker exit-code query failed: " +
           dxmt::test::WineErrorMessage(exit_error);
  }
  CloseHandle(process->handle);

  if (exit_code != 0) {
    return std::string(worker.name) +
           " initialization worker exited with status " +
           std::to_string(exit_code);
  }

  return std::nullopt;
}

void BI_UnrealEngineRHIInitialization(benchmark::State &state) {
  const std::wstring directory = ExecutableDirectory();
  if (directory.empty()) {
    state.SkipWithError("failed to resolve benchmark executable directory");
    return;
  }

  for (auto _ : state) {
    for (const InitializationWorker &worker : kInitializationWorkers) {
      if (auto error = RunInitializationWorker(worker, directory)) {
        state.SkipWithError(error->c_str());
        return;
      }
    }
  }
}

BENCHMARK(BI_UnrealEngineRHIInitialization)->Iterations(1)->UseRealTime();

} // namespace
