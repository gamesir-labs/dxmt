#include "com/com_pointer.hpp"
#include "d3d11_device.hpp"
#include "d3d11_enumerable.hpp"
#include "d3d11_view.hpp"
#include "dxmt_dynamic.hpp"
#include "dxmt_staging.hpp"
#include "dxmt_texture.hpp"
#include "d3d11_resource.hpp"
#include "util_win32_compat.h"

namespace dxmt {

#pragma region DeviceTexture

template <typename desc_t>
static UINT
TextureDescHeight(const desc_t &desc) {
  if constexpr (std::is_same_v<desc_t, D3D11_TEXTURE1D_DESC>) {
    return 1;
  } else {
    return desc.Height;
  }
}

template <typename desc_t>
static UINT
TextureDescDepth(const desc_t &desc) {
  if constexpr (std::is_same_v<desc_t, D3D11_TEXTURE3D_DESC1>) {
    return desc.Depth;
  } else {
    return 1;
  }
}

template <typename desc_t>
static UINT
TextureDescArraySize(const desc_t &desc) {
  if constexpr (std::is_same_v<desc_t, D3D11_TEXTURE3D_DESC1>) {
    return 1;
  } else {
    return desc.ArraySize;
  }
}

template <typename desc_t>
static UINT
TextureDescSampleCount(const desc_t &desc) {
  if constexpr (std::is_same_v<desc_t, D3D11_TEXTURE2D_DESC1>) {
    return desc.SampleDesc.Count;
  } else {
    return 1;
  }
}

static constexpr SIZE_T kD3DKMTExistingHeapPageSize = 0x1000;

static SIZE_T
AlignD3DKMTExistingHeapSize(SIZE_T size) {
  if (!size)
    size = kD3DKMTExistingHeapPageSize;
  return (size + kD3DKMTExistingHeapPageSize - 1) & ~(kD3DKMTExistingHeapPageSize - 1);
}

template <typename desc_t>
static SIZE_T
CalculateD3DKMTExistingHeapSize(MTLD3D11Device *pDevice, const desc_t &desc) {
  SIZE_T size = 0;
  auto mipLevels = desc.MipLevels ? desc.MipLevels : 1;
  auto arraySize = TextureDescArraySize(desc);
  auto sampleCount = TextureDescSampleCount(desc);

  for (UINT level = 0; level < mipLevels; level++) {
    uint32_t bytesPerRow = 0;
    uint32_t bytesPerImage = 0;
    uint32_t bytesPerSlice = 0;
    if (FAILED(GetLinearTextureLayout(pDevice, desc, level, bytesPerRow, bytesPerImage, bytesPerSlice, true)))
      return kD3DKMTExistingHeapPageSize;
    size += SIZE_T(bytesPerSlice) * arraySize * sampleCount;
  }

  return AlignD3DKMTExistingHeapSize(size);
}

template <typename tag_texture>
class DeviceTexture : public TResourceBase<tag_texture, IMTLMinLODClampable> {
private:
  Rc<RenamableTexturePool> renamable_;
  float min_lod = 0.0;
  D3DKMT_HANDLE local_kmt_ = 0;
  D3DKMT_HANDLE global_kmt_ = 0;

  static UINT
  DebugDescHeight(const typename tag_texture::DESC1 &desc) {
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE1D_DESC>) {
      return 1;
    } else {
      return desc.Height;
    }
  }

