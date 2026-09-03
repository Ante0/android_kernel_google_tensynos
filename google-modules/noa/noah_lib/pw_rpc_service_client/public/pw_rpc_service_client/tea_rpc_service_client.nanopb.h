#ifndef TEA_RPC_SERVICE_CLIENT_NANOPB_H
#define TEA_RPC_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "tea_rpc_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEA_RPC_SERVICE_ID (0x53dbc2b2U)
#define TEA_RPC_SERVICE_PUSHEVENTS_METHOD_ID (0xe742c79dU)

typedef void (*SendEventCallback)(const int64_t time_ms, const uint8_t* bytes, const size_t len);

/*
 * rpc PushEvents(PushEventsRequest) returns (stream PushEventsResponse);
 */
PW_RPC_SERVER_STREAMING_METHOD(TeaRpcService, TEA_RPC_SERVICE_ID, PushEvents,
                               TEA_RPC_SERVICE_PUSHEVENTS_METHOD_ID,
                               noa_service_tea_rpc_service_PushEventsRequest,
                               noa_service_tea_rpc_service_PushEventsResponse);

#ifdef __cplusplus
}
#endif
#endif /* TEA_RPC_SERVICE_CLIENT_NANOPB_H */
