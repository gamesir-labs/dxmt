#if defined(__MINGW32__)

#include <pthread.h>

extern "C" int pthread_cond_timedwait64(pthread_cond_t *cv,
                                         pthread_mutex_t *external_mutex,
                                         const struct timespec *t) {
  return pthread_cond_timedwait(cv, external_mutex, t);
}

#endif
