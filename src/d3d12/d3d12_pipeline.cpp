#include "d3d12_pipeline.hpp"

#include "com/com_guid.hpp"
#include "com/com_object.hpp"
#include "com/com_private_data.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include <algorithm>
#include <cstring>
#include <utility>

namespace dxmt::d3d12 {
namespace {

constexpr uint32_t kShaderKindPixel = 0;
constexpr uint32_t kShaderKindVertex = 1;
constexpr uint32_t kShaderKindGeometry = 2;
constexpr uint32_t kShaderKindHull = 3;
constexpr uint32_t kShaderKindDomain = 4;
constexpr uint32_t kShaderKindCompute = 5;

const char *
ShaderStageName(PipelineShaderStage stage) {
  switch (stage) {
  case PipelineShaderStage::Vertex:
    return "VS";
  case PipelineShaderStage::Pixel:
    return "PS";
  case PipelineShaderStage::Geometry:
    return "GS";
  case PipelineShaderStage::Hull:
    return "HS";
  case PipelineShaderStage::Domain:
    return "DS";
  case PipelineShaderStage::Compute:
    return "CS";
  }
  return "unknown";
}

uint32_t
ExpectedShaderKind(PipelineShaderStage stage) {
  switch (stage) {
  case PipelineShaderStage::Vertex:
    return kShaderKindVertex;
  case PipelineShaderStage::Pixel:
    return kShaderKindPixel;
  case PipelineShaderStage::Geometry:
    return kShaderKindGeometry;
  case PipelineShaderStage::Hull:
    return kShaderKindHull;
  case PipelineShaderStage::Domain:
    return kShaderKindDomain;
  case PipelineShaderStage::Compute:
    return kShaderKindCompute;
  }
  return UINT32_MAX;
}

bool
HasBytecode(const D3D12_SHADER_BYTECODE &bytecode) {
  return bytecode.pShaderBytecode && bytecode.BytecodeLength;
}

bool
IsDxilContainer(const D3D12_SHADER_BYTECODE &bytecode) {
  dxil::ContainerInfo container = {};
  const auto status = dxil::ParseContainer(
      bytecode.pShaderBytecode, bytecode.BytecodeLength, container);
  return status == dxil::ParseStatus::Ok &&
         container.hasPart(dxil::fourcc::Dxil);
}

HRESULT
ParseDxilShader(PipelineShaderStage stage,
                const D3D12_SHADER_BYTECODE &bytecode,
                PipelineDxilShader &shader) {
  if (!bytecode.pShaderBytecode && bytecode.BytecodeLength)
    return E_INVALIDARG;
  if (!HasBytecode(bytecode))
    return S_FALSE;
  if (!IsDxilContainer(bytecode))
    return E_NOTIMPL;

  shader = {};
  shader.stage = stage;
  shader.bytecode.resize(bytecode.BytecodeLength);
  std::memcpy(shader.bytecode.data(), bytecode.pShaderBytecode,
              bytecode.BytecodeLength);

  const auto status = shader.parser.parse(shader.bytecode.data(),
                                          shader.bytecode.size());
  if (status != dxil::ParseStatus::Ok) {
    WARN("D3D12PipelineState: failed to parse ", ShaderStageName(stage),
         " DXIL bytecode: ", dxil::StatusName(status));
    return E_INVALIDARG;
  }

  const auto &program = shader.parser.dxilProgram();
  const auto expected_kind = ExpectedShaderKind(stage);
  if (program && expected_kind != UINT32_MAX &&
      program->shader_kind() != expected_kind) {
    WARN("D3D12PipelineState: ", ShaderStageName(stage),
         " bytecode contains ", dxil::PsvShaderKindName(program->shader_kind()),
         " shader");
    return E_INVALIDARG;
  }

  return S_OK;
}

HRESULT
AppendDxilShader(PipelineShaderStage stage,
                 const D3D12_SHADER_BYTECODE &bytecode,
                 std::vector<PipelineDxilShader> &shaders) {
  PipelineDxilShader shader = {};
  const auto hr = ParseDxilShader(stage, bytecode, shader);
  if (hr == S_FALSE)
    return S_OK;
  if (FAILED(hr))
    return hr;

  shaders.push_back(std::move(shader));
  return S_OK;
}

const PipelineDxilShader *
FindShader(const std::vector<PipelineDxilShader> &shaders,
           PipelineShaderStage stage, uint32_t *index = nullptr) {
  for (uint32_t i = 0; i < shaders.size(); i++) {
    if (shaders[i].stage == stage) {
      if (index)
        *index = i;
      return &shaders[i];
    }
  }
  return nullptr;
}

bool
IsSystemGeneratedValue(const dxil::DxilTranslationSignatureElementInfo &sig) {
  return sig.semantic_kind != 0 || sig.semantic_key == "SV_POSITION0" ||
         sig.semantic_key == "SV_TARGET0" ||
         sig.semantic_key == "SV_DEPTH0" ||
         sig.semantic_key == "SV_COVERAGE0";
}

void
AppendSignatureLinks(const std::vector<PipelineDxilShader> &shaders,
                     PipelineShaderStage producer_stage,
                     PipelineShaderStage consumer_stage,
                     std::vector<PipelineSignatureLink> &links) {
  uint32_t producer_shader_index = 0;
  uint32_t consumer_shader_index = 0;
  const auto *producer =
      FindShader(shaders, producer_stage, &producer_shader_index);
  const auto *consumer =
      FindShader(shaders, consumer_stage, &consumer_shader_index);
  if (!producer || !consumer)
    return;

  const auto *producer_info = producer->translation();
  const auto *consumer_info = consumer->translation();
  if (!producer_info || !consumer_info)
    return;

  for (uint32_t consumer_sig_index = 0;
       consumer_sig_index < consumer_info->signatures.size();
       consumer_sig_index++) {
    const auto &consumer_sig = consumer_info->signatures[consumer_sig_index];
    if (consumer_sig.kind != dxil::DxilTranslationSignatureKind::Input ||
        IsSystemGeneratedValue(consumer_sig))
      continue;

    auto match = std::find_if(
        producer_info->signatures.begin(), producer_info->signatures.end(),
        [&](const dxil::DxilTranslationSignatureElementInfo &producer_sig) {
          return producer_sig.kind ==
                     dxil::DxilTranslationSignatureKind::Output &&
                 producer_sig.output_stream == consumer_sig.output_stream &&
                 producer_sig.semantic_key == consumer_sig.semantic_key &&
                 (producer_sig.component_mask & consumer_sig.component_mask) ==
                     consumer_sig.component_mask;
        });
    if (match == producer_info->signatures.end()) {
      WARN("D3D12PipelineState: unmatched ", ShaderStageName(consumer_stage),
           " input signature ", consumer_sig.semantic_key);
      continue;
    }

    links.push_back({
        .producer_shader_index = producer_shader_index,
        .producer_signature_index =
            uint32_t(match - producer_info->signatures.begin()),
        .consumer_shader_index = consumer_shader_index,
        .consumer_signature_index = consumer_sig_index,
        .semantic_key = consumer_sig.semantic_key,
        .producer_component_mask = match->component_mask,
        .consumer_component_mask = consumer_sig.component_mask,
    });
  }
}

std::vector<PipelineSignatureLink>
BuildSignatureLinks(const std::vector<PipelineDxilShader> &shaders) {
  std::vector<PipelineSignatureLink> links;

  if (FindShader(shaders, PipelineShaderStage::Hull))
    AppendSignatureLinks(shaders, PipelineShaderStage::Vertex,
                         PipelineShaderStage::Hull, links);
  if (FindShader(shaders, PipelineShaderStage::Domain))
    AppendSignatureLinks(shaders, PipelineShaderStage::Hull,
                         PipelineShaderStage::Domain, links);

  const auto raster_producer = FindShader(shaders, PipelineShaderStage::Domain)
                                   ? PipelineShaderStage::Domain
                                   : PipelineShaderStage::Vertex;
  if (FindShader(shaders, PipelineShaderStage::Geometry)) {
    AppendSignatureLinks(shaders, raster_producer,
                         PipelineShaderStage::Geometry, links);
    AppendSignatureLinks(shaders, PipelineShaderStage::Geometry,
                         PipelineShaderStage::Pixel, links);
  } else {
    AppendSignatureLinks(shaders, raster_producer,
                         PipelineShaderStage::Pixel, links);
  }

  return links;
}

class PipelineStateImpl final : public ComObjectWithInitialRef<ID3D12PipelineState>,
                                public PipelineState {
public:
  PipelineStateImpl(IMTLD3D12Device *device, PipelineStateType type,
                    ID3D12RootSignature *root_signature,
                    std::vector<PipelineDxilShader> &&shaders,
                    std::vector<PipelineSignatureLink> &&signature_links)
      : device_(device), type_(type), root_signature_(root_signature),
        shaders_(std::move(shaders)),
        signature_links_(std::move(signature_links)) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override {
    if (!object)
      return E_POINTER;

    *object = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) ||
        riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable) ||
        riid == __uuidof(ID3D12PipelineState)) {
      *object = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12PipelineState), riid))
      WARN("D3D12PipelineState: unknown interface query ", str::format(riid));

    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *data_size,
                                           void *data) override {
    return private_data_.getData(guid, data_size, data);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT data_size,
                                           const void *data) override {
    return private_data_.setData(guid, data_size, data);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid,
                                                   const IUnknown *data) override {
    return private_data_.setInterface(guid, data);
  }

  HRESULT STDMETHODCALLTYPE SetName(const WCHAR *name) override {
    name_ = name ? str::fromws(name) : std::string();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override {
    return device_->QueryInterface(riid, device);
  }

  HRESULT STDMETHODCALLTYPE GetCachedBlob(ID3DBlob **blob) override {
    InitReturnPtr(blob);
    return E_NOTIMPL;
  }

  PipelineStateType GetType() const override {
    return type_;
  }

  ID3D12RootSignature *GetRootSignature() const override {
    return root_signature_.ptr();
  }

  const std::vector<PipelineDxilShader> &GetDxilShaders() const override {
    return shaders_;
  }

  const std::vector<PipelineSignatureLink> &GetSignatureLinks() const override {
    return signature_links_;
  }

private:
  Com<IMTLD3D12Device> device_;
  PipelineStateType type_;
  Com<ID3D12RootSignature> root_signature_;
  std::vector<PipelineDxilShader> shaders_;
  std::vector<PipelineSignatureLink> signature_links_;
  ComPrivateData private_data_;
  std::string name_;
};

