#include "nvngx_d3d12.hpp"
#include "log/log.hpp"
#include "nvngx_parameter.hpp"

namespace dxmt {

namespace {

NVNGX_RESULT
CreateUnsupportedD3D12CapabilityParameters(NVNGXParameter **out_params) {
  if (!out_params)
    return NVNGX_RESULT_INVALID_PARAMETER;

  auto out_parameters = new ParametersImpl();
  out_parameters->Set("SuperSampling.Available", 0u);
  out_parameters->Set(NVSDK_NGX_EParameter_SuperSampling_Available, 0u);
  out_parameters->Set("SuperSampling.MinDriverVersionMajor", 0);
  out_parameters->Set("SuperSampling.MinDriverVersionMinor", 0);
  out_parameters->Set("SuperSampling.NeedsUpdatedDriver", 0);
  out_parameters->Set("SuperSampling.FeatureInitResult",
                      static_cast<unsigned int>(NVNGX_RESULT_FEATURE_NOT_SUPPORTED));
  out_parameters->Set("DLSS.Available", 0u);
  out_parameters->Set("DLSS.NeedsUpdatedDriver", 0);
  out_parameters->Set("DLSS.FeatureInitResult",
                      static_cast<unsigned int>(NVNGX_RESULT_FEATURE_NOT_SUPPORTED));
  out_parameters->Set("FrameGeneration.Available", 0u);
  out_parameters->Set("FrameGeneration.FeatureInitResult",
                      static_cast<unsigned int>(NVNGX_RESULT_FEATURE_NOT_SUPPORTED));
  out_parameters->Set("DLSSG.Available", 0u);
  out_parameters->Set("DLSSG.FeatureInitResult",
                      static_cast<unsigned int>(NVNGX_RESULT_FEATURE_NOT_SUPPORTED));
  out_parameters->Set("RayReconstruction.Available", 0u);
  out_parameters->Set("RayReconstruction.FeatureInitResult",
                      static_cast<unsigned int>(NVNGX_RESULT_FEATURE_NOT_SUPPORTED));
  out_parameters->Set("DLSS_RR.Available", 0u);
  out_parameters->Set("RTXValue", 0);
  out_parameters->Set("CreationNodeMask", 1);
  out_parameters->Set("VisibilityNodeMask", 1);

  *out_params = out_parameters;
  return NVNGX_RESULT_OK;
}

NVNGX_RESULT
RejectUnsupportedD3D12Feature(unsigned int feature, unsigned int **out_handle) {
  if (out_handle)
    *out_handle = nullptr;

  WARN("NVSDK_NGX_D3D12: feature ", feature, " is not supported");
  return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
}

} // namespace

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init_Ext(unsigned long long id, const wchar_t *path,
                         ID3D12Device *device, unsigned int sdk_version,
                         const void *feature_info) {
  if (!device)
    return NVNGX_RESULT_INVALID_PARAMETER;

  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init(unsigned long long id, const wchar_t *path,
                     ID3D12Device *device, const void *feature_info,
                     unsigned int sdk_version) {
  if (!device)
    return NVNGX_RESULT_INVALID_PARAMETER;

  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init_ProjectID(const char *project, unsigned int engine_type,
                               const char *engine_version,
                               const wchar_t *path, ID3D12Device *device,
                               unsigned int sdk_version,
                               const void *feature_info) {
  if (!device)
    return NVNGX_RESULT_INVALID_PARAMETER;

  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Init_with_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version,
    const wchar_t *path, ID3D12Device *device, const void *feature_info,
    unsigned int sdk_version) {
  if (!device)
    return NVNGX_RESULT_INVALID_PARAMETER;

  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Shutdown() {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_Shutdown1(ID3D12Device *device) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetScratchBufferSize(unsigned int feature,
                                     const NVNGXParameter *params,
                                     size_t *out_size) {
  if (!out_size)
    return NVNGX_RESULT_INVALID_PARAMETER;

  *out_size = 0;
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList *command_list,
                              unsigned int feature, NVNGXParameter *params,
                              unsigned int **out_handle) {
  if (out_handle)
    *out_handle = nullptr;

  if (!command_list || !params || !out_handle)
    return NVNGX_RESULT_INVALID_PARAMETER;

  return RejectUnsupportedD3D12Feature(feature, out_handle);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList *command_list,
                                const unsigned int *handle,
                                NVNGXParameter *params, void *callback) {
  if (!command_list || !handle || !params)
    return NVNGX_RESULT_INVALID_PARAMETER;

  return NVNGX_RESULT_FEATURE_NOT_SUPPORTED;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_ReleaseFeature(unsigned int *handle) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_AllocateParameters(NVNGXParameter **out_params) {
  if (!out_params)
    return NVNGX_RESULT_INVALID_PARAMETER;

  *out_params = new ParametersImpl();
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_DestroyParameters(NVNGXParameter *params) {
  delete static_cast<ParametersImpl *>(params);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetCapabilityParameters(NVNGXParameter **out_params) {
  return CreateUnsupportedD3D12CapabilityParameters(out_params);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetParameters(NVNGXParameter **out_params) {
  return CreateUnsupportedD3D12CapabilityParameters(out_params);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D12_GetFeatureRequirements(
    IDXGIAdapter *adapter, const NVNGX_FeatureDiscoveryInfo *discovery_info,
    NVNGX_FeatureRequirement *requirement) {
  if (!discovery_info || !requirement)
    return NVNGX_RESULT_INVALID_PARAMETER;

  requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_UNSUPPORTED;
  requirement->MinHWArchitecture = 0;
  strcpy_s(requirement->MinOSVersion, "10.0.16299.0");
  return NVNGX_RESULT_OK;
}

} // namespace dxmt
