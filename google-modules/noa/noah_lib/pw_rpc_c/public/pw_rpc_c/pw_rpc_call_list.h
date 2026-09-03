#ifndef PW_RPC_CALL_LIST_H
#define PW_RPC_CALL_LIST_H
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <inttypes.h>
#include <stddef.h>
#endif

#include "pw_rpc_c/pw_rpc_call.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_status.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct PwRpcCallListNodeStruct {
  PwRpcCall* call;
  struct PwRpcCallListNodeStruct* next;
} PwRpcCallListNode;

typedef struct {
  PwRpcCallListNode* head;
  PwRpcCallListNode* tail;
  size_t size;
  PwRpcLock lock;
} PwRpcCallList;

/**
 * @brief Initialize the list.
 *
 * @param[in] list The call list context.
 * @return The initialization result.
 * @retval kPwStatusOk Success.
 * @retval kPwStatusInvalidArgument No valid call list context.
 */
PwStatus PwRpcCallListInit(PwRpcCallList* list);

/**
 * @brief Deinitialize the list.
 *
 * @param[in] list The call list context.
 * @return The deinitialization result.
 * @retval kPwStatusOk Success.
 * @retval kPwStatusInvalidArgument No valid call list context.
 */
PwStatus PwRpcCallListDeinit(PwRpcCallList* list);

/**
 * @brief Put a call object to the list.
 *
 * @param[in] list The call list context.
 * @param[in] call The call which is stored.
 * @return The storing result.
 * @retval kPwStatusOk Success.
 */
PwStatus PwRpcCallListPut(PwRpcCallList* list, PwRpcCall* call);

/**
 * @brief Get a call object from the list.
 *
 * @param[in] list The call list context.
 * @param[in] call_id The call ID of a call.
 * @return The call object.
 * @retval nullptr No call object with the call ID.
 */
PwRpcCall* PwRpcCallListGet(PwRpcCallList* list, uint32_t call_id);

/**
 * @brief Remove a call object from the list.
 *
 * @param[in] list The call list context.
 * @param[in] call_id The call ID of a call.
 * @return The removing result.
 * @retval kPwStatusOk Success.
 * @retval kPwStatusNotFound No call object is founded.
 */
PwStatus PwRpcCallListRemove(PwRpcCallList* list, uint32_t call_id);

/**
 * @brief Remove a call object and return it.
 *
 * @param[in] list The call list context.
 * @return The call object.
 * @retval nullptr No call object in the list.
 */
PwRpcCall* PwRpcCallListPop(PwRpcCallList* list);

/**
 * @brief Get the number of calls in the list.
 *
 * @param[in] list The call list context.
 * @return The size of this call list.
 */
size_t PwRpcCallListSize(PwRpcCallList* list);
#ifdef __cplusplus
}
#endif
#endif /* PW_RPC_CALL_LIST_H */
