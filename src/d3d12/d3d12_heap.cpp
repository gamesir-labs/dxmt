#include "d3d12_heap.hpp"

#include "com/com_guid.hpp"
#include "com/com_object.hpp"
#include "com/com_private_data.hpp"
#include "log/log.hpp"
#include "util_string.hpp"

#include <atomic>

namespace dxmt::d3d12 {
namespace {

static bool
ShouldLogExternalCpuHeapDiag() {
  static std::atomic<uint32_t> count = 0;
  return count.fetch_add(1, std::memory_order_relaxed) < 8;
}

class HeapImpl final : public ComObjectWithInitialRef<ID3D12Heap>,
                       public Heap {
public:
  HeapImpl(IMTLD3D12Device *device, const D3D12_HEAP_DESC &desc)
      : device_(device), desc_(desc),
        heap_type_(d3d12::GetHeapType(desc.Properties)),
        cpu_visible_(d3d12::IsCpuVisibleHeap(desc.Properties)) {
    if (!desc_.Alignment)
      desc_.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    if (!desc_.Properties.CreationNodeMask)
      desc_.Properties.CreationNodeMask = 1;
    if (!desc_.Properties.VisibleNodeMask)
      desc_.Properties.VisibleNodeMask = 1;
  }

  HeapImpl(IMTLD3D12Device *device, const D3D12_HEAP_DESC &desc,
           const void *external_address)
      : HeapImpl(device, desc) {
    external_address_ = external_address;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;

    *ppvObject = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) ||
        riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Heap)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12Heap), riid))
      WARN("D3D12Heap: unknown interface query ", str::format(riid));
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
    return private_data_.setName(name);
  }

  HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override {
    return device_->QueryInterface(riid, device);
  }

#ifdef WIDL_EXPLICIT_AGGREGATE_RETURNS
  D3D12_HEAP_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_HEAP_DESC *__ret) override {
    *__ret = desc_;
    return __ret;
  }
#else
  D3D12_HEAP_DESC STDMETHODCALLTYPE GetDesc() override {
    return desc_;
  }
#endif

  const D3D12_HEAP_DESC &GetHeapDesc() const override {
    return desc_;
  }

  D3D12_HEAP_TYPE GetHeapType() const override {
    return heap_type_;
  }

  bool IsCpuVisible() const override {
    return cpu_visible_;
  }

  dxmt::Buffer *GetBuffer() const override {
    EnsureBufferAllocation();
    return buffer_.ptr();
  }

  dxmt::BufferAllocation *GetAllocation() const override {
    EnsureBufferAllocation();
    return allocation_.ptr();
  }

  void EnsureBufferAllocation() const {
    if (!allocation_) {
      buffer_ = new dxmt::Buffer(desc_.SizeInBytes,
                                 device_->GetDXMTDevice().device());
      if (external_address_) {
        if (ShouldLogExternalCpuHeapDiag()) {
          WARN("D3D12Heap: OpenExistingHeapFromAddress using external CPU backing"
           " address=", external_address_,
           " size=", desc_.SizeInBytes,
           " flags=", desc_.Flags);
        }
        allocation_ = buffer_->allocateExternalCpu(
            GetHeapBufferAllocationFlags(desc_.Properties),
            const_cast<void *>(external_address_));
      } else {
        allocation_ = buffer_->allocate(GetHeapBufferAllocationFlags(desc_.Properties));
      }
      buffer_->rename(Rc<dxmt::BufferAllocation>(allocation_));
    }
  }

  WMT::Heap GetPlacementHeap() override {
    if (placement_heap_)
      return placement_heap_;
    if (heap_type_ != D3D12_HEAP_TYPE_DEFAULT || cpu_visible_)
      return {};

    WMTPlacementHeapInfo info = {};
    info.size = desc_.SizeInBytes;
    info.options = WMTResourceStorageModePrivate |
                   WMTResourceHazardTrackingModeUntracked;
    placement_heap_ =
        device_->GetDXMTDevice().device().newPlacementHeap(info);
    if (!placement_heap_) {
      WARN("D3D12Heap: TODO failed to create Metal4 placement heap"
           " size=", desc_.SizeInBytes,
           " flags=", desc_.Flags);
    }
    return placement_heap_;
  }

private:
  Com<IMTLD3D12Device> device_;
  ComPrivateData private_data_;
  D3D12_HEAP_DESC desc_ = {};
  D3D12_HEAP_TYPE heap_type_ = D3D12_HEAP_TYPE_DEFAULT;
  bool cpu_visible_ = false;
  mutable Rc<dxmt::Buffer> buffer_;
  mutable Rc<dxmt::BufferAllocation> allocation_;
  WMT::Reference<WMT::Heap> placement_heap_;
  const void *external_address_ = nullptr;
  std::string name_;
};

} // namespace

D3D12_HEAP_TYPE
GetHeapType(const D3D12_HEAP_PROPERTIES &properties) {
  if (properties.Type != D3D12_HEAP_TYPE_CUSTOM)
    return properties.Type;

  if (properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
    return D3D12_HEAP_TYPE_DEFAULT;

  if (properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE)
    return D3D12_HEAP_TYPE_UPLOAD;

  return D3D12_HEAP_TYPE_READBACK;
}

bool
IsCpuVisibleHeap(const D3D12_HEAP_PROPERTIES &properties) {
  const auto type = GetHeapType(properties);
  return type == D3D12_HEAP_TYPE_UPLOAD || type == D3D12_HEAP_TYPE_READBACK;
}

Flags<dxmt::BufferAllocationFlag>
GetHeapBufferAllocationFlags(const D3D12_HEAP_PROPERTIES &properties) {
  Flags<dxmt::BufferAllocationFlag> flags;
  switch (GetHeapType(properties)) {
  case D3D12_HEAP_TYPE_UPLOAD:
    flags.set(dxmt::BufferAllocationFlag::CpuWriteCombined);
    break;
  case D3D12_HEAP_TYPE_READBACK:
    break;
  case D3D12_HEAP_TYPE_DEFAULT:
  default:
    flags.set(dxmt::BufferAllocationFlag::CpuInvisible);
    flags.set(dxmt::BufferAllocationFlag::GpuPrivate);
    break;
  }
  return flags;
}

Com<ID3D12Heap>
CreateHeap(IMTLD3D12Device *device, const D3D12_HEAP_DESC *desc) {
  return Com<ID3D12Heap>::transfer(new HeapImpl(device, *desc));
}

Com<ID3D12Heap>
CreateExternalCpuHeap(IMTLD3D12Device *device, const D3D12_HEAP_DESC *desc,
                      const void *address) {
  return Com<ID3D12Heap>::transfer(new HeapImpl(device, *desc, address));
}

} // namespace dxmt::d3d12
