#ifndef WLAN_RPC_SERVICE_CLIENT_NANOPB_H
#define WLAN_RPC_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "wlan_rpc_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WLAN_RPC_SERVICE_ID (0x9eb971b0U)
#define WLAN_RPC_SERVICE_COMMAND_METHOD_ID (0x1223e11cU)
#define WLAN_RPC_SERVICE_EVENT_METHOD_ID (0xd13d74cbU)
#define WLAN_RPC_SERVICE_MSG_MAX_SIZE (512U)
#define UNUSED(x) UNUSED_##x __attribute__((__unused__))

PW_RPC_UNARY_METHOD(WlanRpcService, WLAN_RPC_SERVICE_ID, Command,
                    WLAN_RPC_SERVICE_COMMAND_METHOD_ID,
                    noa_service_wlan_rpc_service_Request,
                    noa_service_wlan_rpc_service_Response);

/*
 * rpc Event(stream Response) returns (stream Request) {}
 */
PW_RPC_BIDIRECTIONAL_STREAMING_METHOD(WlanRpcService, WLAN_RPC_SERVICE_ID,
                                      Event, WLAN_RPC_SERVICE_EVENT_METHOD_ID,
                                      noa_service_wlan_rpc_service_Response,
                                      noa_service_wlan_rpc_service_Request);

#ifdef __cplusplus
}
#endif
#endif /* WLAN_RPC_SERVICE_CLIENT_NANOPB_H */
