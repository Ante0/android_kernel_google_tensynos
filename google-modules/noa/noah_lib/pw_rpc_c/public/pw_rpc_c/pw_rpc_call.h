#ifndef PW_RPC_CALL_H
#define PW_RPC_CALL_H
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <inttypes.h>
#include <stddef.h>
#endif

#include "pw_rpc_c/pw_status.h"

#ifdef __cplusplus
extern "C" {
#endif
struct PwRpcClientStruct;

// See pw_rpc/internal/call.h for reference
typedef enum {
  kPwRpcServerCall,
  kPwRpcClientCall,
} PwRpcCallType;

typedef enum {
  kPwRpcCallAwaitingResponse,
  kPwRpcCallComplete,
  kPwRpcCallStreamClosedAwaitingResponse,
} PwRpcCallState;

struct PwRpcCallStruct;
typedef void (*PwRpcCallOnNextCallback)(struct PwRpcCallStruct* call,
                                        const uint8_t* payload,
                                        size_t payload_size);
typedef void (*PwRpcCallOnCompleteCallback)(struct PwRpcCallStruct* call,
                                            const uint8_t* payload,
                                            size_t payload_size,
                                            PwStatus status);
typedef void (*PwRpcCallOnErrorCallback)(struct PwRpcCallStruct* call,
                                         PwStatus error);

typedef struct PwRpcCallStruct {
  struct PwRpcClientStruct* client;
  PwRpcCallType type;
  uint32_t call_id;
  uint32_t channel_id;
  uint32_t service_id;
  uint32_t method_id;

  PwRpcCallOnNextCallback on_next;
  PwRpcCallOnCompleteCallback on_complete;
  PwRpcCallOnErrorCallback on_error;

  void* context;

  PwRpcCallState state;
} PwRpcCall;
#ifdef __cplusplus
}
#endif
#endif