  static UINT
  DebugDescDepth(const typename tag_texture::DESC1 &desc) {
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      return desc.Depth;
    } else {
      return 1;
    }
  }

  static UINT
  DebugDescArraySize(const typename tag_texture::DESC1 &desc) {
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      return 1;
    } else {
      return desc.ArraySize;
    }
  }

  static UINT
  DebugDescSampleCount(const typename tag_texture::DESC1 &desc) {
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE2D_DESC1>) {
      return desc.SampleDesc.Count;
    } else {
      return 1;
    }
  }

  static UINT
  DebugDescSampleQuality(const typename tag_texture::DESC1 &desc) {
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE2D_DESC1>) {
      return desc.SampleDesc.Quality;
    } else {
      return 0;
    }
  }

  void
  LogRenderTargetViewFailure(const char *reason, const D3D11_RENDER_TARGET_VIEW_DESC1 &viewDesc) const {
    WARN(
        "CreateRenderTargetView: ", reason,
        " resource=", reinterpret_cast<const void *>(this),
        " dimension=", uint32_t(tag_texture::dimension),
        " resource_format=", uint32_t(this->desc.Format),
        " view_format=", uint32_t(viewDesc.Format),
        " view_dimension=", uint32_t(viewDesc.ViewDimension),
        " bind_flags=", this->desc.BindFlags,
        " metal_usage=", uint32_t(this->texture_->usage()),
        " usage=", uint32_t(this->desc.Usage),
        " cpu_access=", this->desc.CPUAccessFlags,
        " misc_flags=", this->desc.MiscFlags,
        " size=", this->desc.Width, "x", DebugDescHeight(this->desc), "x", DebugDescDepth(this->desc),
        " array_size=", DebugDescArraySize(this->desc),
        " mip_levels=", this->desc.MipLevels,
        " sample=", DebugDescSampleCount(this->desc), ":", DebugDescSampleQuality(this->desc)
    );
  }

  // GDI DC interop state
  HDC dc_ = nullptr;
  HBITMAP dc_bitmap_ = nullptr;
  void *dc_bits_ = nullptr;
  Com<ID3D11Texture2D1> dc_staging_;
  bool surface_mapped_ = false;

  using SRVBase =
      TResourceViewBase<tag_shader_resource_view<DeviceTexture<tag_texture>>>;
  class TextureSRV : public SRVBase {
  public:
    TextureSRV(const TextureViewDescriptor &descriptor,
               const tag_shader_resource_view<>::DESC1 *pDesc,
               DeviceTexture *pResource, MTLD3D11Device *pDevice)
        : SRVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      this->view_id_ = this->texture_->createView(descriptor);
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength()
      );
    }

  };

  using UAVBase =
      TResourceViewBase<tag_unordered_access_view<DeviceTexture<tag_texture>>>;
  class TextureUAV : public UAVBase {
  public:
    TextureUAV(const TextureViewDescriptor &descriptor,
               const tag_unordered_access_view<>::DESC1 *pDesc,
               DeviceTexture *pResource, MTLD3D11Device *pDevice)
        : UAVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      this->view_id_ = this->texture_->createView(descriptor);
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength()
      );
    }
  };

  using RTVBase =
      TResourceViewBase<tag_render_target_view<DeviceTexture<tag_texture>>>;
  class TextureRTV : public RTVBase {
  public:
    TextureRTV(
        const TextureViewDescriptor &descriptor, WMTPixelFormat view_format, const tag_render_target_view<>::DESC1 *pDesc,
        DeviceTexture *pResource, MTLD3D11Device *pDevice, const MTL_RENDER_PASS_ATTACHMENT_DESC &mtl_rtv_desc
    ) :
        RTVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      auto rtv_descriptor = descriptor;
      rtv_descriptor.intendedUsage = WMTTextureUsageRenderTarget;
      this->view_id_ = this->texture_->createView(rtv_descriptor);
      this->format_ = view_format;
      this->pass_desc_ = mtl_rtv_desc;
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength()
      );
    }
  };

  using DSVBase =
      TResourceViewBase<tag_depth_stencil_view<DeviceTexture<tag_texture>>>;
  class TextureDSV : public DSVBase {
  public:
    TextureDSV(
        const TextureViewDescriptor &descriptor, WMTPixelFormat view_format, const tag_depth_stencil_view<>::DESC1 *pDesc,
        DeviceTexture *pResource, MTLD3D11Device *pDevice, const MTL_RENDER_PASS_ATTACHMENT_DESC &attachment_desc
    ) :
        DSVBase(pDesc, pResource, pDevice) {
      this->texture_ = pResource->texture_.ptr();
      auto dsv_descriptor = descriptor;
      dsv_descriptor.intendedUsage = WMTTextureUsageRenderTarget;
      this->view_id_ = this->texture_->createView(dsv_descriptor);
      this->format_ = view_format;
      this->pass_desc_ = attachment_desc;
      this->renamable_ = pResource->renamable_.ptr();
      this->readonly_flags_ = this->desc.Flags & 0b11;
      this->subset_ = ResourceSubsetState(
        &descriptor,
        this->texture_->miplevelCount(),
        this->texture_->arrayLength(),
        this->readonly_flags_
      );
    }
  };

