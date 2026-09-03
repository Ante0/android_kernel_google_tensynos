#ifndef NEP_CMD_RPC_SERVICE_CLIENT_NANOPB_H
#define NEP_CMD_RPC_SERVICE_CLIENT_NANOPB_H

#include "nep_cmd_service.pb.h"
#include "pw_rpc_c/pw_rpc_service_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NEP_CMD_RPC_SERVICE_ID (0x866fa960U)
#define NEP_CMD_RPC_SERVICE_COMMAND_METHOD_ID (0xd3a9835cU)
#define NEP_CMD_RPC_SERVICE_LARGE_COMMAND_METHOD_ID (0x5fa5b8a6)
#define NEP_CMD_RPC_SERVICE_EVENT_METHOD_ID (0x4c648469U)

/*
 * rpc Command(Request) returns (Response) {}
 */

PW_RPC_UNARY_METHOD(NepCmdRpcService, NEP_CMD_RPC_SERVICE_ID, Command,
                    NEP_CMD_RPC_SERVICE_COMMAND_METHOD_ID,
                    noa_service_nep_cmd_service_Request,
                    noa_service_nep_cmd_service_Response);

/*
 * rpc LargeCommand(LargeRequest) returns (Response) {}
 */

PW_RPC_UNARY_METHOD(NepCmdRpcService, NEP_CMD_RPC_SERVICE_ID, LargeCommand,
                    NEP_CMD_RPC_SERVICE_LARGE_COMMAND_METHOD_ID,
                    noa_service_nep_cmd_service_LargeRequest,
                    noa_service_nep_cmd_service_Response);

/*
 * rpc Event(stream Response) returns (stream Request) {}
 */
PW_RPC_BIDIRECTIONAL_STREAMING_METHOD(NepCmdRpcService, NEP_CMD_RPC_SERVICE_ID,
                                      Event, NEP_CMD_RPC_SERVICE_EVENT_METHOD_ID,
                                      noa_service_nep_cmd_service_Response,
                                      noa_service_nep_cmd_service_Request);

#ifdef __cplusplus
}
#endif
#endif /* NEP_CMD_RPC_SERVICE_CLIENT_NANOPB_H */
