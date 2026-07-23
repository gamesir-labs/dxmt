#include "config/config.hpp"
#include "d3d11_fence.hpp"
#include "d3d11_private.h"
#include "d3d11_query.hpp"
#include "dxmt_command_queue.hpp"
#include "d3d11_context_impl.cpp"
#include "dxmt_context.hpp"
#include "dxmt_staging.hpp"
#include <algorithm>

namespace dxmt {
struct ContextInternalState {
  using device_mutex_t = d3d11_device_mutex;
  CommandQueue &cmd_queue;
  bool has_dirty_op_since_last_event = false;
};


template<typename Object> Rc<Object> forward_rc(Rc<Object>& obj) {
  return std::move(obj);
}

using ImmediateContextBase = MTLD3D11DeviceContextImplBase<ContextInternalState>;

class MTLD3D11VideoContext final : public ID3D11VideoContext {
public:
  explicit MTLD3D11VideoContext(ID3D11DeviceContext *context) :
      context_(context) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) override {
    return context_->QueryInterface(riid, ppvObject);
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return context_->AddRef();
  }

  ULONG STDMETHODCALLTYPE Release() override {
    return context_->Release();
  }

  void STDMETHODCALLTYPE GetDevice(ID3D11Device **ppDevice) override {
    return context_->GetDevice(ppDevice);
  }

  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) override {
    return context_->GetPrivateData(guid, pDataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) override {
    return context_->SetPrivateData(guid, DataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {
    return context_->SetPrivateDataInterface(guid, pData);
  }

  HRESULT STDMETHODCALLTYPE GetDecoderBuffer(
      ID3D11VideoDecoder *,
      D3D11_VIDEO_DECODER_BUFFER_TYPE,
      UINT *buffer_size,
      void **buffer) override {
    if (buffer_size)
      *buffer_size = 0;
    if (buffer)
      *buffer = nullptr;
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE ReleaseDecoderBuffer(
      ID3D11VideoDecoder *,
      D3D11_VIDEO_DECODER_BUFFER_TYPE) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE DecoderBeginFrame(
      ID3D11VideoDecoder *,
      ID3D11VideoDecoderOutputView *,
      UINT,
      const void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE DecoderEndFrame(ID3D11VideoDecoder *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE SubmitDecoderBuffers(
      ID3D11VideoDecoder *,
      UINT,
      const D3D11_VIDEO_DECODER_BUFFER_DESC *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE DecoderExtension(
      ID3D11VideoDecoder *,
      const D3D11_VIDEO_DECODER_EXTENSION *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  void STDMETHODCALLTYPE VideoProcessorSetOutputTargetRect(
      ID3D11VideoProcessor *,
      WINBOOL,
      const RECT *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetOutputBackgroundColor(
      ID3D11VideoProcessor *,
      WINBOOL,
      const D3D11_VIDEO_COLOR *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetOutputColorSpace(
      ID3D11VideoProcessor *,
      const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetOutputAlphaFillMode(
      ID3D11VideoProcessor *,
      D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE,
      UINT) override {}

  void STDMETHODCALLTYPE VideoProcessorSetOutputConstriction(
      ID3D11VideoProcessor *,
      WINBOOL,
      SIZE) override {}

  void STDMETHODCALLTYPE VideoProcessorSetOutputStereoMode(
      ID3D11VideoProcessor *,
      WINBOOL) override {}

  HRESULT STDMETHODCALLTYPE VideoProcessorSetOutputExtension(
      ID3D11VideoProcessor *,
      const GUID *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  void STDMETHODCALLTYPE VideoProcessorGetOutputTargetRect(
      ID3D11VideoProcessor *,
      WINBOOL *enabled,
      RECT *rect) override {
    if (enabled)
      *enabled = FALSE;
    if (rect)
      *rect = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetOutputBackgroundColor(
      ID3D11VideoProcessor *,
      WINBOOL *y_cb_cr,
      D3D11_VIDEO_COLOR *color) override {
    if (y_cb_cr)
      *y_cb_cr = FALSE;
    if (color)
      *color = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetOutputColorSpace(
      ID3D11VideoProcessor *,
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space) override {
    if (color_space)
      *color_space = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetOutputAlphaFillMode(
      ID3D11VideoProcessor *,
      D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE *alpha_fill_mode,
      UINT *stream_idx) override {
    if (alpha_fill_mode)
      *alpha_fill_mode = D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE;
    if (stream_idx)
      *stream_idx = 0;
  }

  void STDMETHODCALLTYPE VideoProcessorGetOutputConstriction(
      ID3D11VideoProcessor *,
      WINBOOL *enabled,
      SIZE *size) override {
    if (enabled)
      *enabled = FALSE;
    if (size)
      *size = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetOutputStereoMode(
      ID3D11VideoProcessor *,
      WINBOOL *enabled) override {
    if (enabled)
      *enabled = FALSE;
  }

  HRESULT STDMETHODCALLTYPE VideoProcessorGetOutputExtension(
      ID3D11VideoProcessor *,
      const GUID *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  void STDMETHODCALLTYPE VideoProcessorSetStreamFrameFormat(
      ID3D11VideoProcessor *,
      UINT,
      D3D11_VIDEO_FRAME_FORMAT) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamColorSpace(
      ID3D11VideoProcessor *,
      UINT,
      const D3D11_VIDEO_PROCESSOR_COLOR_SPACE *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamOutputRate(
      ID3D11VideoProcessor *,
      UINT,
      D3D11_VIDEO_PROCESSOR_OUTPUT_RATE,
      WINBOOL,
      const DXGI_RATIONAL *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamSourceRect(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL,
      const RECT *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamDestRect(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL,
      const RECT *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamAlpha(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL,
      float) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamPalette(
      ID3D11VideoProcessor *,
      UINT,
      UINT,
      const UINT *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamPixelAspectRatio(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL,
      const DXGI_RATIONAL *,
      const DXGI_RATIONAL *) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamLumaKey(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL,
      float,
      float) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamStereoFormat(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL,
      D3D11_VIDEO_PROCESSOR_STEREO_FORMAT,
      WINBOOL,
      WINBOOL,
      D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE,
      int) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamAutoProcessingMode(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL) override {}

  void STDMETHODCALLTYPE VideoProcessorSetStreamFilter(
      ID3D11VideoProcessor *,
      UINT,
      D3D11_VIDEO_PROCESSOR_FILTER,
      WINBOOL,
      int) override {}

  HRESULT STDMETHODCALLTYPE VideoProcessorSetStreamExtension(
      ID3D11VideoProcessor *,
      UINT,
      const GUID *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamFrameFormat(
      ID3D11VideoProcessor *,
      UINT,
      D3D11_VIDEO_FRAME_FORMAT *format) override {
    if (format)
      *format = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamColorSpace(
      ID3D11VideoProcessor *,
      UINT,
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE *color_space) override {
    if (color_space)
      *color_space = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamOutputRate(
      ID3D11VideoProcessor *,
      UINT,
      D3D11_VIDEO_PROCESSOR_OUTPUT_RATE *rate,
      WINBOOL *repeat,
      DXGI_RATIONAL *custom_rate) override {
    if (rate)
      *rate = D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL;
    if (repeat)
      *repeat = FALSE;
    if (custom_rate)
      *custom_rate = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamSourceRect(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enabled,
      RECT *rect) override {
    if (enabled)
      *enabled = FALSE;
    if (rect)
      *rect = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamDestRect(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enabled,
      RECT *rect) override {
    if (enabled)
      *enabled = FALSE;
    if (rect)
      *rect = {};
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamAlpha(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enabled,
      float *alpha) override {
    if (enabled)
      *enabled = FALSE;
    if (alpha)
      *alpha = 1.0f;
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamPalette(
      ID3D11VideoProcessor *,
      UINT,
      UINT entry_count,
      UINT *entries) override {
    if (entries)
      std::fill_n(entries, entry_count, 0);
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamPixelAspectRatio(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enabled,
      DXGI_RATIONAL *src_aspect_ratio,
      DXGI_RATIONAL *dst_aspect_ratio) override {
    if (enabled)
      *enabled = FALSE;
    if (src_aspect_ratio)
      *src_aspect_ratio = {1, 1};
    if (dst_aspect_ratio)
      *dst_aspect_ratio = {1, 1};
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamLumaKey(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enabled,
      float *lower,
      float *upper) override {
    if (enabled)
      *enabled = FALSE;
    if (lower)
      *lower = 0.0f;
    if (upper)
      *upper = 1.0f;
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamStereoFormat(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enabled,
      D3D11_VIDEO_PROCESSOR_STEREO_FORMAT *format,
      WINBOOL *left_view_frame0,
      WINBOOL *base_view_frame0,
      D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE *flip_mode,
      int *mono_offset) override {
    if (enabled)
      *enabled = FALSE;
    if (format)
      *format = D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_MONO;
    if (left_view_frame0)
      *left_view_frame0 = FALSE;
    if (base_view_frame0)
      *base_view_frame0 = FALSE;
    if (flip_mode)
      *flip_mode = D3D11_VIDEO_PROCESSOR_STEREO_FLIP_NONE;
    if (mono_offset)
      *mono_offset = 0;
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamAutoProcessingMode(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enabled) override {
    if (enabled)
      *enabled = FALSE;
  }

  void STDMETHODCALLTYPE VideoProcessorGetStreamFilter(
      ID3D11VideoProcessor *,
      UINT,
      D3D11_VIDEO_PROCESSOR_FILTER,
      WINBOOL *enabled,
      int *level) override {
    if (enabled)
      *enabled = FALSE;
    if (level)
      *level = 0;
  }

  HRESULT STDMETHODCALLTYPE VideoProcessorGetStreamExtension(
      ID3D11VideoProcessor *,
      UINT,
      const GUID *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE VideoProcessorBlt(
      ID3D11VideoProcessor *,
      ID3D11VideoProcessorOutputView *,
      UINT,
      UINT,
      const D3D11_VIDEO_PROCESSOR_STREAM *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE NegotiateCryptoSessionKeyExchange(
      ID3D11CryptoSession *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  void STDMETHODCALLTYPE EncryptionBlt(
      ID3D11CryptoSession *,
      ID3D11Texture2D *,
      ID3D11Texture2D *,
      UINT,
      void *) override {}

  void STDMETHODCALLTYPE DecryptionBlt(
      ID3D11CryptoSession *,
      ID3D11Texture2D *,
      ID3D11Texture2D *,
      D3D11_ENCRYPTED_BLOCK_INFO *,
      UINT,
      const void *,
      UINT,
      void *) override {}

  void STDMETHODCALLTYPE StartSessionKeyRefresh(
      ID3D11CryptoSession *,
      UINT,
      void *) override {}

  void STDMETHODCALLTYPE FinishSessionKeyRefresh(ID3D11CryptoSession *) override {}

  HRESULT STDMETHODCALLTYPE GetEncryptionBltKey(
      ID3D11CryptoSession *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE NegotiateAuthenticatedChannelKeyExchange(
      ID3D11AuthenticatedChannel *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE QueryAuthenticatedChannel(
      ID3D11AuthenticatedChannel *,
      UINT,
      const void *,
      UINT,
      void *) override {
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE ConfigureAuthenticatedChannel(
      ID3D11AuthenticatedChannel *,
      UINT,
      const void *,
      D3D11_AUTHENTICATED_CONFIGURE_OUTPUT *output) override {
    if (output)
      *output = {};
    return DXGI_ERROR_UNSUPPORTED;
  }

  void STDMETHODCALLTYPE VideoProcessorSetStreamRotation(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL,
      D3D11_VIDEO_PROCESSOR_ROTATION) override {}

  void STDMETHODCALLTYPE VideoProcessorGetStreamRotation(
      ID3D11VideoProcessor *,
      UINT,
      WINBOOL *enable,
      D3D11_VIDEO_PROCESSOR_ROTATION *rotation) override {
    if (enable)
      *enable = FALSE;
    if (rotation)
      *rotation = D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY;
  }

private:
  ID3D11DeviceContext *context_;
};

template <>
template <CommandWithContext<ArgumentEncodingContext> cmd>
void
ImmediateContextBase::EmitST(cmd &&fn) {
  CommandChunk *chk = ctx_state.cmd_queue.CurrentChunk();
  chk->emitcc(std::forward<cmd>(fn));
}

template <>
template <CommandWithContext<ArgumentEncodingContext> cmd>
void
ImmediateContextBase::EmitOP(cmd &&fn) {
  CommandChunk *chk = ctx_state.cmd_queue.CurrentChunk();
  chk->emitcc(std::forward<cmd>(fn));
  ctx_state.has_dirty_op_since_last_event = true;
}

template <>
template <typename T>
moveonly_list<T>
ImmediateContextBase::AllocateCommandData(size_t n) {
  CommandChunk *chk = ctx_state.cmd_queue.CurrentChunk();
  return moveonly_list<T>((T *)chk->allocate_cpu_heap(sizeof(T) * n, alignof(T)), n);
}

template <>
std::tuple<WMT::Buffer, uint64_t>
ImmediateContextBase::AllocateStagingBuffer(size_t size, size_t alignment) {
  return ctx_state.cmd_queue.AllocateStagingBuffer(size, alignment);
}

template <>
AllocatedTempBufferSlice
ImmediateContextBase::AllocateStagingBuffer1(size_t size, size_t alignment) {
  return ctx_state.cmd_queue.AllocateStagingBuffer1(size, alignment);
}

template <>
void
ImmediateContextBase::UseCopyDestination(Rc<StagingResource> &staging) {
  staging->useCopyDestination(ctx_state.cmd_queue.CurrentSeqId());
}

template <>
void
ImmediateContextBase::UseCopySource(Rc<StagingResource> &staging) {
  staging->useCopySource(ctx_state.cmd_queue.CurrentSeqId());
}

template <>
std::pair<BufferAllocation *, uint32_t>
ImmediateContextBase::GetDynamicBufferAllocation(Rc<DynamicBuffer> &dynamic) {
  return {dynamic->immediateName().ptr(), dynamic->immediateSuballocation()};
}

class MTLD3D11ImmediateContext : public ImmediateContextBase {
public:
  MTLD3D11ImmediateContext(MTLD3D11Device *pDevice, CommandQueue &cmd_queue) :
      ImmediateContextBase(pDevice, ctx_state, pDevice->mutex),
      cmd_queue(cmd_queue),
      ctx_state({cmd_queue}),
      video_context_(this),
      d3dmt_(this, mutex) {
        ignore_map_flag_no_wait_ = Config::getInstance().getOption<bool>("d3d11.ignoreMapFlagNoWait", false);
      }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) override {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(ID3D11Multithread)) {
      *ppvObject = ref(&d3dmt_);
      return S_OK;
    }

    if (riid == __uuidof(ID3D11VideoContext)) {
      *ppvObject = ref(&video_context_);
      return S_OK;
    }

    return ImmediateContextBase::QueryInterface(riid, ppvObject);
  }

  ULONG STDMETHODCALLTYPE
  AddRef() override {
    uint32_t refCount = this->refcount++;
    if (unlikely(!refCount))
      this->m_parent->AddRef();

    return refCount + 1;
  }

  ULONG STDMETHODCALLTYPE
  Release() override {
    uint32_t refCount = --this->refcount;
    D3D11_ASSERT(refCount != ~0u && "try to release a 0 reference object");
    if (unlikely(!refCount))
      this->m_parent->Release();

    return refCount;
  }

  void *
  MapDynamicBuffer(Rc<DynamicBuffer> &dynamic, uint64_t current_seq_id, uint64_t coherent_seq_id) {
    if (auto next_sub = dynamic->nextSuballocation()) {
      EmitST([allocation = dynamic->immediateName(), next_sub](ArgumentEncodingContext &enc) mutable {
        allocation->useSuballocation(next_sub);
      });
    } else {
      dynamic->updateImmediateName(current_seq_id, dynamic->allocate(coherent_seq_id), 0, false);
      EmitST([allocation = dynamic->immediateName(),
              buffer = Rc(dynamic->buffer)](ArgumentEncodingContext &enc) mutable {
        allocation->useSuballocation(0);
        auto _ = buffer->rename(forward_rc(allocation));
      });
    }

    return dynamic->immediateMappedMemory();
  }

  HRESULT
  STDMETHODCALLTYPE
  Map(ID3D11Resource *pResource, UINT Subresource, D3D11_MAP MapType, UINT MapFlags,
      D3D11_MAPPED_SUBRESOURCE *pMappedResource) override {
    dxmt::apitrace::begin_d3d_call("ID3D11DeviceContext::Map");
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    if (unlikely(!pResource || !pMappedResource))
      return E_INVALIDARG;
    if (auto common = GetResourceCommon(pResource))
      FlushPendingBufferUpdatesFor(common->buffer().ptr());
    UINT buffer_length = 0, &row_pitch = buffer_length;
    UINT bind_flag = 0, &depth_pitch = bind_flag;
    auto current_seq_id = cmd_queue.CurrentSeqId();
    auto coherent_seq_id = cmd_queue.CoherentSeqId();
    if (auto dynamic = GetDynamicBuffer(pResource, &buffer_length, &bind_flag)) {
      switch (MapType) {
      case D3D11_MAP_READ:
      case D3D11_MAP_WRITE:
      case D3D11_MAP_READ_WRITE:
        return E_INVALIDARG;
      case D3D11_MAP_WRITE_DISCARD: {
        if (bind_flag & D3D11_BIND_VERTEX_BUFFER) {
          state_.InputAssembler.VertexBuffers.set_dirty();
        }
        if (bind_flag & D3D11_BIND_CONSTANT_BUFFER) {
          for (auto &stage : state_.ShaderStages) {
            stage.ConstantBuffers.set_dirty();
          }
        }
        if (bind_flag & D3D11_BIND_SHADER_RESOURCE) {
          for (auto &stage : state_.ShaderStages) {
            stage.SRVs.set_dirty();
          }
        }

        pMappedResource->pData = MapDynamicBuffer(dynamic, current_seq_id, coherent_seq_id);
        pMappedResource->RowPitch = buffer_length;
        pMappedResource->DepthPitch = buffer_length;
        break;
      }
      case D3D11_MAP_WRITE_NO_OVERWRITE: {
        pMappedResource->pData = dynamic->immediateMappedMemory();
        pMappedResource->RowPitch = buffer_length;
        pMappedResource->DepthPitch = buffer_length;
        break;
      }
      }
      return S_OK;
    }
    if (auto dynamic = GetDynamicTexture(pResource, Subresource, &row_pitch, &depth_pitch)) {
      switch (MapType) {
      case D3D11_MAP_READ:
      case D3D11_MAP_WRITE:
      case D3D11_MAP_READ_WRITE:
      case D3D11_MAP_WRITE_NO_OVERWRITE:
        return E_INVALIDARG;
      case D3D11_MAP_WRITE_DISCARD: {
        for (auto &stage : state_.ShaderStages) {
          stage.SRVs.set_dirty();
        }

        pMappedResource->pData = MapDynamicBuffer(dynamic, current_seq_id, coherent_seq_id);
        pMappedResource->RowPitch = row_pitch;
        pMappedResource->DepthPitch = depth_pitch;
        break;
      }
      }
      return S_OK;
    }
    if (auto dynamic = GetDynamicLinearTexture(pResource, &row_pitch, &depth_pitch)) {
      switch (MapType) {
      case D3D11_MAP_READ:
      case D3D11_MAP_WRITE:
      case D3D11_MAP_READ_WRITE:
        return E_INVALIDARG;
      case D3D11_MAP_WRITE_DISCARD: {
        for (auto &stage : state_.ShaderStages) {
          stage.SRVs.set_dirty();
        }

        dynamic->updateImmediateName(current_seq_id, dynamic->allocate(coherent_seq_id), false);
        EmitST([allocation = dynamic->immediateName(),
              texture = Rc(dynamic->texture)](ArgumentEncodingContext &enc) mutable {
          auto _ = texture->rename(forward_rc(allocation));
        });

        pMappedResource->pData = dynamic->mappedMemory();
        pMappedResource->RowPitch = row_pitch;
        pMappedResource->DepthPitch = depth_pitch;
        break;
      }
      case D3D11_MAP_WRITE_NO_OVERWRITE: {
        pMappedResource->pData = dynamic->mappedMemory();
        pMappedResource->RowPitch = row_pitch;
        pMappedResource->DepthPitch = depth_pitch;
        break;
      }
      }
      return S_OK;
    }
    if (auto staging = GetStagingResource(pResource, Subresource)) {
      if (MapType > 3 || MapType == 0)
          return E_INVALIDARG;

      if (ignore_map_flag_no_wait_)
        MapFlags &= ~D3D11_MAP_FLAG_DO_NOT_WAIT;

      while (true) {
        auto result = staging->tryMap(coherent_seq_id, MapType & D3D11_MAP_READ, MapType & D3D11_MAP_WRITE);
        if (result == StagingMapResult::Mapped)
          return E_FAIL;
        if (result == StagingMapResult::Renamable) {
          // when write to a buffer that is gpu-readonly
          auto next_name = staging->allocate(coherent_seq_id);
          // can't guarantee a full overwrite
          std::memcpy(staging->mappedMemory(next_name), staging->mappedImmediateMemory(), staging->length);
          staging->updateImmediateName(current_seq_id, next_name);
          EmitST([staging, next_name](ArgumentEncodingContext &enc) mutable { 
            auto _ = staging->buffer()->rename(staging->allocation(next_name));
          });
          result = StagingMapResult::Mappable;
        }
        if (result == StagingMapResult::Mappable) {
          TRACE("staging map ready");
          pMappedResource->pData = staging->mappedImmediateMemory();
          pMappedResource->RowPitch = staging->bytesPerRow;
          pMappedResource->DepthPitch = staging->bytesPerImage;
          return S_OK;
        }
        if (MapFlags & D3D11_MAP_FLAG_DO_NOT_WAIT) {
          return DXGI_ERROR_WAS_STILL_DRAWING;
        }
        // even it's in a while loop
        // only the first flush will have effect
        // and the following calls are essentially no-op
        Flush();
        TRACE("staging map block");
        auto& statistics = cmd_queue.CurrentFrameStatistics();
        auto t0 = clock::now();
        cmd_queue.WaitCPUFence(coherent_seq_id + uint64_t(result));
        auto t1 = clock::now();
        statistics.sync_count++;
        statistics.sync_interval += (t1 - t0);
        current_seq_id = cmd_queue.CurrentSeqId();
        coherent_seq_id = cmd_queue.CoherentSeqId();
      };
    };
    if (pMappedResource == nullptr) {
      UNIMPLEMENTED("map-on-default: map");
    }
    return E_INVALIDARG;
  }

  void
  STDMETHODCALLTYPE
  Unmap(ID3D11Resource *pResource, UINT Subresource) override {
    dxmt::apitrace::begin_d3d_call("ID3D11DeviceContext::Unmap");
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    if (unlikely(!pResource))
      return;
    UINT row_pitch = 0;
    UINT depth_pitch = 0;
    if (auto staging = GetStagingResource(pResource, Subresource)) {
      staging->unmap();
    };
    UINT buffer_length = 0;
    UINT bind_flag = 0;
    if (auto dynamic = GetDynamicBuffer(pResource, &buffer_length, &bind_flag)) {
      dynamic->immediateName()->flushCpuShadow(0, buffer_length, dynamic->immediateSuballocation());
      return;
    }
    if (auto dynamic = GetDynamicTexture(pResource, Subresource, &row_pitch, &depth_pitch)) {
      dynamic->immediateName()->flushCpuShadow(
          0,
          dynamic->buffer->length(),
          dynamic->immediateSuballocation()
      );
      BlitObject texture(device, pResource);
      UpdateTexture(TextureUpdateCommand(texture, Subresource, nullptr),
                    dynamic->buffer, row_pitch, depth_pitch);
    }
  }

  void
  STDMETHODCALLTYPE
  Begin(ID3D11Asynchronous *pAsync) override {
    if (unlikely(!pAsync))
      return;

    std::lock_guard<d3d11_device_mutex> lock(mutex);

    FlushAllPendingBufferUpdates();
    if (auto counter = com_cast<IMTLD3D11CounterExt>(pAsync)) {
      counter->BeginCounter();
      EmitOP([counter = std::move(counter)](ArgumentEncodingContext &enc) mutable {
        counter->EncodeBeginCounter(&enc);
      });
      return;
    }

    // in theory pAsync could be any of them: { Query, Predicate, Counter }.
    // However `Predicate` is still not supported.
    D3D11_QUERY_DESC desc;
    ((ID3D11Query *)pAsync)->GetDesc(&desc);
    switch (desc.Query) {
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
    case D3D11_QUERY_TIMESTAMP:
    case D3D11_QUERY_EVENT:
      break;
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_OCCLUSION_PREDICATE: {
      if (auto query = static_cast<MTLD3D11OcclusionQuery *>(pAsync)->Begin())
        EmitST([query = Rc(query)](ArgumentEncodingContext &enc) mutable {
          enc.beginVisibilityResultQuery(std::move(query));
        });
      break;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: {
      // ignore
      break;
    }
    default:
      ERR("Unknown query type ", desc.Query);
      break;
    }
  }

  // See Begin()
  void
  STDMETHODCALLTYPE
  End(ID3D11Asynchronous *pAsync) override {
    if (unlikely(!pAsync))
      return;

    std::lock_guard<d3d11_device_mutex> lock(mutex);

    if (auto counter = com_cast<IMTLD3D11CounterExt>(pAsync)) {
      counter->EndCounter();
      EmitOP([counter = std::move(counter)](ArgumentEncodingContext &enc) mutable {
        counter->EncodeEndCounter(&enc);
      });
      promote_flush = true;
      return;
    }

    D3D11_QUERY_DESC desc;
    ((ID3D11Query *)pAsync)->GetDesc(&desc);
    switch (desc.Query) {
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
    case D3D11_QUERY_PIPELINE_STATISTICS:
    case D3D11_QUERY_EVENT: {
    if (!pending_buffer_updates_.empty() || ctx_state.has_dirty_op_since_last_event) {
        FlushAllPendingBufferUpdates();
        auto event_id = cmd_queue.GetNextEventSeqId();
        static_cast<MTLD3D11EventQuery *>(pAsync)->Issue(event_id);
        InvalidateCurrentPass(true);
        EmitOP([event_id](ArgumentEncodingContext &enc) mutable {
          enc.signalEvent(event_id);
        });
        promote_flush = true;
        ctx_state.has_dirty_op_since_last_event = false;
      } else {
        static_cast<MTLD3D11EventQuery *>(pAsync)->Issue(cmd_queue.GetCurrentEventSeqId());
      }
      break;
    }
    case D3D11_QUERY_TIMESTAMP: {
      if (auto query = static_cast<MTLD3D11TimestampQuery *>(pAsync)->End()) {
        InvalidateCurrentPass(true);
        EmitOP([query = Rc(query)](ArgumentEncodingContext &enc) mutable {
          enc.sampleTimestamp(std::move(query));
        });
        promote_flush = true;
      }
      break;
    }
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_OCCLUSION_PREDICATE: {
      if (auto query = static_cast<MTLD3D11OcclusionQuery *>(pAsync)->End())
        EmitST([query = Rc(query)](ArgumentEncodingContext &enc) mutable {
          enc.endVisibilityResultQuery(std::move(query));
        });
      promote_flush = true;
      break;
    }
    default:
      ERR("Unknown query type ", desc.Query);
      break;
    }
  }

  HRESULT
  STDMETHODCALLTYPE
  GetData(ID3D11Asynchronous *pAsync, void *pData, UINT DataSize, UINT GetDataFlags) override {
    if (!pAsync || (DataSize && !pData))
      return E_INVALIDARG;

    // Allow dataSize to be zero
    if (DataSize && DataSize != pAsync->GetDataSize())
      return E_INVALIDARG;

    if (auto counter = com_cast<IMTLD3D11CounterExt>(pAsync)) {
      uint64_t null_data = 0;
      auto *data_ptr = pData ? static_cast<uint64_t *>(pData) : &null_data;
      auto hr = counter->GetCounterData(data_ptr);
      if (hr == S_FALSE &&
          (GetDataFlags & D3D11_ASYNC_GETDATA_DONOTFLUSH) == 0) {
        cmd_queue.CurrentFrameStatistics().event_stall++;
        Flush();
      }
      return hr;
    }

    HRESULT hr = S_FALSE;

    D3D11_QUERY_DESC desc;
    ((ID3D11Query *)pAsync)->GetDesc(&desc);
    switch (desc.Query) {
    case D3D11_QUERY_EVENT:
    case D3D11_QUERY_TIMESTAMP_DISJOINT:
    case D3D11_QUERY_PIPELINE_STATISTICS: {
      switch (static_cast<MTLD3D11EventQuery *>(pAsync)->CheckEventState(cmd_queue.SignaledEventSeqId())) {
      case EventState::Pending:
        break;
      case EventState::Stall:
        GetDataFlags &= ~D3D11_ASYNC_GETDATA_DONOTFLUSH;
        break;
      case EventState::Signaled:
        hr = S_OK;
        break;
      case EventState::Invalid:
        return DXGI_ERROR_INVALID_CALL;
      }
      break;
    }
    default:
      break;
    }
    switch (desc.Query) {
    case D3D11_QUERY_EVENT: {
      if (pData)
        *static_cast<BOOL *>(pData) = (hr == S_OK);
      break;
    }
    case D3D11_QUERY_OCCLUSION: {
      uint64_t null_data;
      uint64_t *data_ptr = pData ? (uint64_t *)pData : &null_data;
      hr = static_cast<MTLD3D11OcclusionQuery *>(pAsync)->GetData(data_ptr);
      break;
    }
    case D3D11_QUERY_OCCLUSION_PREDICATE: {
      BOOL null_data;
      BOOL *data_ptr = pData ? (BOOL *)pData : &null_data;
      hr = static_cast<MTLD3D11OcclusionQuery *>(pAsync)->GetData(data_ptr);
      break;
    }
    case D3D11_QUERY_TIMESTAMP: {
      uint64_t null_data;
      uint64_t *data_ptr = pData ? (uint64_t *)pData : &null_data;
      hr = static_cast<MTLD3D11TimestampQuery *>(pAsync)->GetData(data_ptr);
      break;
    }
    case D3D11_QUERY_TIMESTAMP_DISJOINT: {
      if (pData) {
        (*static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT *>(pData)) = {1'000'000'000, FALSE};
      }
      break;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: {
      if (hr == S_OK && pData) {
        (*static_cast<D3D11_QUERY_DATA_PIPELINE_STATISTICS *>(pData)) = {};
      }
      break;
    }
    default:
      ERR("Unknown query type ", desc.Query);
      return E_FAIL;
    }
    if (hr == S_FALSE && (GetDataFlags & D3D11_ASYNC_GETDATA_DONOTFLUSH) == 0) {
      cmd_queue.CurrentFrameStatistics().event_stall++;
      Flush();
    }
    return hr;
  }

  void
  STDMETHODCALLTYPE
  Flush() override {
    Flush1(D3D11_CONTEXT_TYPE_ALL, NULL);
  }

  void
  STDMETHODCALLTYPE
  Flush1(D3D11_CONTEXT_TYPE Type, HANDLE hEvent) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    if (!pending_buffer_updates_.empty() || ctx_state.has_dirty_op_since_last_event || promote_flush) {
      FlushAllPendingBufferUpdates();
      InvalidateCurrentPass(true);
      Commit();
    }
    if (hEvent) {
      auto shared_event_listener = cmd_queue.GetSharedEventListener();
      MTLSharedEvent_setWin32EventAtValue(
          cmd_queue.event, shared_event_listener, hEvent, cmd_queue.GetCurrentEventSeqId()
      );
    }
  }

  void PrepareFlush() override {
    FlushAllPendingBufferUpdates();
    InvalidateCurrentPass(true);
  }

  void
  STDMETHODCALLTYPE
  ExecuteCommandList(ID3D11CommandList *pCommandList, BOOL RestoreContextState) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    auto *command_list = dynamic_cast<MTLD3D11CommandList *>(pCommandList);
    if (!command_list) {
      WARN("D3D11ImmediateContext: ExecuteCommandList called with an invalid command list");
      return;
    }

    ResetEncodingContextState();

    Com<MTLD3D11CommandList, false> cmdlist = command_list;
    auto seq_id = ctx_state.cmd_queue.CurrentSeqId();

    promote_flush = cmdlist->promote_flush;

    auto query_list = AllocateCommandData<Rc<VisibilityResultQuery>>(cmdlist->visibility_query_count);
    for (const auto &[query, index] : cmdlist->issued_visibility_query) {
      query_list[index] = new VisibilityResultQuery();
      query->DoDeferredQuery(query_list[index].ptr());
    }

    for (const auto &query : cmdlist->issued_event_query) {
      End(query.ptr());
    }

    for (const auto &query : cmdlist->issued_timestamp_query) {
      End(query.ptr());
    }

    for (const auto &staging : cmdlist->read_staging_resources) {
      staging->useCopySource(seq_id);
    }

    for (const auto &staging : cmdlist->written_staging_resources) {
      staging->useCopyDestination(seq_id);
    }

    for (const auto &used_dynamic : cmdlist->used_dynamic_buffers) {
      if (!used_dynamic.latest)
        continue;
      used_dynamic.buffer->updateImmediateName(seq_id, Rc(used_dynamic.allocation), used_dynamic.suballocation, true);
    }

    for (const auto &used_dynamic : cmdlist->used_dynamic_lineartextures) {
      if (!used_dynamic.latest)
        continue;
      used_dynamic.texture->updateImmediateName(seq_id, Rc(used_dynamic.allocation), true);
    }

    EmitOP([cmdlist = std::move(cmdlist), query_list = std::move(query_list)](ArgumentEncodingContext &enc) {
      // Finished command list should clean up the encoding context
      enc.pushDeferredVisibilityQueries(query_list.data());
      cmdlist->Execute(enc);
      enc.popDeferredVisibilityQueries();
    });

    if (RestoreContextState)
      RestoreEncodingContextState();
    else
      ResetD3D11ContextState();
  }

  HRESULT
  STDMETHODCALLTYPE
  FinishCommandList(BOOL RestoreDeferredContextState, ID3D11CommandList **ppCommandList) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    return DXGI_ERROR_INVALID_CALL;
  }

  D3D11_DEVICE_CONTEXT_TYPE
  STDMETHODCALLTYPE
  GetType() override {
    return D3D11_DEVICE_CONTEXT_IMMEDIATE;
  }

  virtual void
  WaitUntilGPUIdle() override {
    FlushAllPendingBufferUpdates();
    uint64_t seq = cmd_queue.CurrentSeqId();
    if(!InvalidateCurrentPass())
      Commit();
    cmd_queue.WaitCPUFence(seq);
  };

  void
  Commit() override {
    promote_flush = false;
    D3D11_ASSERT(cmdbuf_state == CommandBufferState::Idle);
    ctx_state.cmd_queue.CommitCurrentChunk();
    ctx_state.has_dirty_op_since_last_event = false;
  };

  HRESULT KeyedMutexAcquire(WMT::Reference<WMT::SharedEvent> event, UINT64 Value) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    FlushAllPendingBufferUpdates();
    Flush();
    EmitOP([event = std::move(event), Value](ArgumentEncodingContext &enc) mutable {
      enc.waitEvent(std::move(event), Value);
    });

    return S_OK;
  }

  HRESULT KeyedMutexRelease(WMT::Reference<WMT::SharedEvent> event, UINT64 Value) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    FlushAllPendingBufferUpdates();
    InvalidateCurrentPass();
    EmitOP([event = std::move(event), Value](ArgumentEncodingContext &enc) mutable {
      enc.signalEvent(std::move(event), Value);
    });
    Flush();

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Signal(ID3D11Fence *pFence, UINT64 Value) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    auto fence = dynamic_cast<MTLD3D11Fence *>(pFence);
    if (!fence)
      return E_INVALIDARG;
    Com<ID3D11Device> owner;
    fence->GetDevice(&owner);
    if (owner.ptr() != this->m_parent)
      return E_INVALIDARG;

    FlushAllPendingBufferUpdates();
    InvalidateCurrentPass();
    EmitOP([event = fence->event, Value](ArgumentEncodingContext &enc) mutable {
      enc.signalEvent(std::move(event), Value);
    });
    Flush();

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Wait(ID3D11Fence *pFence, UINT64 Value) override {
    std::lock_guard<d3d11_device_mutex> lock(mutex);

    auto fence = dynamic_cast<MTLD3D11Fence *>(pFence);
    if (!fence)
      return E_INVALIDARG;

    FlushAllPendingBufferUpdates();
    Flush();
    EmitOP([event = fence->event, Value](ArgumentEncodingContext &enc) mutable {
      enc.waitEvent(std::move(event), Value);
    });

    return S_OK;
  }

  UINT
  STDMETHODCALLTYPE
  GetContextFlags() override {
    return 0;
  }

private:
  std::vector<Com<MTLD3D11OcclusionQuery>> pending_occlusion_queries;
  CommandQueue &cmd_queue;
  ContextInternalState ctx_state;
  std::atomic<uint32_t> refcount = 0;
  MTLD3D11VideoContext video_context_;
  D3D11Multithread d3dmt_;
  bool ignore_map_flag_no_wait_;
};

std::unique_ptr<MTLD3D11DeviceContextBase>
InitializeImmediateContext(MTLD3D11Device *pDevice, CommandQueue &cmd_queue) {
  return std::make_unique<MTLD3D11ImmediateContext>(pDevice, cmd_queue);
}

}; // namespace dxmt
