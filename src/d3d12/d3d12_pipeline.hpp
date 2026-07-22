#pragma once

#include "d3d12_root_signature.hpp"
#include "Metal.hpp"
#include "DXILParser/DXILParser.hpp"
#include "airconv_dx12_metal4.h"
#include <d3d12.h>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dxmt::d3d12 {

class PipelineNativeArtifactCache;

struct PipelineNativeArtifactCacheStats {
  uint64_t hits = 0;
  uint64_t misses = 0;
  uint64_t waits = 0;
  uint64_t compiles = 0;
  uint64_t compile_failures = 0;
};

std::shared_ptr<PipelineNativeArtifactCache>
CreatePipelineNativeArtifactCache();
PipelineNativeArtifactCacheStats
GetPipelineNativeArtifactCacheStats(PipelineNativeArtifactCache *cache);

enum class PipelineStateType {
  Graphics,
  Compute,
};

enum class PipelineShaderStage {
  Vertex,
  Pixel,
  Geometry,
  Hull,
  Domain,
  Compute,
};

enum class NativeShaderAbiEligibilityReason {
  None,
  UnsupportedRootSignature,
  UnsupportedDescriptorRange,
  UnsupportedRootDescriptor,
  UnsupportedGeometryPipeline,
  UnsupportedTessellationPipeline,
  ShaderAbiMismatch,
};

enum class PipelineShaderBytecodeKind {
  Unknown,
  Dxbc,
  Dxil,
};

struct PipelineCachedShader {
  PipelineShaderBytecodeKind kind = PipelineShaderBytecodeKind::Unknown;
  std::vector<uint8_t> bytecode;
  dxil::Parser parser;
  DXMT12_MTL4_SHADER_REFLECTION reflection = {};
  std::vector<DXMT12_MTL4_SHADER_ARGUMENT> argument_info;
};

struct PipelineDxilShader {
  PipelineShaderStage stage = PipelineShaderStage::Vertex;
  std::shared_ptr<PipelineCachedShader> cached_shader;
  dxmt12_airconv_shader_t shader = nullptr;

  PipelineDxilShader() = default;
  ~PipelineDxilShader() {
    destroyShader();
  }

  PipelineDxilShader(const PipelineDxilShader &) = delete;
  PipelineDxilShader &operator=(const PipelineDxilShader &) = delete;

  PipelineDxilShader(PipelineDxilShader &&other) noexcept {
    *this = std::move(other);
  }

  PipelineDxilShader &operator=(PipelineDxilShader &&other) noexcept {
    if (this == &other)
      return *this;
    destroyShader();
    stage = other.stage;
    cached_shader = std::move(other.cached_shader);
    shader = std::exchange(other.shader, nullptr);
    return *this;
  }

  PipelineShaderBytecodeKind kind() const {
    return cached_shader ? cached_shader->kind : PipelineShaderBytecodeKind::Unknown;
  }

  const std::vector<uint8_t> &bytecode() const {
    static const std::vector<uint8_t> empty;
    return cached_shader ? cached_shader->bytecode : empty;
  }

  dxmt12_airconv_shader_t shaderHandle() const {
    return shader;
  }

  const DXMT12_MTL4_SHADER_REFLECTION &reflection() const {
    static const DXMT12_MTL4_SHADER_REFLECTION empty = {};
    return cached_shader ? cached_shader->reflection : empty;
  }

  const dxil::DxilTranslationInfo *translation() const {
    if (!cached_shader)
      return nullptr;
    const auto &info = cached_shader->parser.dxilTranslation();
    return info ? &*info : nullptr;
  }

  const DXMT12_MTL4_SHADER_ARGUMENT *constantBufferInfo() const {
    if (!cached_shader || cached_shader->argument_info.empty())
      return nullptr;
    return cached_shader->argument_info.data();
  }

  const DXMT12_MTL4_SHADER_ARGUMENT *resourceArgumentInfo() const {
    if (!cached_shader)
      return nullptr;
    return cached_shader->argument_info.size() <=
                   cached_shader->reflection.NumConstantBuffers
               ? nullptr
               : cached_shader->argument_info.data() +
                     cached_shader->reflection.NumConstantBuffers;
  }

private:
  void destroyShader() {
    if (!shader)
      return;
    if (kind() == PipelineShaderBytecodeKind::Dxil)
      DXMT12DXILDestroy(shader);
    else
      DXMT12SM50Destroy(shader);
    shader = nullptr;
  }
};

struct PipelineMetalShader {
  WMT::Reference<WMT::Library> library;
  WMT::Reference<WMT::Function> function;
  bool persistent_cache_hit = false;
  std::string persistent_cache_key;
};