Com<ID3D12PipelineState>
CreatePipelineStateObject(IMTLD3D12Device *device, PipelineStateType type,
                          ID3D12RootSignature *root_signature,
                          std::vector<PipelineDxilShader> &&shaders) {
  auto signature_links = BuildSignatureLinks(shaders);
  return Com<ID3D12PipelineState>::transfer(
      new PipelineStateImpl(device, type, root_signature, std::move(shaders),
                            std::move(signature_links)));
}

void
StoreStatus(HRESULT *status, HRESULT value) {
  if (status)
    *status = value;
}

} // namespace

Com<ID3D12PipelineState>
CreateGraphicsPipelineState(IMTLD3D12Device *device,
                            const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                            HRESULT *status) {
  if (!device || !desc) {
    StoreStatus(status, E_INVALIDARG);
    return nullptr;
  }

  std::vector<PipelineDxilShader> shaders;
  shaders.reserve(5);

  HRESULT hr = AppendDxilShader(PipelineShaderStage::Vertex, desc->VS, shaders);
  if (FAILED(hr)) {
    StoreStatus(status, hr);
    return nullptr;
  }
  hr = AppendDxilShader(PipelineShaderStage::Pixel, desc->PS, shaders);
  if (FAILED(hr)) {
    StoreStatus(status, hr);
    return nullptr;
  }
  hr = AppendDxilShader(PipelineShaderStage::Geometry, desc->GS, shaders);
  if (FAILED(hr)) {
    StoreStatus(status, hr);
    return nullptr;
  }
  hr = AppendDxilShader(PipelineShaderStage::Hull, desc->HS, shaders);
  if (FAILED(hr)) {
    StoreStatus(status, hr);
    return nullptr;
  }
  hr = AppendDxilShader(PipelineShaderStage::Domain, desc->DS, shaders);
  if (FAILED(hr)) {
    StoreStatus(status, hr);
    return nullptr;
  }
  if (shaders.empty()) {
    StoreStatus(status, E_INVALIDARG);
    return nullptr;
  }

  auto pso = CreatePipelineStateObject(
      device, PipelineStateType::Graphics, desc->pRootSignature,
      std::move(shaders));
  StoreStatus(status, pso ? S_OK : E_OUTOFMEMORY);
  return pso;
}

Com<ID3D12PipelineState>
CreateComputePipelineState(IMTLD3D12Device *device,
                           const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                           HRESULT *status) {
  if (!device || !desc) {
    StoreStatus(status, E_INVALIDARG);
    return nullptr;
  }

  std::vector<PipelineDxilShader> shaders;
  shaders.reserve(1);
  const auto hr =
      AppendDxilShader(PipelineShaderStage::Compute, desc->CS, shaders);
  if (FAILED(hr)) {
    StoreStatus(status, hr);
    return nullptr;
  }
  if (shaders.empty()) {
    StoreStatus(status, E_INVALIDARG);
    return nullptr;
  }

  auto pso = CreatePipelineStateObject(
      device, PipelineStateType::Compute, desc->pRootSignature,
      std::move(shaders));
  StoreStatus(status, pso ? S_OK : E_OUTOFMEMORY);
  return pso;
}

} // namespace dxmt::d3d12
