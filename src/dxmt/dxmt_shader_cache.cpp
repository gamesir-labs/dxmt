#include "dxmt_shader_cache.hpp"
#include "sha1/sha1_util.hpp"
#include "util_env.hpp"
#include "util_string.hpp"

namespace dxmt {

std::string
GetDXMTShaderCacheDirectory() {
  std::string path;
  if (path = env::getEnvVar("DXMT_SHADER_CACHE_PATH"); !path.empty() && path.starts_with("/")) {
    if (!path.ends_with('/'))
      path += "/";
    return path;
  }

  auto exe_path = env::getExePath();
  auto exe_name = env::getExeName();
  if (exe_path.empty())
    exe_path = exe_name;

  auto digest = Sha1HashState::compute(exe_path.data(), exe_path.size()).string();
  return str::format("dxmt/", exe_name, "_", digest.substr(0, 16), "/");
}

ShaderCache &
ShaderCache::getInstance(WMTMetalVersion version) {
  static dxmt::mutex mutex;
  static std::unordered_map<WMTMetalVersion, std::unique_ptr<ShaderCache>> caches;

  std::lock_guard<dxmt::mutex> lock(mutex);
  auto iter = caches.find(version);
  if (iter == caches.end()) {
    auto inserted = caches.insert({version, std::make_unique<ShaderCache>(version)});
    return *inserted.first->second;
  }
  return *iter->second;
}

ShaderCache::ShaderCache(WMTMetalVersion metal_version) {
  if (env::getEnvVar("DXMT_SHADER_CACHE") == "0")
    return;
  std::string path = GetDXMTShaderCacheDirectory();
  path += str::format("shaders_", (unsigned int)metal_version, ".db");
  scache_writer_ = WMT::CacheWriter::alloc_init(path.c_str(), kDXMTShaderCacheVersion);
  scache_reader_ = WMT::CacheReader::alloc_init(path.c_str(), kDXMTShaderCacheVersion);
}

} // namespace dxmt