struct PipelineMetalGraphicsState {
  PipelineMetalShader vertex;
  std::array<PipelineMetalShader, 3> tessellation_vertex_hull;
  PipelineMetalShader tessellation_domain;
  PipelineMetalShader geometry_vertex;
  PipelineMetalShader geometry;
  PipelineMetalShader geometry_strip_vertex;
  PipelineMetalShader geometry_strip;
  PipelineMetalShader pixel;
  WMT::Reference<WMT::RenderPipelineState> pso;
  WMT::Reference<WMT::RenderPipelineState> tessellation_pso_u16;
  WMT::Reference<WMT::RenderPipelineState> tessellation_pso_u32;
  WMT::Reference<WMT::RenderPipelineState> strip_pso;
  WMT::Reference<WMT::DepthStencilState> depth_stencil;
  wmtcmd_render_setrasterizerstate rasterizer = {};
  bool use_geometry = false;
  bool use_tessellation = false;
  uint64_t pixel_shader_demote_msaa_srv_mask_lo = 0;
  uint64_t pixel_shader_demote_msaa_srv_mask_hi = 0;
  uint32_t tess_num_output_control_point_element = 0;
  uint32_t tess_threads_per_patch = 0;
};

struct PipelineMetalComputeState {
  PipelineMetalShader compute;
  WMT::Reference<WMT::ComputePipelineState> pso;
  WMTSize threadgroup_size = {1, 1, 1};
};

struct PipelineSignatureLink {
  uint32_t producer_shader_index = 0;
  uint32_t producer_signature_index = 0;
  uint32_t consumer_shader_index = 0;
  uint32_t consumer_signature_index = 0;
  std::string semantic_key;
  uint8_t producer_component_mask = 0;
  uint8_t consumer_component_mask = 0;
};

struct PipelineGraphicsState {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
  std::vector<std::string> input_element_semantic_names;
  std::vector<D3D12_SO_DECLARATION_ENTRY> stream_output_entries;
  std::vector<std::string> stream_output_semantic_names;
  std::vector<UINT> stream_output_strides;
  std::vector<uint8_t> cached_pso;
};

struct PipelineComputeState {
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
  std::vector<uint8_t> cached_pso;
};

class PipelineState {
public:
  virtual ~PipelineState() = default;

  // Monotonic per-process identity used by immutable submission-plan caches.
  // Unlike the object address it cannot be reused after destruction, so cache
  // entries may safely outlive one ExecuteCommandLists batch.
  virtual uint64_t GetCacheIdentity() const = 0;
  virtual IMTLD3D12Device *GetParentDevice() const = 0;
  virtual PipelineStateType GetType() const = 0;
  virtual ID3D12RootSignature *GetRootSignature() const = 0;
  virtual const std::vector<PipelineDxilShader> &GetDxilShaders() const = 0;
  virtual const std::vector<PipelineSignatureLink> &GetSignatureLinks() const = 0;
  virtual const PipelineGraphicsState *GetGraphicsState() const = 0;
  virtual const PipelineComputeState *GetComputeState() const = 0;
  virtual const std::string &GetShaderCacheKey() const = 0;
  virtual const PipelineMetalGraphicsState *GetMetalGraphicsState() = 0;
  virtual const PipelineMetalGraphicsState *
  GetMetalGraphicsState(uint64_t pixel_shader_demote_msaa_srv_mask_lo,
                        uint64_t pixel_shader_demote_msaa_srv_mask_hi) = 0;
  virtual const PipelineMetalComputeState *GetMetalComputeState() = 0;
  virtual bool UsesBindlessMirror() const = 0;
  virtual DXMT12_MTL4_SHADER_ABI_VERSION GetShaderAbiVersion() const = 0;
};

NativeShaderAbiEligibilityReason
GetNativeShaderAbiEligibility(const std::vector<PipelineDxilShader> &shaders,
                              const RootSignature *root_signature);

// Private IID for a non-RTTI downcast ID3D12PipelineState* -> dxmt PipelineState*.
// See IID_DXMTResourceDowncast in d3d12_resource.hpp for rationale (no AddRef,
// internal replay fast-path, preserves dynamic_cast null-on-mismatch semantics).
inline constexpr GUID IID_DXMTPipelineStateDowncast = {
    0x9f2c5d83, 0x71e4, 0x4a6b,
    {0x88, 0x1f, 0xc2, 0x40, 0x6e, 0x95, 0x3b, 0x7d}};

Com<ID3D12PipelineState>
CreateGraphicsPipelineState(IMTLD3D12Device *device,
                            const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                            HRESULT *status = nullptr);

Com<ID3D12PipelineState>
CreateComputePipelineState(IMTLD3D12Device *device,
                           const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                           HRESULT *status = nullptr);

#ifdef __ID3D12Device2_INTERFACE_DEFINED__
Com<ID3D12PipelineState>
CreatePipelineStateFromStream(IMTLD3D12Device *device,
                              const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                              HRESULT *status = nullptr);
#endif

#ifdef __ID3D12PipelineLibrary_INTERFACE_DEFINED__
Com<ID3D12PipelineLibrary>
CreatePipelineLibrary(IMTLD3D12Device *device);
#endif

} // namespace dxmt::d3d12
