#include "noa_ipc/noa_ipc_os.h"

#include "arch/memory.h"
#include "mailbox/mailbox.h"
#include "mailbox/mailbox_clients.h"
#include "notifier/notifier.h"
#include "notifier/notifier_mailbox.h"
#include "pw_sync/thread_notification.h"
#include "pw_thread/yield.h"

extern "C" {
namespace mba_drv = noa::driver::mailbox;
using noa::module::notifier::kReceiver;
using noa::module::notifier::kSender;
using noa::module::notifier::NotifierMailbox;

// Memory
void NoaIpcFlushDCache(volatile void* addr, int32_t dsize) {
  FlushDCache(addr, dsize);
}

void NoaIpcInvalidateDCache(volatile void* addr, int32_t dsize) {
  InvalidateDCache(addr, dsize);
}

// Notifier
void NoaIpcNotifierInit(void** notifier_data, void* init_data, bool is_source,
                        bool is_sink, void*) {
  int32_t mba_id = *(int32_t*)init_data;
  NotifierMailbox* mba_notifier = new NotifierMailbox();
  if (mba_notifier) {
    uint32_t flag = 0;
    flag |= (is_source ? kSender : 0);
    flag |= (is_sink ? kReceiver : 0);
    mba_notifier->Init("ipc_channel", flag, (mba_drv::MailboxClientId)mba_id);
  }
  *notifier_data = mba_notifier;
}

void NoaIpcNotifierDeinit(void* notifier_data) {
  NotifierMailbox* mba_notifier = (NotifierMailbox*)notifier_data;
  delete mba_notifier;
}

void NoaIpcNotify(void* notifier_data) {
  noa::module::notifier::Notifier* notifier =
      (noa::module::notifier::Notifier*)notifier_data;
  notifier->Notify();
}

void NoaIpcRegisterNotificationHandler(void* notifier_data, OnNotified callback,
                                       void* context) {
  noa::module::notifier::Notifier* notifier =
      (noa::module::notifier::Notifier*)notifier_data;
  notifier->RegisterNotificationHandler(callback, context);
}

void NoaIpcSignalInit(void** signal) {
  *signal = new pw::sync::ThreadNotification();
}

int32_t NoaIpcWaitFor(void* signal) {
  pw::sync::ThreadNotification* notif = (pw::sync::ThreadNotification*)signal;
  notif->acquire();
  return 0;
}

void NoaIpcComplete(void* signal) {
  pw::sync::ThreadNotification* notif = (pw::sync::ThreadNotification*)signal;
  notif->release();
}

void NoaIpcSignalDeinit(void* signal) {
  delete (pw::sync::ThreadNotification*)signal;
}

// Thread
bool NoaIpcYield(void) {
  pw::this_thread::yield();
  return true;
}

// Memory Allocation
void* NoaIpcAllocate(size_t size) {
  return malloc(size);
}

void NoaIpcFree(void* ptr) {
  free(ptr);
}

void* OsIoRemap(uint32_t noa_addr, uint32_t /*len*/, void* /*context*/) {
  return reinterpret_cast<void*>(noa_addr);
}

void OsIoUnmap(void* /*os_addr*/, void* /*context*/) {
  // Do nothing.
}
}
