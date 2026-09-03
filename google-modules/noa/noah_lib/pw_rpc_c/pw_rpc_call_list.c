#include "pw_rpc_c/pw_rpc_call_list.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/string.h>
#include <linux/types.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

#include "pw_rpc_c/pw_rpc_os.h"

PwStatus PwRpcCallListInit(PwRpcCallList* list) {
  if (!list) {
    return kPwStatusInvalidArgument;
  }
  memset(list, 0, sizeof(PwRpcCallList));
  PwRpcLockInit(&list->lock);
  return kPwStatusOk;
}

PwStatus PwRpcCallListDeinit(PwRpcCallList* list) {
  PwRpcLockAcquire(list->lock);
  if (!list) {
    return kPwStatusInvalidArgument;
  }

  PwRpcCallListNode* cur = list->head;
  while (cur != NULL) {
    PwRpcCallListNode* removed = cur;
    cur = cur->next;
    PwRpcFree(removed);
  };
  PwRpcLockRelease(list->lock);

  PwRpcLockDeinit(list->lock);
  memset(list, 0, sizeof(PwRpcCallList));
  return kPwStatusOk;
}

PwStatus PwRpcCallListPut(PwRpcCallList* list, PwRpcCall* call) {
  PwRpcLockAcquire(list->lock);
  if (!list) {
    PwRpcLockRelease(list->lock);
    return kPwStatusInvalidArgument;
  }

  PwRpcCallListNode* new_node =
      (PwRpcCallListNode*)PwRpcAllocate(sizeof(PwRpcCallListNode));
  new_node->call = call;

  // Older calls might be handled eariler. Put new call to the end.
  if (!list->head) {
    list->head = new_node;
    list->tail = new_node;
  } else if (list->tail) {
    list->tail->next = new_node;
  } else {
    PwRpcLockRelease(list->lock);
    PwRpcFree(new_node);
    return kPwStatusInternal;
  }
  list->tail = new_node;
  new_node->next = NULL;
  list->size++;
  PwRpcLockRelease(list->lock);
  return kPwStatusOk;
}

PwRpcCall* PwRpcCallListGet(PwRpcCallList* list, uint32_t call_id) {
  PwRpcLockAcquire(list->lock);
  if (!list || !list->head) {
    PwRpcLockRelease(list->lock);
    return NULL;
  }

  PwRpcCallListNode* iter = list->head;
  while (iter != NULL) {
    if (iter->call->call_id == call_id) {
      PwRpcLockRelease(list->lock);
      return iter->call;
    }
    iter = iter->next;
  }
  PwRpcLockRelease(list->lock);
  return NULL;
}

PwStatus PwRpcCallListRemove(PwRpcCallList* list, uint32_t call_id) {
  PwRpcLockAcquire(list->lock);
  if (!list) {
    PwRpcLockRelease(list->lock);
    return kPwStatusInvalidArgument;
  }
  if (!list->head) {
    PwRpcLockRelease(list->lock);
    return kPwStatusNotFound;
  }
  PwRpcCallListNode* prev = NULL;
  PwRpcCallListNode* cur = list->head;
  while (cur != NULL) {
    if (cur->call->call_id == call_id) {
      if (cur == list->head) {
        list->head = cur->next;
      }
      if (cur == list->tail) {
        list->tail = prev;
      }
      if (prev != NULL) {
        prev->next = cur->next;
      }
      list->size--;
      PwRpcFree(cur);
      PwRpcLockRelease(list->lock);
      return kPwStatusOk;
    }
    prev = cur;
    cur = cur->next;
  }
  PwRpcLockRelease(list->lock);
  return kPwStatusNotFound;
}

PwRpcCall* PwRpcCallListPop(PwRpcCallList* list) {
  PwRpcLockAcquire(list->lock);
  if (!list->head) {
    PwRpcLockRelease(list->lock);
    return NULL;
  }
  PwRpcCall* call = list->head->call;
  PwRpcCallListNode* removed = list->head;
  list->head = list->head->next;
  PwRpcFree(removed);
  PwRpcLockRelease(list->lock);
  return call;
}

size_t PwRpcCallListSize(PwRpcCallList* list) {
  PwRpcLockAcquire(list->lock);
  if (!list) {
    PwRpcLockRelease(list->lock);
    return 0;
  }
  PwRpcLockRelease(list->lock);
  return list->size;
}
