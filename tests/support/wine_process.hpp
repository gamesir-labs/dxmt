#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dxmt::test {

struct WineProcess {
  HANDLE handle = nullptr;
  DWORD id = 0;
};

inline std::wstring WineExecutablePath() {
  std::vector<wchar_t> buffer(1024);
  while (true) {
    const DWORD size =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0)
      return {};
    if (size < buffer.size() - 1)
      return std::wstring(buffer.data(), size);
    buffer.resize(buffer.size() * 2);
  }
}

inline std::wstring WidenWineArgument(std::string_view value) {
  if (value.empty())
    return {};

  UINT code_page = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  int size = MultiByteToWideChar(code_page, flags, value.data(),
                                 static_cast<int>(value.size()), nullptr, 0);
  if (size == 0) {
    code_page = CP_ACP;
    flags = 0;
    size = MultiByteToWideChar(code_page, flags, value.data(),
                               static_cast<int>(value.size()), nullptr, 0);
  }
  if (size == 0)
    return {};

  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(code_page, flags, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

inline std::wstring QuoteWineArgument(std::wstring_view argument) {
  if (argument.empty())
    return L"\"\"";
  if (argument.find_first_of(L" \t\"") == std::wstring_view::npos)
    return std::wstring(argument);

  std::wstring result = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'\"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(character);
      backslashes = 0;
      continue;
    }
    result.append(backslashes, L'\\');
    backslashes = 0;
    result.push_back(character);
  }
  result.append(backslashes * 2, L'\\');
  result.push_back(L'\"');
  return result;
}

inline std::wstring
BuildWineCommandLine(std::wstring_view executable,
                     const std::vector<std::string> &arguments) {
  std::wstring command_line = QuoteWineArgument(executable);
  for (const auto &argument : arguments) {
    command_line.push_back(L' ');
    command_line += QuoteWineArgument(WidenWineArgument(argument));
  }
  return command_line;
}

inline std::string WineErrorMessage(DWORD error) {
  char *buffer = nullptr;
  const DWORD size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<char *>(&buffer), 0, nullptr);
  if (size == 0 || buffer == nullptr)
    return "Win32 error " + std::to_string(error);

  std::string message(buffer, size);
  LocalFree(buffer);
  while (!message.empty() &&
         (message.back() == '\r' || message.back() == '\n'))
    message.pop_back();
  return message;
}

inline std::optional<WineProcess>
StartWineProcess(std::wstring_view executable,
                 const std::vector<std::string> &arguments, DWORD *error,
                 HANDLE output = nullptr) {
  const std::wstring executable_path(executable);
  auto command_line = BuildWineCommandLine(executable, arguments);
  STARTUPINFOW startup_info = {};
  startup_info.cb = sizeof(startup_info);
  if (output != nullptr) {
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = output;
    startup_info.hStdError = output;
  }

  PROCESS_INFORMATION process_info = {};
  if (!CreateProcessW(executable_path.c_str(), command_line.data(), nullptr,
                      nullptr, output != nullptr, 0, nullptr, nullptr,
                      &startup_info, &process_info)) {
    *error = GetLastError();
    return std::nullopt;
  }

  CloseHandle(process_info.hThread);
  return WineProcess{process_info.hProcess, process_info.dwProcessId};
}

} // namespace dxmt::test
