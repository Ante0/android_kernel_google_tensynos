#ifndef SHELL_CMD_SERVICE_CLIENT_NANOPB_H
#define SHELL_CMD_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "shell_cmd_service/shell_cmd_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHELL_CMD_SERVICE_ID (0x1daf98f6U)
#define SHELL_CMD_SERVICE_EXECUTE_METHOD_ID (0x5ead12d2U)

/*
 * rpc Execute(Request) returns (Response)
 */
PW_RPC_UNARY_METHOD(ShellCmdService, SHELL_CMD_SERVICE_ID, Execute,
                    SHELL_CMD_SERVICE_EXECUTE_METHOD_ID,
                    noa_service_shell_cmd_service_Request,
                    noa_service_shell_cmd_service_Response);

#ifdef __cplusplus
}
#endif
#endif /* SHELL_CMD_SERVICE_CLIENT_NANOPB_H */
