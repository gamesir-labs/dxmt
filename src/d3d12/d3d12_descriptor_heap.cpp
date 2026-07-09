#include "d3d12_descriptor_heap.hpp"
#include "d3d12_descriptor_mirror.hpp"

#include "com/com_guid.hpp"
#include "com/com_object.hpp"
#include "com/com_private_data.hpp"
#include "log/log.hpp"
#include "util_string.hpp"

#include <memory>

namespace dxmt::d3d12 {
namespace {

std::atomic<uint64_t> g_descriptor_content_generation{1};

bool DescriptorHeapSupportsMirror(const D3D12_DESCRIPTOR_HEAP_DESC &desc) {
  if (!(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
    return false;

  return desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
         desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
}

class DescriptorHeapImpl final
    : public ComObjectWithInitialRef<ID3D12DescriptorHeap>,
      public DescriptorHeap {
public:
  DescriptorHeapImpl(IMTLD3D12Device *device,
                     const D3D12_DESCRIPTOR_HEAP_DESC &desc)
      : device_(device), desc_(desc), records_(desc.NumDescriptors) {
    for (UINT i = 0; i < desc.NumDescriptors; i++) {
      records_[i].magic = DescriptorRecord::kMagic;
      records_[i].heap_type = desc.Type;
      records_[i].shader_visible =
          (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
      records_[i].cpu_handle.ptr =
          reinterpret_cast<SIZE_T>(&records_[i]);
      records_[i].heap_index = i;
      records_[i].heap_count = desc.NumDescriptors;
    }
    // Eagerly allocate the unified descriptor owner for shader-visible
    // CBV/SRV/UAV and SAMPLER heaps, and back-fill the per-record back-pointer.
    // The owner preserves the typed mirror buffers and owns the MSC descriptor
    // table buffer plus the Metal4 argument table that binds that buffer at bind
    // point 0 (resources) or 1 (samplers). Doing this at construction avoids
    // cross-thread first-touch ordering between descriptor writes and replay.
    const bool sampler_heap = desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    if (DescriptorHeapSupportsMirror(desc_)) {
      mirror_ = std::make_unique<DescriptorHeapMirror>(
          device_->GetMTLDevice(), desc.NumDescriptors, sampler_heap);
      for (UINT i = 0; i < desc.NumDescriptors; i++)
        records_[i].mirror = mirror_.get();
    }
  }

  ~DescriptorHeapImpl() {
    if (!mirror_)
      return;
    auto &queue = device_->GetDXMTDevice().queue();
    for (auto &target : mirror_->DrainResidencyTargets()) {
      if (target.allocation)
        queue.RemovePersistentResidencyAfterCompletion(target.allocation);
      if (target.secondary_allocation)
        queue.RemovePersistentResidencyAfterCompletion(
            target.secondary_allocation);
      if (target.sampler)
        queue.RetainUntilGpuComplete(
            [sampler = std::move(target.sampler)]() mutable {
              sampler = nullptr;
            });
    }
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    if (!ppvObject)
      return E_POINTER;

    *ppvObject = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) ||
        riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) ||
        riid == __uuidof(ID3D12DescriptorHeap)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D12DescriptorHeap), riid))
      WARN("D3D12DescriptorHeap: unknown interface query ", str::format(riid));
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
  D3D12_DESCRIPTOR_HEAP_DESC *STDMETHODCALLTYPE
  GetDesc(D3D12_DESCRIPTOR_HEAP_DESC *__ret) override {
    *__ret = desc_;
    return __ret;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetCPUDescriptorHandleForHeapStart(D3D12_CPU_DESCRIPTOR_HANDLE *__ret) override {
    *__ret = GetCPUDescriptorHandleForHeapStartImpl();
    return __ret;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE *STDMETHODCALLTYPE
  GetGPUDescriptorHandleForHeapStart(D3D12_GPU_DESCRIPTOR_HANDLE *__ret) override {
    *__ret = GetGPUDescriptorHandleForHeapStartImpl();
    return __ret;
  }
#else
  D3D12_DESCRIPTOR_HEAP_DESC STDMETHODCALLTYPE GetDesc() override {
    return desc_;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE
  GetCPUDescriptorHandleForHeapStart() override {
    return GetCPUDescriptorHandleForHeapStartImpl();
  }

  D3D12_GPU_DESCRIPTOR_HANDLE STDMETHODCALLTYPE
  GetGPUDescriptorHandleForHeapStart() override {
    return GetGPUDescriptorHandleForHeapStartImpl();
  }
#endif

  const D3D12_DESCRIPTOR_HEAP_DESC &GetDescriptorHeapDesc() const override {
    return desc_;
  }

  DescriptorRecord *
  GetDescriptorRecord(D3D12_CPU_DESCRIPTOR_HANDLE handle) override {
    return DescriptorRecordFromHandle(handle);
  }

  const DescriptorRecord *
  GetDescriptorRecord(D3D12_CPU_DESCRIPTOR_HANDLE handle) const override {
    return DescriptorRecordFromHandle(handle);
  }

  const DescriptorRecord *
  GetDescriptorRecord(D3D12_GPU_DESCRIPTOR_HANDLE handle) const override {
    return DescriptorRecordFromHandle({static_cast<SIZE_T>(handle.ptr)});
  }

  DescriptorHeapMirror *GetMirror() override { return mirror_.get(); }

private:
  D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandleForHeapStartImpl() const {
    if (records_.empty())
      return {};
    return records_[0].cpu_handle;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandleForHeapStartImpl() const {
    if (!(desc_.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) ||
        records_.empty())
      return {};

    D3D12_GPU_DESCRIPTOR_HANDLE handle = {};
    handle.ptr = records_[0].cpu_handle.ptr;
    return handle;
  }

  DescriptorRecord *DescriptorRecordFromHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    auto *record = reinterpret_cast<DescriptorRecord *>(handle.ptr);
    if (!record || records_.empty())
      return nullptr;
    if (record < records_.data() || record >= records_.data() + records_.size())
      return nullptr;
    return record;
  }

  const DescriptorRecord *
  DescriptorRecordFromHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const {
    auto *record = reinterpret_cast<const DescriptorRecord *>(handle.ptr);
    if (!record || records_.empty())
      return nullptr;
    if (record < records_.data() || record >= records_.data() + records_.size())
      return nullptr;
    return record;
  }

  Com<IMTLD3D12Device> device_;
  ComPrivateData private_data_;
  D3D12_DESCRIPTOR_HEAP_DESC desc_ = {};
  std::vector<DescriptorRecord> records_;
  std::string name_;
  std::unique_ptr<DescriptorHeapMirror> mirror_;
};

} // namespace

bool IsBindlessMirrorEnabled() {
  return true;
}

void BumpDescriptorContentGeneration() {
  auto value = g_descriptor_content_generation.fetch_add(
                   1, std::memory_order_relaxed) + 1;
  if (!value)
    g_descriptor_content_generation.store(1, std::memory_order_relaxed);
}

uint64_t GetDescriptorContentGeneration() {
  return g_descriptor_content_generation.load(std::memory_order_relaxed);
}

Com<ID3D12DescriptorHeap>
CreateDescriptorHeap(IMTLD3D12Device *device,
                     const D3D12_DESCRIPTOR_HEAP_DESC *desc) {
  return Com<ID3D12DescriptorHeap>::transfer(new DescriptorHeapImpl(device, *desc));
}

DescriptorRecord *
GetDescriptorRecordFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
  auto *record = reinterpret_cast<DescriptorRecord *>(handle.ptr);
  if (!record || record->magic != DescriptorRecord::kMagic)
    return nullptr;
  return record;
}

DescriptorRecord *
GetDescriptorRecordFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                 D3D12_DESCRIPTOR_HEAP_TYPE expected_type) {
  auto *record = GetDescriptorRecordFromCpuHandle(handle);
  if (!record)
    return nullptr;
  if (record->heap_type != expected_type) {
    WARN("D3D12DescriptorHeap: descriptor heap type mismatch expected=",
         uint32_t(expected_type), " actual=", uint32_t(record->heap_type));
    return nullptr;
  }
  return record;
}

const DescriptorRecord *
GetDescriptorRecordFromGpuHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle,
                                 D3D12_DESCRIPTOR_HEAP_TYPE expected_type) {
  auto *record =
      GetDescriptorRecordFromCpuHandle({static_cast<SIZE_T>(handle.ptr)},
                                       expected_type);
  if (!record || !record->shader_visible) {
    if (record)
      WARN("D3D12DescriptorHeap: non shader-visible descriptor used as GPU handle");
    return nullptr;
  }
  return record;
}

DescriptorRecord *
GetDescriptorRecordRangeFromCpuHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle,
                                      D3D12_DESCRIPTOR_HEAP_TYPE expected_type,
                                      UINT descriptor_count,
                                      const char *context) {
  auto *record = GetDescriptorRecordFromCpuHandle(handle, expected_type);
  if (!record)
    return nullptr;
  if (descriptor_count &&
      (record->heap_index >= record->heap_count ||
       descriptor_count > record->heap_count - record->heap_index)) {
    WARN("D3D12DescriptorHeap: descriptor range exceeds heap for ",
         context ? context : "<unknown>", " start=", record->heap_index,
         " count=", descriptor_count, " heap_count=", record->heap_count);
    return nullptr;
  }
  return record;
}

} // namespace dxmt::d3d12
