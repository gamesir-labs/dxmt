#include "dxil_converter.hpp"

#include "airconv_context.hpp"
#include "airconv_error.hpp"
#include "airconv_internal.hpp"
#include "dxil_air_converter.hpp"
#include "metallib_writer.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

class DXILShaderInternal {
public:
  std::vector<uint8_t> bytecode;
  dxmt::dxil::Parser parser;
};

int
DXILFail(sm50_error_t *ppError, const std::string &message) {
  if (ppError) {
    auto error = std::make_unique<SM50ErrorInternal>();
    llvm::raw_svector_ostream stream(error->buf);
    stream << message;
    *ppError = sm50_error_t(error.release());
  }
  return 1;
}

} // namespace

AIRCONV_API int
DXILInitialize(const void *pBytecode, size_t BytecodeSize,
               dxil_shader_t *ppShader, MTL_SHADER_REFLECTION *pRefl,
               sm50_error_t *ppError) {
  if (ppError)
    *ppError = nullptr;
  if (ppShader)
    *ppShader = nullptr;

  if (!ppShader)
    return DXILFail(ppError, "ppShader can not be null");
  if (!pBytecode && BytecodeSize)
    return DXILFail(ppError, "pBytecode can not be null");

  auto shader = std::make_unique<DXILShaderInternal>();
  shader->bytecode.resize(BytecodeSize);
  if (BytecodeSize)
    std::memcpy(shader->bytecode.data(), pBytecode, BytecodeSize);

  const auto status = shader->parser.parse(shader->bytecode.data(),
                                           shader->bytecode.size());
  if (status != dxmt::dxil::ParseStatus::Ok) {
    return DXILFail(ppError, std::string("Invalid DXIL bytecode: ") +
                                dxmt::dxil::StatusName(status));
  }

  dxmt::airconv::FillDxilReflection(shader->parser, pRefl);
  *ppShader = dxil_shader_t(shader.release());
  return 0;
}

AIRCONV_API void
DXILDestroy(dxil_shader_t pShader) {
  delete (DXILShaderInternal *)pShader;
}

AIRCONV_API int
DXILCompile(dxil_shader_t pShader, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs,
            const char *FunctionName, sm50_bitcode_t *ppBitcode,
            sm50_error_t *ppError) {
  if (ppError)
    *ppError = nullptr;
  if (ppBitcode)
    *ppBitcode = nullptr;

  if (!pShader)
    return DXILFail(ppError, "pShader can not be null");
  if (!ppBitcode)
    return DXILFail(ppError, "ppBitcode can not be null");

  llvm::LLVMContext context;
  context.setOpaquePointers(false);

  auto module = std::make_unique<llvm::Module>("dxil.air", context);
  dxmt::initializeModule(*module);

  auto *shader = (DXILShaderInternal *)pShader;
  if (auto err = dxmt::airconv::ConvertDxilToAir(
          shader->parser, FunctionName, context, *module, pArgs)) {
    std::string message;
    llvm::raw_string_ostream stream(message);
    llvm::handleAllErrors(std::move(err), [&](const dxmt::UnsupportedFeature &unsupported) {
      stream << unsupported.msg;
    });
    return DXILFail(ppError, stream.str());
  }

  dxmt::runOptimizationPasses(*module);

  auto compiled = std::make_unique<SM50CompiledBitcodeInternal>();
  llvm::raw_svector_ostream out(compiled->vec);
  dxmt::metallib::MetallibWriter writer;
  writer.Write(*module, out);
  *ppBitcode = sm50_bitcode_t(compiled.release());
  return 0;
}

namespace dxmt::airconv {

const dxil::DxilTranslationInfo *
GetDxilTranslationInfo(dxil_shader_t pShader) {
  if (!pShader)
    return nullptr;

  const auto *shader = (const DXILShaderInternal *)pShader;
  const auto &translation = shader->parser.dxilTranslation();
  return translation ? &*translation : nullptr;
}

} // namespace dxmt::airconv
