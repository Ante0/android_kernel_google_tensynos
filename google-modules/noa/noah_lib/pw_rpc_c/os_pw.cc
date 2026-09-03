#include "pw_rpc_c/pw_rpc_os.h"

#include <cstddef>
#include <cstdlib>

#include "pw_sync/lock_annotations.h"
#include "pw_sync/mutex.h"
#include "pw_sync/thread_notification.h"

extern "C" {
using PwMutex = pw::sync::Mutex;
using PwThreadNotification = pw::sync::ThreadNotification;

void PwRpcLockInit(PwRpcLock* lock) {
  *lock = new PwMutex();
}

int32_t PwRpcLockAcquire(PwRpcLock lock) PW_EXCLUSIVE_LOCK_FUNCTION(lock) {
  ((PwMutex*)lock)->lock();
  return 0;
}

void PwRpcLockRelease(PwRpcLock lock) PW_UNLOCK_FUNCTION(lock) {
  ((PwMutex*)lock)->unlock();
}

void PwRpcLockDeinit(PwRpcLock lock) {
  delete (PwMutex*)lock;
}

void* PwRpcAllocate(size_t size) {
  return malloc(size);
}

void PwRpcFree(void* ptr) {
  free(ptr);
}

}  // extern "C"
