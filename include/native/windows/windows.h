#pragma once

#include "windows_base.h"
#include "unknwn.h"

#ifndef _WIN32
#include <dlfcn.h>

inline HMODULE LoadLibraryA(LPCSTR name) {
  return reinterpret_cast<HMODULE>(dlopen(name, RTLD_NOW | RTLD_GLOBAL));
}

inline FARPROC GetProcAddress(HMODULE module, LPCSTR name) {
  return module ? reinterpret_cast<FARPROC>(dlsym(module, name)) : nullptr;
}

inline DWORD GetLastError() {
  return dlerror() ? 1u : 0u;
}

inline SIZE_T VirtualQuery(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T) {
  return 0;
}

#endif
