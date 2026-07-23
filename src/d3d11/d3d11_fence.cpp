#include "d3d11_fence.hpp"
#include "d3d11_device_child.hpp"
#include "d3d11_resource.hpp"
#include "util_win32_compat.h"

#include <cstdint>
#include <cstdio>

namespace dxmt {

namespace {

constexpr ULONG kObjectNameInformation = 1;

using NtQueryObjectProc =
    NTSTATUS(WINAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);

void
MakeSharedFenceBootstrapName(const WCHAR *name, size_t length,
                             char (&bootstrap_name)[54]) {
  constexpr uint64_t kFnvOffset = 1469598103934665603ull;
  constexpr uint64_t kFnvPrime = 1099511628211ull;
  uint64_t forward = kFnvOffset;
  uint64_t reverse = kFnvOffset ^ 0x9e3779b97f4a7c15ull;

  for (size_t i = 0; i < length; ++i) {
    const uint16_t value = static_cast<uint16_t>(name[i]);
    forward = (forward ^ static_cast<uint8_t>(value)) * kFnvPrime;
    forward = (forward ^ static_cast<uint8_t>(value >> 8)) * kFnvPrime;
  }
  for (size_t i = length; i > 0; --i) {
    const uint16_t value = static_cast<uint16_t>(name[i - 1]);
    reverse = (reverse ^ static_cast<uint8_t>(value >> 8)) * kFnvPrime;
    reverse = (reverse ^ static_cast<uint8_t>(value)) * kFnvPrime;
  }

  std::snprintf(bootstrap_name, sizeof(bootstrap_name),
                "DXMT_shared_fence_%016llx%016llx",
                static_cast<unsigned long long>(forward),
                static_cast<unsigned long long>(reverse));
}

bool
GetSharedFenceBootstrapName(HANDLE handle, char (&bootstrap_name)[54]) {
  const auto query_object = reinterpret_cast<NtQueryObjectProc>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
  if (!query_object)
    return false;

  alignas(void *) char buffer[sizeof(UNICODE_STRING) +
                              MAX_PATH * sizeof(WCHAR) + 16] = {};
  ULONG returned_size = 0;
  if (query_object(handle, kObjectNameInformation, buffer, sizeof(buffer),
                   &returned_size))
    return false;

  const auto *object_name = reinterpret_cast<const UNICODE_STRING *>(buffer);
  if (!object_name->Buffer || !object_name->Length ||
      object_name->Length % sizeof(WCHAR) || returned_size > sizeof(buffer) ||
      returned_size < sizeof(UNICODE_STRING) + object_name->Length)
    return false;

  MakeSharedFenceBootstrapName(
      object_name->Buffer, object_name->Length / sizeof(WCHAR), bootstrap_name);
  return true;
}

} // namespace

class MTLD3D11FenceImpl : public MTLD3D11DeviceChild<MTLD3D11Fence> {
public:
  MTLD3D11FenceImpl(MTLD3D11Device *pDevice, WMT::Reference<WMT::SharedEvent> event, D3DKMT_HANDLE handle) :
      MTLD3D11DeviceChild<MTLD3D11Fence>(pDevice) {
    this->event = std::move(event);
    local_kmt = handle;
  };

  ~MTLD3D11FenceImpl() {
    if (local_kmt) {
      D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy = {};
      destroy.hSyncObject = local_kmt;
      D3DKMTDestroySynchronizationObject(&destroy);
    }
  };

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11Fence)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D11Query), riid)) {
      WARN("D3D11Fence: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  };

  HRESULT STDMETHODCALLTYPE
  CreateSharedHandle(const SECURITY_ATTRIBUTES *pAttributes, DWORD Access,
                     const WCHAR *Name, HANDLE *pHandle) final {
    InitReturnPtr(pHandle);
    if (!local_kmt)
      return E_INVALIDARG;

    OBJECT_ATTRIBUTES attr = {};
    attr.Length = sizeof(attr);
    if (pAttributes) {
      attr.SecurityDescriptor = pAttributes->lpSecurityDescriptor;
      if (pAttributes->bInheritHandle)
        attr.Attributes |= OBJ_INHERIT;
    }

    char anonymous_name[54];
    WCHAR anonymous_name_wide[ARRAYSIZE(anonymous_name)] = {};
    if (!Name) {
      MakeUniqueSharedName(anonymous_name);
      for (size_t i = 0; anonymous_name[i]; ++i)
        anonymous_name_wide[i] = static_cast<unsigned char>(anonymous_name[i]);
      Name = anonymous_name_wide;
    }

    WCHAR buffer[MAX_PATH];
    UNICODE_STRING name_str;
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session))
      return E_FAIL;
    const int prefix_length =
        swprintf(buffer, ARRAYSIZE(buffer),
                 L"\\Sessions\\%u\\BaseNamedObjects\\", session);
    const size_t name_length = wcslen(Name);
    if (prefix_length < 0 || static_cast<size_t>(prefix_length) + name_length >=
                                 ARRAYSIZE(buffer))
      return E_INVALIDARG;
    memcpy(buffer + prefix_length, Name, (name_length + 1) * sizeof(WCHAR));
    name_str.Length =
        (static_cast<size_t>(prefix_length) + name_length) * sizeof(WCHAR);
    name_str.MaximumLength = name_str.Length + sizeof(WCHAR);
    name_str.Buffer = buffer;

    attr.ObjectName = &name_str;
    attr.Attributes |= OBJ_CASE_INSENSITIVE;

    if (D3DKMTShareObjects(1, &local_kmt, &attr, Access, pHandle)) {
      ERR("D3D11Fence: Failed to create shared handle");
      return E_FAIL;
    }

    mach_port_t mach_port = event.createMachPort();
    char bootstrap_name[54];
    MakeSharedFenceBootstrapName(
        buffer, static_cast<size_t>(prefix_length) + name_length,
        bootstrap_name);
    if (!mach_port || !WMTBootstrapRegister(bootstrap_name, mach_port)) {
      CloseHandle(*pHandle);
      *pHandle = nullptr;
      ERR("D3D11Fence: Failed to register shared event");
      return E_FAIL;
    }

    const DWORD inherit_flag =
        pAttributes && pAttributes->bInheritHandle ? HANDLE_FLAG_INHERIT : 0;
    if (!SetHandleInformation(*pHandle, HANDLE_FLAG_INHERIT, inherit_flag)) {
      CloseHandle(*pHandle);
      *pHandle = nullptr;
      return E_FAIL;
    }

    return S_OK;
  };

  UINT64 STDMETHODCALLTYPE GetCompletedValue() final {
    return event.signaledValue();
  };

  HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value,
                                                 HANDLE Event) final {
    auto shared_event_listener = this->m_parent->GetDXMTDevice().queue().GetSharedEventListener();
    MTLSharedEvent_setWin32EventAtValue(event.handle, shared_event_listener, Event, Value);
    return S_OK;
  };
};

