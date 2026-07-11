#include "builder.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path FindRepoRoot() {
  if (const char *root = std::getenv("DXMT_REPO_ROOT"))
    return std::filesystem::canonical(root);

  auto path = std::filesystem::current_path();
  while (!path.empty()) {
    if (std::filesystem::is_regular_file(path / "meson.build") &&
        std::filesystem::is_directory(path / "tools/dxmt-builder"))
      return path;
    const auto parent = path.parent_path();
    if (parent == path)
      break;
    path = parent;
  }
  throw std::runtime_error("unable to locate the DXMT repository root");
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::vector<std::string> arguments;
    for (int index = 1; index < argc; ++index)
      arguments.emplace_back(argv[index]);
    dxmt::builder::Application application(FindRepoRoot());
    return application.Run(arguments);
  } catch (const std::exception &error) {
    std::cerr << "dxmt-builder: " << error.what() << '\n';
    return 1;
  }
}