public:
  DeviceTexture(const tag_texture::DESC1 *pDesc, Rc<Texture> &&u_texture, MTLD3D11Device *pDevice) :
      TResourceBase<tag_texture, IMTLMinLODClampable>(*pDesc, pDevice) {
        this->texture_ = std::move(u_texture);
      }

  DeviceTexture(
      const tag_texture::DESC1 *pDesc, Rc<Texture> &&u_texture, Rc<RenamableTexturePool> &&renamable,
      MTLD3D11Device *pDevice
  ) :
      TResourceBase<tag_texture, IMTLMinLODClampable>(*pDesc, pDevice),
      renamable_(std::move(renamable)) {
        this->texture_ = std::move(u_texture);
      }

  DeviceTexture(
      const tag_texture::DESC1 *pDesc, Rc<Texture> &&u_texture, D3DKMT_HANDLE localHandle, D3DKMT_HANDLE globalHandle,
      MTLD3D11Device *pDevice
  ) :
      TResourceBase<tag_texture, IMTLMinLODClampable>(*pDesc, pDevice),
      local_kmt_(localHandle), global_kmt_(globalHandle) {
        this->texture_ = std::move(u_texture);
      }

  ~DeviceTexture() {
    if (local_kmt_) {
      D3DKMT_DESTROYALLOCATION destroy = {};
      destroy.hDevice = this->m_parent->GetLocalD3DKMT();
      destroy.hResource = local_kmt_;
      D3DKMTDestroyAllocation(&destroy);
    }
  }

  Rc<StagingResource> staging(UINT) final { return nullptr; }
  Rc<DynamicBuffer> dynamicBuffer(UINT*, UINT*) final { return {}; }
  Rc<DynamicLinearTexture> dynamicLinearTexture(UINT*, UINT*) final { return {}; };
  Rc<DynamicBuffer> dynamicTexture(UINT , UINT *, UINT *) final { return {}; };

  HRESULT STDMETHODCALLTYPE CreateRenderTargetView(const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc,
                                 ID3D11RenderTargetView1 **ppView) override {
    D3D11_RENDER_TARGET_VIEW_DESC1 finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      return E_INVALIDARG;
    }
    if (!(this->desc.BindFlags & D3D11_BIND_RENDER_TARGET)) {
      LogRenderTargetViewFailure("resource missing D3D11_BIND_RENDER_TARGET", finalDesc);
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    MTL_RENDER_PASS_ATTACHMENT_DESC attachment_desc;
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc,
            attachment_desc, descriptor
        ))) {
      return E_FAIL;
    }
    if (!(this->texture_->usage() & WMTTextureUsageRenderTarget)) {
      LogRenderTargetViewFailure("texture usage missing WMTTextureUsageRenderTarget", finalDesc);
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureRTV(descriptor, descriptor.format, &finalDesc, this, this->m_parent, attachment_desc));
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE CreateDepthStencilView(const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
                                 ID3D11DepthStencilView **ppView) override {
    D3D11_DEPTH_STENCIL_VIEW_DESC finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    MTL_RENDER_PASS_ATTACHMENT_DESC attachment_desc;
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc,
            attachment_desc, descriptor
        ))) {
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureDSV(descriptor, descriptor.format, &finalDesc, this, this->m_parent, attachment_desc));
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc,
                           ID3D11ShaderResourceView1 **ppView) override {
    D3D11_SHADER_RESOURCE_VIEW_DESC1 finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      ERR("DeviceTexture: Failed to create SRV descriptor");
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc, descriptor
        ))) {
      ERR("DeviceTexture: Failed to create texture SRV");
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureSRV(descriptor, &finalDesc, this, this->m_parent));
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  CreateUnorderedAccessView(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc,
                            ID3D11UnorderedAccessView1 **ppView) override {
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 finalDesc;
    if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc,
                                                    &finalDesc))) {
      return E_INVALIDARG;
    }
    TextureViewDescriptor descriptor;
    uint32_t arraySize;
    if constexpr (std::is_same_v<typename tag_texture::DESC1, D3D11_TEXTURE3D_DESC1>) {
      arraySize = this->desc.Depth;
    } else {
      arraySize = this->desc.ArraySize;
    }
    if (FAILED(InitializeAndNormalizeViewDescriptor(
            this->m_parent, this->desc.MipLevels, arraySize, this->texture_.ptr(), finalDesc, descriptor
        ))) {
      ERR("DeviceTexture: Failed to create texture UAV");
      return E_FAIL;
    }
    if (!ppView) {
      return S_FALSE;
    }
    *ppView = ref(new TextureUAV(descriptor, &finalDesc, this, this->m_parent));
    return S_OK;
  };

  virtual HRESULT
  GetSharedHandle(HANDLE *pSharedHandle) override {
    if (pSharedHandle == nullptr || (this->desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)) {
      return E_INVALIDARG;
    }

    if (!(this->desc.MiscFlags & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))) {
      *pSharedHandle = NULL;
      return S_OK;
    }

    if (!global_kmt_) {
      return E_INVALIDARG;
    }

    *pSharedHandle = reinterpret_cast<HANDLE>(global_kmt_);
    return S_OK;
  }

  virtual HRESULT
  CreateSharedHandle(const SECURITY_ATTRIBUTES *Attributes, DWORD Access, const WCHAR *pName, HANDLE *pNTHandle)
      override {
    InitReturnPtr(pNTHandle);
    if (!local_kmt_)
      return E_INVALIDARG;

    OBJECT_ATTRIBUTES attr = {};
    attr.Length = sizeof(attr);
    attr.SecurityDescriptor = const_cast<SECURITY_ATTRIBUTES*>(Attributes);

    WCHAR buffer[MAX_PATH];
    UNICODE_STRING name_str;
    if (pName) {
      DWORD session, len, name_len = wcslen(pName);

      ProcessIdToSessionId(GetCurrentProcessId(), &session);
      len = swprintf(buffer, ARRAYSIZE(buffer), L"\\Sessions\\%u\\BaseNamedObjects\\", session);
      memcpy(buffer + len, pName, (name_len + 1) * sizeof(WCHAR));
      name_str.MaximumLength = name_str.Length = (len + name_len) * sizeof(WCHAR);
      name_str.MaximumLength += sizeof(WCHAR);
      name_str.Buffer = buffer;

      attr.ObjectName = &name_str;
      attr.Attributes = OBJ_CASE_INSENSITIVE;
    }

    if (D3DKMTShareObjects(1, &local_kmt_, &attr, Access, pNTHandle)) {
      ERR("DeviceTexture: Failed to create shared handle");
      return E_FAIL;
    }

    return S_OK;
  }

  void SetMinLOD(float MinLod) override { min_lod = MinLod; }

  float GetMinLOD() override { return min_lod; }

  ~DeviceTexture() {
    if (dc_bitmap_) {
      ::DeleteObject(dc_bitmap_);
    }
    if (dc_) {
      ::DeleteDC(dc_);
    }
    if (local_kmt_) {
      D3DKMT_DESTROYALLOCATION destroy = {};
      destroy.hDevice = this->m_parent->GetLocalD3DKMT();
      destroy.hResource = local_kmt_;
      D3DKMTDestroyAllocation(&destroy);
    }
  }

  HRESULT EnsureStagingTexture() {
    if constexpr (!std::is_same_v<tag_texture, tag_texture_2d>) {
      return E_NOTIMPL;
    } else {
      if (dc_staging_)
        return S_OK;
      D3D11_TEXTURE2D_DESC1 staging_desc = {};
      staging_desc.Width = this->desc.Width;
      staging_desc.Height = this->desc.Height;
      staging_desc.MipLevels = 1;
      staging_desc.ArraySize = 1;
      staging_desc.Format = this->desc.Format;
      staging_desc.SampleDesc.Count = 1;
      staging_desc.SampleDesc.Quality = 0;
      staging_desc.Usage = D3D11_USAGE_STAGING;
      staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
      return CreateStagingTexture2D(this->m_parent, &staging_desc, nullptr, &dc_staging_);
    }
  }

  void FlushAndSync() {
    auto *ctx = this->m_parent->GetImmediateContextPrivate();
    ctx->PrepareFlush();
    ctx->Commit();
    ctx->WaitUntilGPUIdle();
  }

  HRESULT GetSurfaceDC(WINBOOL Discard, HDC *phdc) override {
    if constexpr (!std::is_same_v<tag_texture, tag_texture_2d>) {
      return E_NOTIMPL;
    } else {
      if (!phdc)
        return E_INVALIDARG;
      *phdc = nullptr;

      HRESULT hr = EnsureStagingTexture();
      if (FAILED(hr))
        return hr;

      auto *ctx = this->m_parent->GetImmediateContextPrivate();

      // copy device texture → staging, then sync
      if (!Discard) {
        ctx->CopyResource(dc_staging_.ptr(), static_cast<D3D11ResourceCommon *>(this));
        FlushAndSync();
      }

      // map staging for read
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      hr = ctx->Map(dc_staging_.ptr(), 0, D3D11_MAP_READ, 0, &mapped);
      if (FAILED(hr))
        return hr;

      // create DC and DIB on first use
      if (!dc_) {
        dc_ = ::CreateCompatibleDC(nullptr);
        if (!dc_) {
          ctx->Unmap(dc_staging_.ptr(), 0);
          return E_FAIL;
        }
      }

      if (!dc_bitmap_) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = static_cast<LONG>(this->desc.Width);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(this->desc.Height);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dc_bitmap_ = ::CreateDIBSection(dc_, &bmi, DIB_RGB_COLORS, &dc_bits_, nullptr, 0);
        if (!dc_bitmap_ || !dc_bits_) {
          if (dc_bitmap_) {
            ::DeleteObject(dc_bitmap_);
            dc_bitmap_ = nullptr;
          }
          ctx->Unmap(dc_staging_.ptr(), 0);
          return E_FAIL;
        }
        ::SelectObject(dc_, dc_bitmap_);
      }

      // copy pixel data from staging to DIB
      if (!Discard) {
        UINT dc_pitch = this->desc.Width * 4u;
        auto *src = static_cast<const uint8_t *>(mapped.pData);
        auto *dst = static_cast<uint8_t *>(dc_bits_);
        for (UINT y = 0; y < this->desc.Height; y++) {
          memcpy(dst + y * dc_pitch, src + y * mapped.RowPitch, dc_pitch);
        }
      }

      ctx->Unmap(dc_staging_.ptr(), 0);

      *phdc = dc_;
      return S_OK;
    }
  }

  HRESULT ReleaseSurfaceDC(RECT *pDirtyRect) override {
    if constexpr (!std::is_same_v<tag_texture, tag_texture_2d>) {
      return E_NOTIMPL;
    } else {
      if (!dc_ || !dc_bits_)
        return E_FAIL;

      auto *ctx = this->m_parent->GetImmediateContextPrivate();

      // determine dirty region
      RECT dirty;
      if (pDirtyRect && !IsRectEmpty(pDirtyRect)) {
        dirty = *pDirtyRect;
        // clamp to texture bounds
        if (dirty.left < 0) dirty.left = 0;
        if (dirty.top < 0) dirty.top = 0;
        if (dirty.right > (LONG)this->desc.Width) dirty.right = this->desc.Width;
        if (dirty.bottom > (LONG)this->desc.Height) dirty.bottom = this->desc.Height;
      } else {
        dirty.left = 0;
        dirty.top = 0;
        dirty.right = this->desc.Width;
        dirty.bottom = this->desc.Height;
      }

      // write DIB data back → staging → device texture
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      HRESULT hr = ctx->Map(dc_staging_.ptr(), 0, D3D11_MAP_WRITE, 0, &mapped);
      if (FAILED(hr))
        return hr;

      UINT dc_pitch = this->desc.Width * 4u;
      auto *src = static_cast<const uint8_t *>(dc_bits_);
      auto *dst = static_cast<uint8_t *>(mapped.pData);
      UINT row_bytes = (dirty.right - dirty.left) * 4u;
      for (LONG y = dirty.top; y < dirty.bottom; y++) {
        memcpy(
          dst + y * mapped.RowPitch + dirty.left * 4u,
          src + y * dc_pitch + dirty.left * 4u,
          row_bytes
        );
      }

      ctx->Unmap(dc_staging_.ptr(), 0);

      // copy staging → device texture
      D3D11_BOX box = {};
      box.left = dirty.left;
      box.top = dirty.top;
      box.right = dirty.right;
      box.bottom = dirty.bottom;
      box.front = 0;
      box.back = 1;
      ctx->CopySubresourceRegion(
        static_cast<D3D11ResourceCommon *>(this), 0,
        dirty.left, dirty.top, 0,
        dc_staging_.ptr(), 0, &box
      );

      return S_OK;
    }
  }

  HRESULT SurfaceMap(DXGI_MAPPED_RECT *pLockedRect, UINT MapFlags) override {
    if constexpr (!std::is_same_v<tag_texture, tag_texture_2d>) {
      return E_NOTIMPL;
    } else {
      if (!pLockedRect)
        return E_INVALIDARG;
      if (surface_mapped_)
        return DXGI_ERROR_WAS_STILL_DRAWING;

      HRESULT hr = EnsureStagingTexture();
      if (FAILED(hr))
        return hr;

      auto *ctx = this->m_parent->GetImmediateContextPrivate();

      // read: copy device → staging and sync
      if (MapFlags & DXGI_MAP_READ) {
        ctx->CopyResource(dc_staging_.ptr(), static_cast<D3D11ResourceCommon *>(this));
        FlushAndSync();
      }

      D3D11_MAP d3d11_map;
      if ((MapFlags & DXGI_MAP_READ) && (MapFlags & DXGI_MAP_WRITE))
        d3d11_map = D3D11_MAP_READ_WRITE;
      else if (MapFlags & DXGI_MAP_WRITE)
        d3d11_map = D3D11_MAP_WRITE;
      else
        d3d11_map = D3D11_MAP_READ;

      D3D11_MAPPED_SUBRESOURCE mapped = {};
      hr = ctx->Map(dc_staging_.ptr(), 0, d3d11_map, 0, &mapped);
      if (FAILED(hr))
        return hr;

      pLockedRect->Pitch = mapped.RowPitch;
      pLockedRect->pBits = static_cast<BYTE *>(mapped.pData);
      surface_mapped_ = true;
      return S_OK;
    }
  }

  HRESULT SurfaceUnmap() override {
    if constexpr (!std::is_same_v<tag_texture, tag_texture_2d>) {
      return E_NOTIMPL;
    } else {
      if (!surface_mapped_)
        return E_FAIL;

      auto *ctx = this->m_parent->GetImmediateContextPrivate();
      ctx->Unmap(dc_staging_.ptr(), 0);

      // write back staging → device
      ctx->CopyResource(static_cast<D3D11ResourceCommon *>(this), dc_staging_.ptr());

      surface_mapped_ = false;
      return S_OK;
    }
  }
};