HRESULT
CreateFence(MTLD3D11Device *pDevice, UINT64 InitialValue, D3D11_FENCE_FLAG Flags, REFIID riid, void **ppFence) {
  bool shared = !!(Flags & (D3D11_FENCE_FLAG_SHARED | D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER));
  auto event = pDevice->GetMTLDevice().newSharedEvent();
  D3DKMT_HANDLE local_kmt = 0;
  if (shared) {
    if (!pDevice->GetLocalD3DKMT()) {
      ERR("D3D11Fence: Invalid device handle", " local_kmt=", pDevice->GetLocalD3DKMT());
      return E_FAIL;
    }
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 create = {};
    create.hDevice = pDevice->GetLocalD3DKMT();
    create.Info.Type = D3DDDI_FENCE;
    create.Info.Flags.Shared = 1;
    create.Info.Flags.NtSecuritySharing = 1;
    if (D3DKMTCreateSynchronizationObject2(&create)) {
      ERR("D3D11Fence: Failed to create D3DKMT handle");
      return E_FAIL;
    }
    local_kmt = create.hSyncObject;
  }
  event.signalValue(InitialValue);
  auto fence = new MTLD3D11FenceImpl(pDevice, std::move(event), local_kmt);
  return fence->QueryInterface(riid, ppFence);
}

HRESULT
OpenSharedFence(MTLD3D11Device *pDevice, HANDLE hResource,
                REFIID riid, void **ppFence) {
  InitReturnPtr(ppFence);

  if (reinterpret_cast<uintptr_t>(hResource) & 0xc0000000) {
    WARN("OpenSharedFence: Invalid shared handle type");
    return E_INVALIDARG;
  }

  if (ppFence == nullptr)
    return S_FALSE;

  char bootstrap_name[54];
  if (!GetSharedFenceBootstrapName(hResource, bootstrap_name)) {
    WARN(str::format("OpenSharedFence: Failed to query shared fence name: ",
                     hResource));
    return E_INVALIDARG;
  }

  D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 open = {};
  open.hDevice = pDevice->GetLocalD3DKMT();
  open.hNtHandle = hResource;

  if (D3DKMTOpenSyncObjectFromNtHandle2(&open)) {
    WARN(str::format("OpenSharedFence: Failed to open resource: ", hResource));
    return E_INVALIDARG;
  }

  mach_port_t mach_port;
  if (!WMTBootstrapLookUp(bootstrap_name, &mach_port)) {
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy = {};
    destroy.hSyncObject = open.hSyncObject;
    D3DKMTDestroySynchronizationObject(&destroy);
    ERR("OpenSharedFence: Failed to look up mach port");
    return E_INVALIDARG;
  }

  auto event = pDevice->GetMTLDevice().newSharedEventWithMachPort(mach_port);
  if (!event) {
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy = {};
    destroy.hSyncObject = open.hSyncObject;
    D3DKMTDestroySynchronizationObject(&destroy);
    ERR("OpenSharedFence: Failed to import shared event");
    return E_INVALIDARG;
  }

  auto fence = new MTLD3D11FenceImpl(pDevice, std::move(event),
                                     open.hSyncObject);
  return fence->QueryInterface(riid, ppFence);
}

} // namespace dxmt
