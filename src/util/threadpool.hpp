#pragma once

#include "log/log.hpp"
#include "util_error.hpp"

namespace dxmt {

#ifdef __WIN32__

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "threadpoolapiset.h"

template <typename threadpool_trait> class threadpool {
private:
  TP_CALLBACK_ENVIRON env_;
  PTP_POOL pool_ = nullptr;
  PTP_CLEANUP_GROUP cleanup_ = nullptr;
  static void CALLBACK WorkCallback(PTP_CALLBACK_INSTANCE, PVOID Context,
                                    PTP_WORK) {
    threadpool_trait trait;
    trait.invoke_work(
        reinterpret_cast<typename threadpool_trait::work_type *>(Context));
  }

public:
  struct work_handle {
    PTP_WORK work = nullptr;
    bool done = false;
  };

  threadpool() {
    InitializeThreadpoolEnvironment(&env_);
    pool_ = CreateThreadpool(nullptr);
    if (!pool_) {
      DestroyThreadpoolEnvironment(&env_);
      throw MTLD3DError("Failed to create threadpool");
    }

    SetThreadpoolThreadMaximum(pool_, dxmt::thread::hardware_concurrency());

    cleanup_ = CreateThreadpoolCleanupGroup();
    if (!cleanup_) {
      CloseThreadpool(pool_);
      DestroyThreadpoolEnvironment(&env_);
      throw MTLD3DError("Failed to create threadpool cleanup group");
    }

    SetThreadpoolCallbackPool(&env_, pool_);
    SetThreadpoolCallbackCleanupGroup(&env_, cleanup_, nullptr);
  }

  ~threadpool() {
    CloseThreadpoolCleanupGroupMembers(cleanup_, true, nullptr);
    CloseThreadpoolCleanupGroup(cleanup_);
    CloseThreadpool(pool_);
    DestroyThreadpoolEnvironment(&env_);
  }

  threadpool(threadpool const &) = delete;
  threadpool(threadpool &&) = delete;
  threadpool &operator=(threadpool const &) = delete;
  threadpool &operator=(threadpool &&) = delete;

  HRESULT enqueue(typename threadpool_trait::work_type *Work,
                  work_handle *pHandle) {
    if (!Work || !pHandle)
      return E_POINTER;

    auto work = CreateThreadpoolWork(&WorkCallback, Work, &env_);
    if (!work)
      return HRESULT_FROM_WIN32(GetLastError());

    pHandle->work = work;
    pHandle->done = false;
    SubmitThreadpoolWork(work);
    return S_OK;
  }

  void wait(work_handle *handle) {
    if (handle->done)
      return;
    WaitForThreadpoolWorkCallbacks(handle->work, FALSE);
    CloseThreadpoolWork(handle->work);
    handle->done = true;
    handle->work = nullptr;
  }
};

#endif

} // namespace dxmt