struct SharedResourceData {
  char mach_port_name[54];
  D3D11_RESOURCE_DIMENSION dimension;
  union {
    D3D11_TEXTURE1D_DESC desc1d;
    D3D11_TEXTURE2D_DESC1 desc2d;
    D3D11_TEXTURE3D_DESC1 desc3d;
  } desc;
};

template <typename tag>
HRESULT CreateDeviceTextureInternal(MTLD3D11Device *pDevice,
                                    const typename tag::DESC1 *pDesc,
                                    const D3D11_SUBRESOURCE_DATA *pInitialData,
                                    typename tag::COM_IMPL **ppTexture) {
  WMTTextureInfo info;
  typename tag::DESC1 finalDesc;
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &info))) {
    return E_INVALIDARG;
  }
  bool single_subresource = info.mipmap_level_count == 1 && info.array_length == 1 &&
                            !(finalDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE);
  auto texture = Rc<Texture>(new Texture(info, pDevice->GetMTLDevice()));

  auto &initializer = pDevice->GetDXMTDevice().queue().initializer;

  auto initialize = [&](Rc<TextureAllocation> &&allocation) {
    texture->rename(std::move(allocation));
    if (!pInitialData) {
      for (auto sub : EnumerateSubresources(finalDesc)) {
        initializer.initWithZero(texture.ptr(), texture->current(), sub.ArraySlice, sub.MipLevel);
      }
    } else {
      for (auto sub : EnumerateSubresources(finalDesc)) {
        auto &data = pInitialData[sub.SubresourceId];
        initializer.initWithData(
            texture.ptr(), texture->current(), sub.ArraySlice, sub.MipLevel, data.pSysMem, data.SysMemPitch,
            data.SysMemSlicePitch
        );
      }
    }
  };

  auto shared_flag =
      D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  if (finalDesc.MiscFlags & shared_flag) {
    auto local_kmt = pDevice->GetLocalD3DKMT();
    if (!local_kmt) {
      ERR(
          "DeviceTexture: Invalid device handle",
          " local_kmt=", local_kmt,
          " misc_flags=", finalDesc.MiscFlags,
          " dimension=", uint32_t(tag::dimension),
          " size=", finalDesc.Width, "x", TextureDescHeight(finalDesc), "x", TextureDescDepth(finalDesc)
      );
      return E_FAIL;
    }
    // use a dedicated path for now, because there are other works for private storage mode

    Flags<TextureAllocationFlag> flags;
    flags.set(TextureAllocationFlag::GpuPrivate);
    if (finalDesc.Usage == D3D11_USAGE_IMMUTABLE)
      flags.set(TextureAllocationFlag::GpuReadonly);
    if (!(finalDesc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)))
      flags.set(TextureAllocationFlag::ShaderReadonly);
    flags.set(TextureAllocationFlag::Shared);
    auto allocation = texture->allocate(flags);

    mach_port_t mach_port = allocation->machPort;
    if (!mach_port) {
      ERR("DeviceTexture: Failed to get mach port for shared texture");
      return E_FAIL;
    }
    SharedResourceData runtimeData;
    MakeUniqueSharedName(runtimeData.mach_port_name);
    if (!WMTBootstrapRegister(runtimeData.mach_port_name, mach_port)) {
      ERR("DeviceTexture: Failed to register mach port for shared texture");
      return E_FAIL;
    }
    runtimeData.dimension = tag::dimension;
    memcpy(&runtimeData.desc, pDesc, sizeof(typename tag::DESC1));

    D3DKMT_CREATEALLOCATION create = {};
    create.hDevice = local_kmt;
    create.pPrivateRuntimeData = &runtimeData;
    create.PrivateRuntimeDataSize = sizeof(runtimeData);
    create.Flags.StandardAllocation = 1;
    create.NumAllocations = 1;
    D3DDDI_ALLOCATIONINFO allocationInfo = {};
    create.pAllocationInfo = &allocationInfo;
    D3DKMT_CREATESTANDARDALLOCATION standardAllocation = {};
    create.pStandardAllocation = &standardAllocation;
    standardAllocation.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    standardAllocation.ExistingHeapData.Size = CalculateD3DKMTExistingHeapSize(pDevice, finalDesc);
    create.Flags.ExistingSysMem = 1;
    alignas(kD3DKMTExistingHeapPageSize) static const char systemMem[kD3DKMTExistingHeapPageSize] = {};
    allocationInfo.pSystemMem = systemMem;
    create.Flags.CreateResource = 1;
    create.Flags.CreateShared = 1;
    create.Flags.NtSecuritySharing = !!(finalDesc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
    auto status = D3DKMTCreateAllocation2(&create);
    if (status) {
      ERR(
          "DeviceTexture: Failed to create D3DKMT for shared texture",
          " status=", status,
          " hDevice=", create.hDevice,
          " hResource=", create.hResource,
          " hGlobalShare=", create.hGlobalShare,
          " nt_security=", create.Flags.NtSecuritySharing,
          " existing_heap_size=", standardAllocation.ExistingHeapData.Size,
          " allocation_private_size=", allocationInfo.PrivateDriverDataSize
      );
      return E_FAIL;
    }

    // TODO: handle keyed mutex

    initialize(std::move(allocation));
    *ppTexture = reinterpret_cast<typename tag::COM_IMPL *>(
        ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), create.hResource,
                                   create.hGlobalShare, pDevice))
    );
    return S_OK;
  }

  Flags<TextureAllocationFlag> flags;
  flags.set(finalDesc.CPUAccessFlags ? TextureAllocationFlag::GpuManaged : TextureAllocationFlag::GpuPrivate);
  if (!(finalDesc.BindFlags & (D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)))
    flags.set(TextureAllocationFlag::ShaderReadonly);
  if (finalDesc.Usage == D3D11_USAGE_IMMUTABLE)
    flags.set(TextureAllocationFlag::GpuReadonly);
  if (single_subresource && (finalDesc.BindFlags & D3D11_BIND_DEPTH_STENCIL)) {
    Rc<RenamableTexturePool> renamable = new RenamableTexturePool(texture.ptr(), 32, flags);
    initialize(renamable->getNext(0));
    *ppTexture = reinterpret_cast<typename tag::COM_IMPL *>(
        ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), std::move(renamable), pDevice)));
  } else {
    initialize(texture->allocate(flags));
    *ppTexture = reinterpret_cast<typename tag::COM_IMPL *>(
        ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), pDevice)));
  }
  return S_OK;
}

