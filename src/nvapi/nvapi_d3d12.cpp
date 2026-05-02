#include "log/log.hpp"
#include "nvapi_d3d12.hpp"
#include <cstring>

namespace dxmt {

namespace {

bool
IsSupportedNvShaderExtnOpCode(NvU32 opCode) {
  switch (opCode) {
  case NV_EXTN_OP_UINT64_ATOMIC:
  case NV_EXTN_OP_SHFL:
  case NV_EXTN_OP_FP16_ATOMIC:
    return false;
  default:
    WARN("nvapi: unsupported D3D12 shader extension opcode ", opCode);
    return false;
  }
}

} // namespace

NVAPI_INTERFACE
NvAPI_D3D12_SetNvShaderExtnSlotSpace(IUnknown *pDev, NvU32 uavSlot,
                                     NvU32 uavSpace) {
  if (!pDev)
    return NVAPI_INVALID_ARGUMENT;

  WARN("nvapi: D3D12 shader extension UAV slot is not supported");
  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(IUnknown *pDev,
                                                NvU32 uavSlot,
                                                NvU32 uavSpace) {
  if (!pDev)
    return NVAPI_INVALID_ARGUMENT;

  WARN("nvapi: D3D12 thread-local shader extension UAV slot is not supported");
  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(ID3D12Device *pDevice,
                                          NvU32 opCode,
                                          bool *pSupported) {
  if (!pDevice || !pSupported)
    return NVAPI_INVALID_ARGUMENT;

  *pSupported = IsSupportedNvShaderExtnOpCode(opCode);
  return NVAPI_OK;
}

NVAPI_INTERFACE
NvAPI_D3D12_QueryPresentBarrierSupport(ID3D12Device *pDevice,
                                       bool *pSupported) {
  if (!pDevice || !pSupported)
    return NVAPI_INVALID_ARGUMENT;

  *pSupported = false;
  return NVAPI_OK;
}

NVAPI_INTERFACE
NvAPI_D3D12_CreatePresentBarrierClient(
    ID3D12Device *pDevice, IDXGISwapChain *pSwapChain,
    NvPresentBarrierClientHandle *pPresentBarrierClient) {
  if (!pDevice || !pSwapChain || !pPresentBarrierClient)
    return NVAPI_INVALID_ARGUMENT;

  *pPresentBarrierClient = nullptr;
  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_D3D12_RegisterPresentBarrierResources(
    NvPresentBarrierClientHandle presentBarrierClient, ID3D12Fence *pFence,
    ID3D12Resource **ppResources, NvU32 numResources) {
  if (!presentBarrierClient || !pFence || (!ppResources && numResources))
    return NVAPI_INVALID_ARGUMENT;

  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_D3D12_CreateGraphicsPipelineState(
    ID3D12Device *pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pPSODesc,
    NvU32 numExtensions, const NVAPI_D3D12_PSO_EXTENSION_DESC **ppExtensions,
    ID3D12PipelineState **ppPSO) {
  if (!pDevice || !pPSODesc || !ppPSO)
    return NVAPI_INVALID_ARGUMENT;

  *ppPSO = nullptr;
  if (numExtensions) {
    WARN("nvapi: D3D12 graphics PSO extensions are not supported");
    return NVAPI_NOT_SUPPORTED;
  }

  return SUCCEEDED(pDevice->CreateGraphicsPipelineState(pPSODesc,
                                                        IID_PPV_ARGS(ppPSO)))
             ? NVAPI_OK
             : NVAPI_ERROR;
}

NVAPI_INTERFACE
NvAPI_D3D12_CreateComputePipelineState(
    ID3D12Device *pDevice, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pPSODesc,
    NvU32 numExtensions, const NVAPI_D3D12_PSO_EXTENSION_DESC **ppExtensions,
    ID3D12PipelineState **ppPSO) {
  if (!pDevice || !pPSODesc || !ppPSO)
    return NVAPI_INVALID_ARGUMENT;

  *ppPSO = nullptr;
  if (numExtensions) {
    WARN("nvapi: D3D12 compute PSO extensions are not supported");
    return NVAPI_NOT_SUPPORTED;
  }

  return SUCCEEDED(pDevice->CreateComputePipelineState(pPSODesc,
                                                       IID_PPV_ARGS(ppPSO)))
             ? NVAPI_OK
             : NVAPI_ERROR;
}

NVAPI_INTERFACE
NvAPI_D3D12_SetDepthBoundsTestValues(ID3D12GraphicsCommandList *pCommandList,
                                     float minDepth, float maxDepth) {
  if (!pCommandList)
    return NVAPI_INVALID_ARGUMENT;

  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_D3D12_SetAsyncFrameMarker(
    ID3D12CommandQueue *pCommandQueue,
    NV_ASYNC_FRAME_MARKER_PARAMS *pSetAsyncFrameMarkerParams) {
  if (!pCommandQueue || !pSetAsyncFrameMarkerParams)
    return NVAPI_INVALID_ARGUMENT;

  switch (pSetAsyncFrameMarkerParams->version) {
  case NV_ASYNC_FRAME_MARKER_PARAMS_VER1:
    return NVAPI_OK;
  default:
    return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
  }
}

NVAPI_INTERFACE
NvAPI_D3D12_NotifyOutOfBandCommandQueue(ID3D12CommandQueue *pCommandQueue,
                                        NV_OUT_OF_BAND_CQ_TYPE cqType) {
  if (!pCommandQueue)
    return NVAPI_INVALID_ARGUMENT;

  return NVAPI_OK;
}

NVAPI_INTERFACE
NvAPI_D3D12_SetCreateCommandQueueLowLatencyHint(ID3D12Device *pDevice) {
  if (!pDevice)
    return NVAPI_INVALID_ARGUMENT;

  return NVAPI_OK;
}

NVAPI_INTERFACE
NvAPI_D3D12_GetRaytracingCaps(ID3D12Device *pDevice,
                              NVAPI_D3D12_RAYTRACING_CAPS_TYPE type,
                              void *pData, size_t dataSize) {
  if (!pDevice || !pData || !dataSize)
    return NVAPI_INVALID_ARGUMENT;

  memset(pData, 0, dataSize);
  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_NGX_GetNGXOverrideState(
    NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS *pGetOverrideStateParams) {
  if (!pGetOverrideStateParams)
    return NVAPI_INVALID_ARGUMENT;

  switch (pGetOverrideStateParams->version) {
  case NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS_VER1:
    pGetOverrideStateParams->feedbackMaskSR = 0;
    pGetOverrideStateParams->feedbackMaskRR = 0;
    pGetOverrideStateParams->feedbackMaskFG = 0;
    memset(pGetOverrideStateParams->reserved, 0,
           sizeof(pGetOverrideStateParams->reserved));
    return NVAPI_OK;
  default:
    return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
  }
}

NVAPI_INTERFACE
NvAPI_NGX_SetNGXOverrideState(
    NV_NGX_DLSS_OVERRIDE_SET_STATE_PARAMS *pSetOverrideStateParams) {
  if (!pSetOverrideStateParams)
    return NVAPI_INVALID_ARGUMENT;

  switch (pSetOverrideStateParams->version) {
  case NV_NGX_DLSS_OVERRIDE_SET_STATE_PARAMS_VER1:
    return NVAPI_NOT_SUPPORTED;
  default:
    return NVAPI_INCOMPATIBLE_STRUCT_VERSION;
  }
}

NVAPI_INTERFACE
NvAPI_DirectD3D12GraphicsCommandList_Create(
    ID3D12GraphicsCommandList *pDXD3D12GraphicsCommandList,
    INvAPI_DirectD3D12GraphicsCommandList **ppReturnD3D12GraphicsCommandList) {
  if (!pDXD3D12GraphicsCommandList || !ppReturnD3D12GraphicsCommandList)
    return NVAPI_INVALID_ARGUMENT;

  *ppReturnD3D12GraphicsCommandList = nullptr;
  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_DirectD3D12GraphicsCommandList_Release(
    INvAPI_DirectD3D12GraphicsCommandList *pD3D12GraphicsCommandList) {
  if (!pD3D12GraphicsCommandList)
    return NVAPI_INVALID_ARGUMENT;

  return NVAPI_NOT_SUPPORTED;
}

NVAPI_INTERFACE
NvAPI_DirectD3D12GraphicsCommandList_Reset(
    INvAPI_DirectD3D12GraphicsCommandList *pD3D12GraphicsCommandList) {
  if (!pD3D12GraphicsCommandList)
    return NVAPI_INVALID_ARGUMENT;

  return NVAPI_NOT_SUPPORTED;
}

} // namespace dxmt
