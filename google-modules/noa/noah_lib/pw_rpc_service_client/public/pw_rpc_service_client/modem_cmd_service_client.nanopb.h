#ifndef MODEM_CMD_SERVICE_CLIENT_NANOPB_H
#define MODEM_CMD_SERVICE_CLIENT_NANOPB_H

#include "modem_cmd_service.pb.h"
#include "pw_rpc_c/pw_rpc_service_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODEM_CMD_SERVICE_ID (0xf623bf02U)
#define MODEM_CMD_SERVICE_INIT_COMMAND_METHOD_ID (0x73e11250U)
#define MODEM_CMD_SERVICE_COMMAND_METHOD_ID (0x1223e11cU)
#define MODEM_CMD_SERVICE_EVENT_METHOD_ID (0xd13d74cbU)
#define MODEM_CMD_SERVICE_MSG_MAX_SIZE (512U)
#define UNUSED(x) UNUSED_##x __attribute__((__unused__))

/*
 * rpc InitCommand(InitRequest) returns (Response) {}
 */
PW_RPC_UNARY_METHOD(ModemCmdService, MODEM_CMD_SERVICE_ID, InitCommand,
                    MODEM_CMD_SERVICE_INIT_COMMAND_METHOD_ID,
                    noa_service_modem_cmd_service_InitRequest,
                    noa_service_modem_cmd_service_Response);

/*
 * rpc Command(Request) returns (Response) {}
 */
PW_RPC_UNARY_METHOD(ModemCmdService, MODEM_CMD_SERVICE_ID, Command,
                    MODEM_CMD_SERVICE_COMMAND_METHOD_ID,
                    noa_service_modem_cmd_service_Request,
                    noa_service_modem_cmd_service_Response);

/*
 * rpc Event(stream Response) returns (stream Request) {}
 */
PW_RPC_BIDIRECTIONAL_STREAMING_METHOD(ModemCmdService, MODEM_CMD_SERVICE_ID,
                                      Event, MODEM_CMD_SERVICE_EVENT_METHOD_ID,
                                      noa_service_modem_cmd_service_Response,
                                      noa_service_modem_cmd_service_Request);

#ifdef __cplusplus
}
#endif
#endif /* MODEM_CMD_SERVICE_CLIENT_NANOPB_H */
