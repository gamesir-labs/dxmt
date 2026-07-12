#pragma once

#include <dxmt_test_com.hpp>

#include <d3dcompiler.h>

#include <string_view>

namespace dxmt::test {

struct ShaderCompilation {
  HRESULT result = E_FAIL;
  ComPtr<ID3DBlob> bytecode;
  ComPtr<ID3DBlob> diagnostics;

  std::string_view diagnostic_text() const {
    return diagnostics ? std::string_view(static_cast<const char *>(
                                              diagnostics->GetBufferPointer()),
                                          diagnostics->GetBufferSize())
                       : std::string_view();
  }
};

inline ShaderCompilation CompileShader(std::string_view source,
                                       const char *target) {
  ShaderCompilation compilation;
  compilation.result = D3DCompile(
      source.data(), source.size(), nullptr, nullptr, nullptr, "main", target,
      D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
      compilation.bytecode.put(), compilation.diagnostics.put());
  return compilation;
}

} // namespace dxmt::test