HRESULT
CreateDeviceTexture1D(MTLD3D11Device *pDevice,
                      const D3D11_TEXTURE1D_DESC *pDesc,
                      const D3D11_SUBRESOURCE_DATA *pInitialData,
                      ID3D11Texture1D **ppTexture) {
  return CreateDeviceTextureInternal<tag_texture_1d>(pDevice, pDesc,
                                                     pInitialData, ppTexture);
}

HRESULT
CreateDeviceTexture2D(MTLD3D11Device *pDevice,
                      const D3D11_TEXTURE2D_DESC1 *pDesc,
                      const D3D11_SUBRESOURCE_DATA *pInitialData,
                      ID3D11Texture2D1 **ppTexture) {
  return CreateDeviceTextureInternal<tag_texture_2d>(pDevice, pDesc,
                                                     pInitialData, ppTexture);
}

HRESULT
CreateDeviceTexture3D(MTLD3D11Device *pDevice,
                      const D3D11_TEXTURE3D_DESC1 *pDesc,
                      const D3D11_SUBRESOURCE_DATA *pInitialData,
                      ID3D11Texture3D1 **ppTexture) {
  return CreateDeviceTextureInternal<tag_texture_3d>(pDevice, pDesc,
                                                     pInitialData, ppTexture);
}

