#pragma once

#include <d3d12.h>
#include <dxgi.h>

#define __NVAPI_EMPTY_SAL
#include "nvapi.h"
#include "nvShaderExtnEnums.h"
#undef __NVAPI_EMPTY_SAL

namespace dxmt {

NVAPI_INTERFACE NvAPI_D3D12_SetNvShaderExtnSlotSpace(IUnknown *pDev,
                                                     NvU32 uavSlot,
                                                     NvU32 uavSpace);
NVAPI_INTERFACE NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread(
    IUnknown *pDev, NvU32 uavSlot, NvU32 uavSpace);
NVAPI_INTERFACE NvAPI_D3D12_IsNvShaderExtnOpCodeSupported(
    ID3D12Device *pDevice, NvU32 opCode, bool *pSupported);
NVAPI_INTERFACE NvAPI_D3D12_QueryPresentBarrierSupport(
    ID3D12Device *pDevice, bool *pSupported);
NVAPI_INTERFACE NvAPI_D3D12_CreatePresentBarrierClient(
    ID3D12Device *pDevice, IDXGISwapChain *pSwapChain,
    NvPresentBarrierClientHandle *pPresentBarrierClient);
NVAPI_INTERFACE NvAPI_D3D12_RegisterPresentBarrierResources(
    NvPresentBarrierClientHandle presentBarrierClient, ID3D12Fence *pFence,
    ID3D12Resource **ppResources, NvU32 numResources);
NVAPI_INTERFACE NvAPI_D3D12_CreateGraphicsPipelineState(
    ID3D12Device *pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *pPSODesc,
    NvU32 numExtensions, const NVAPI_D3D12_PSO_EXTENSION_DESC **ppExtensions,
    ID3D12PipelineState **ppPSO);
NVAPI_INTERFACE NvAPI_D3D12_CreateComputePipelineState(
    ID3D12Device *pDevice, const D3D12_COMPUTE_PIPELINE_STATE_DESC *pPSODesc,
    NvU32 numExtensions, const NVAPI_D3D12_PSO_EXTENSION_DESC **ppExtensions,
    ID3D12PipelineState **ppPSO);
NVAPI_INTERFACE NvAPI_D3D12_SetDepthBoundsTestValues(
    ID3D12GraphicsCommandList *pCommandList, float minDepth, float maxDepth);
NVAPI_INTERFACE NvAPI_D3D12_SetAsyncFrameMarker(
    ID3D12CommandQueue *pCommandQueue,
    NV_ASYNC_FRAME_MARKER_PARAMS *pSetAsyncFrameMarkerParams);
NVAPI_INTERFACE NvAPI_D3D12_NotifyOutOfBandCommandQueue(
    ID3D12CommandQueue *pCommandQueue, NV_OUT_OF_BAND_CQ_TYPE cqType);
NVAPI_INTERFACE NvAPI_D3D12_SetCreateCommandQueueLowLatencyHint(
    ID3D12Device *pDevice);
NVAPI_INTERFACE NvAPI_D3D12_GetRaytracingCaps(
    ID3D12Device *pDevice, NVAPI_D3D12_RAYTRACING_CAPS_TYPE type,
    void *pData, size_t dataSize);
NVAPI_INTERFACE NvAPI_NGX_GetNGXOverrideState(
    NV_NGX_DLSS_OVERRIDE_GET_STATE_PARAMS *pGetOverrideStateParams);
NVAPI_INTERFACE NvAPI_NGX_SetNGXOverrideState(
    NV_NGX_DLSS_OVERRIDE_SET_STATE_PARAMS *pSetOverrideStateParams);
NVAPI_INTERFACE NvAPI_DirectD3D12GraphicsCommandList_Create(
    ID3D12GraphicsCommandList *pDXD3D12GraphicsCommandList,
    INvAPI_DirectD3D12GraphicsCommandList **ppReturnD3D12GraphicsCommandList);
NVAPI_INTERFACE NvAPI_DirectD3D12GraphicsCommandList_Release(
    INvAPI_DirectD3D12GraphicsCommandList *pD3D12GraphicsCommandList);
NVAPI_INTERFACE NvAPI_DirectD3D12GraphicsCommandList_Reset(
    INvAPI_DirectD3D12GraphicsCommandList *pD3D12GraphicsCommandList);

} // namespace dxmt
