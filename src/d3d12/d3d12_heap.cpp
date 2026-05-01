#include "d3d12_heap.hpp"

#include "com/com_guid.hpp"
#include "com/com_object.hpp"
#include "com/com_private_data.hpp"
#include "log/log.hpp"
#include "util_string.hpp"

namespace dxmt::d3d12 {
namespace {

class HeapImpl final : public ComObjectWithInitialRef<ID3D12Heap>,
                       public Heap {
public:
  HeapImpl(IMTLD3D12Device *device, const D3D12_HEAP_DESC &desc)
      : device_(device), desc_(desc),
        heap_type_(d3d12::GetHeapType(desc.Properties)),
        cpu_visible_(d3d12::IsCpuVisibleHeap(desc.Properties)),
        buffer_(new dxmt::Buffer(desc.SizeInBytes,
                                 device_->GetDXMTDevice().device())) {
    allocation_ = buffer_->allocate(GetHeapBufferAllocationFlags(desc.Properties));
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
    return S_OK;
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

  dxmt::BufferAllocation *GetAllocation() const override {
    return allocation_.ptr();
  }

private:
  Com<IMTLD3D12Device> device_;
  ComPrivateData private_data_;
  D3D12_HEAP_DESC desc_ = {};
  D3D12_HEAP_TYPE heap_type_ = D3D12_HEAP_TYPE_DEFAULT;
  bool cpu_visible_ = false;
  Rc<dxmt::Buffer> buffer_;
  Rc<dxmt::BufferAllocation> allocation_;
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

} // namespace dxmt::d3d12