template <typename tag>
HRESULT
ImportSharedTextureInternal(
    MTLD3D11Device *pDevice, const typename tag::DESC1 *pDescUnchecked, mach_port_t MachPort, REFIID riid,
    void **ppTexture
) {
  WMTTextureInfo info;
  typename tag::DESC1 finalDesc;
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDescUnchecked, &finalDesc, &info)))
    return E_INVALIDARG;

  auto texture = Rc<Texture>(new Texture(info, pDevice->GetMTLDevice()));
  auto allocation = texture->import(MachPort);
  if (!allocation)
    return E_FAIL;
  texture->rename(std::move(allocation));

  Com<DeviceTexture<tag>> device_texture = (ref(new DeviceTexture<tag>(&finalDesc, std::move(texture), pDevice)));
  return device_texture->QueryInterface(riid, ppTexture);
}

HRESULT
ImportSharedTexture(MTLD3D11Device *pDevice, HANDLE hResource, REFIID riid, void **ppTexture) {
  InitReturnPtr(ppTexture);

  if (!(reinterpret_cast<uintptr_t>(hResource) & 0xc0000000)) {
    WARN("ImportSharedTexture: Invalid shared handle type");
    return E_INVALIDARG;
  }

  if (ppTexture == nullptr)
    return S_FALSE;

  struct SharedResourceData runtimeData;

  D3DKMT_QUERYRESOURCEINFO query = {};
  query.hDevice = pDevice->GetLocalD3DKMT();
  query.hGlobalShare = reinterpret_cast<uintptr_t>(hResource);
  query.pPrivateRuntimeData = &runtimeData;
  query.PrivateRuntimeDataSize = sizeof(runtimeData);

  if (D3DKMTQueryResourceInfo(&query)) {
    WARN("ImportSharedTexture: Failed to query resource: ", hResource);
    return E_INVALIDARG;
  }

  if (query.PrivateRuntimeDataSize != sizeof(runtimeData)) {
    WARN("ImportSharedTexture: Unexpected size: ", query.PrivateRuntimeDataSize);
    return E_INVALIDARG;
  } 

  D3DDDI_OPENALLOCATIONINFO2 alloc = {};
  D3DKMT_OPENRESOURCE open = {};
  open.hDevice = pDevice->GetLocalD3DKMT();
  open.hGlobalShare = reinterpret_cast<uintptr_t>(hResource);
  open.NumAllocations = 1;
  open.pOpenAllocationInfo2 = &alloc;
  open.pPrivateRuntimeData = &runtimeData;
  open.PrivateRuntimeDataSize = query.PrivateRuntimeDataSize;

  if (D3DKMTOpenResource2(&open)) {
    WARN("ImportSharedTexture: Failed to open resource: ", hResource);
    return E_INVALIDARG;
  }

  D3DKMT_DESTROYALLOCATION destroy = {};
  destroy.hDevice = pDevice->GetLocalD3DKMT();
  destroy.hResource = open.hResource;
  D3DKMTDestroyAllocation(&destroy);

  mach_port_t mach_port;
  if (!WMTBootstrapLookUp(runtimeData.mach_port_name, &mach_port)) {
    ERR("ImportSharedTexture: Failed to look up mach port");
    return E_INVALIDARG;
  }

  switch (runtimeData.dimension)
  {
  case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
    return ImportSharedTextureInternal<tag_texture_1d>(
        pDevice, &runtimeData.desc.desc1d, mach_port, riid, ppTexture
    );
  case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
    return ImportSharedTextureInternal<tag_texture_2d>(
        pDevice, &runtimeData.desc.desc2d, mach_port, riid, ppTexture
    );
  case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
    return ImportSharedTextureInternal<tag_texture_3d>(
        pDevice, &runtimeData.desc.desc3d, mach_port, riid, ppTexture
    );
  default:
    ERR("ImportSharedTexture: Unsupported resource dimension");
    return E_INVALIDARG;
  }
}

