#pragma once

#include "DXILParser/DXILParser.hpp"
#include "airconv_public.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"

namespace dxmt::dxbc {
class ShaderInfo;
}

namespace dxmt::airconv {

llvm::Error ConvertDxilToAir(const dxil::Parser &parser, const char *name,
                             llvm::LLVMContext &context,
                             llvm::Module &module,
                             SM50_SHADER_COMPILATION_ARGUMENT_DATA *args);

void FillDxilReflection(const dxil::Parser &parser,
                        MTL_SHADER_REFLECTION *reflection);

void BuildDxilShaderInfo(const dxil::DxilTranslationInfo &translation,
                         dxbc::ShaderInfo &shader_info);

} // namespace dxmt::airconv
