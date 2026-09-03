#include <cstring>

#include "pw_rpc_c/pw_rpc_call.h"

namespace noa::module::linux_rpc_client::pw_rpc_c {

PwRpcCall* head = nullptr;
PwRpcCall* tail = nullptr;

PwStatus PwRpcCallListPut(PwRpcCall* call) {
  if (head == nullptr) {
    head = call;
  } else {
    tail->next = call;
  }
  tail = call;
  call->next = nullptr;
  return kPwStatusOk;
}

PwRpcCall* PwRpcCallListGet(uint32_t call_id) {
  if (head == nullptr) {
    return nullptr;
  }

  PwRpcCall* iter = head;
  while (iter != nullptr) {
    if (iter->call_id == call_id) {
      return iter;
    }
    iter = iter->next;
  }
  return nullptr;
}

PwStatus PwRpcCallListRemove(uint32_t call_id) {
  if (head == nullptr) {
    return kPwStatusNotFound;
  }
  PwRpcCall* prev = nullptr;
  PwRpcCall* cur = head;
  while (cur != nullptr) {
    if (cur->call_id == call_id) {
      if (cur == head) {
        head = cur->next;
      }
      if (cur == tail) {
        tail = prev;
      }
      if (prev != nullptr) {
        prev->next = cur->next;
      }
      return kPwStatusOk;
    }
    prev = cur;
    cur = cur->next;
  }
  return kPwStatusNotFound;
}
}  // namespace noa::module::linux_rpc_client::pw_rpc_c