HRESULT
ImportSharedTextureFromNtHandle(MTLD3D11Device *pDevice, HANDLE hResource, REFIID riid, void **ppTexture) {
  InitReturnPtr(ppTexture);

  if (reinterpret_cast<uintptr_t>(hResource) & 0xc0000000) {
    WARN("ImportSharedTextureFromNtHandle: Invalid shared handle type");
    return E_INVALIDARG;
  }

  if (ppTexture == nullptr)
    return S_FALSE;

  struct SharedResourceData runtimeData;

  D3DKMT_QUERYRESOURCEINFOFROMNTHANDLE query = {};
  query.hDevice = pDevice->GetLocalD3DKMT();
  query.hNtHandle = hResource;
  query.pPrivateRuntimeData = &runtimeData;
  query.PrivateRuntimeDataSize = sizeof(runtimeData);

  if (D3DKMTQueryResourceInfoFromNtHandle(&query)) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Failed to query resource: ", hResource));
    return E_INVALIDARG;
  }
  
  if (query.PrivateRuntimeDataSize != sizeof(runtimeData)) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Unexpected size: ", query.PrivateRuntimeDataSize));
    return E_INVALIDARG;
  }

  D3DDDI_OPENALLOCATIONINFO2 alloc = {};
  D3DKMT_OPENRESOURCEFROMNTHANDLE open = {};
  char dummy;

  open.hDevice = pDevice->GetLocalD3DKMT();
  open.hNtHandle = hResource;
  open.NumAllocations = 1;
  open.pOpenAllocationInfo2 = &alloc;
  open.pPrivateRuntimeData = &runtimeData;
  open.PrivateRuntimeDataSize = query.PrivateRuntimeDataSize;
  open.pTotalPrivateDriverDataBuffer = &dummy;
  open.TotalPrivateDriverDataBufferSize = 0;

  if (D3DKMTOpenResourceFromNtHandle(&open)) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Failed to open resource: ", hResource));
    return E_INVALIDARG;
  }

  D3DKMT_DESTROYALLOCATION destroy = {};
  destroy.hDevice = pDevice->GetLocalD3DKMT();
  destroy.hResource = open.hResource;
  D3DKMTDestroyAllocation(&destroy);

  if (open.hSyncObject) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Ignoring bundled sync object"));
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroySync = {};
    destroySync.hSyncObject = open.hSyncObject;
    D3DKMTDestroySynchronizationObject(&destroySync);
  }
  if (open.hKeyedMutex) {
    WARN(str::format("ImportSharedTextureFromNtHandle: Ignoring bundled keyed mutex"));
    D3DKMT_DESTROYKEYEDMUTEX destroyMutex = {};
    destroyMutex.hKeyedMutex = open.hKeyedMutex;
    D3DKMTDestroyKeyedMutex(&destroyMutex);
  }

  mach_port_t mach_port;
  if (!WMTBootstrapLookUp(runtimeData.mach_port_name, &mach_port)) {
    ERR("ImportSharedTexture: Failed to look up mach port");
    return E_INVALIDARG;
  }

  switch (runtimeData.dimension)
  {
  case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
    return ImportSharedTextureInternal<tag_texture_1d>(
        pDevice, &runtimeData.desc.desc1d, mach_port, riid, ppTexture
    );
  case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
    return ImportSharedTextureInternal<tag_texture_2d>(
        pDevice, &runtimeData.desc.desc2d, mach_port, riid, ppTexture
    );
  case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
    return ImportSharedTextureInternal<tag_texture_3d>(
        pDevice, &runtimeData.desc.desc3d, mach_port, riid, ppTexture
    );
  default:
    ERR("ImportSharedTexture: Unsupported resource dimension");
    return E_INVALIDARG;
  }
}

HRESULT
ImportSharedTextureByName(
    MTLD3D11Device *pDevice, LPCWSTR lpName, DWORD dwDesiredAccess, REFIID riid, void **ppTexture
) {
  D3DKMT_OPENNTHANDLEFROMNAME openFromName = {};
  openFromName.dwDesiredAccess = dwDesiredAccess;

  OBJECT_ATTRIBUTES attr = {};
  attr.Length = sizeof(attr);

  WCHAR buffer[MAX_PATH];
  UNICODE_STRING name_str;
  DWORD session, len, name_len = wcslen(lpName);

  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  len = swprintf(buffer, ARRAYSIZE(buffer), L"\\Sessions\\%u\\BaseNamedObjects\\", session);
  memcpy(buffer + len, lpName, (name_len + 1) * sizeof(WCHAR));
  name_str.MaximumLength = name_str.Length = (len + name_len) * sizeof(WCHAR);
  name_str.MaximumLength += sizeof(WCHAR);
  name_str.Buffer = buffer;

  attr.ObjectName = &name_str;
  attr.Attributes = OBJ_CASE_INSENSITIVE;
  openFromName.pObjAttrib = &attr;

  if (D3DKMTOpenNtHandleFromName(&openFromName)) {
    WARN(str::format("ImportSharedTextureByName: Failed to open NT handle from name: ", lpName));
    return E_INVALIDARG;
  }

  HRESULT res = ImportSharedTextureFromNtHandle(pDevice, openFromName.hNtHandle, riid, ppTexture);
  CloseHandle(openFromName.hNtHandle);
  return res;
}

#pragma endregion

} // namespace dxmt
