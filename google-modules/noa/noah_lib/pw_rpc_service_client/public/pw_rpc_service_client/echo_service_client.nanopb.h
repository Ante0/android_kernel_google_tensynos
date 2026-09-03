#ifndef ECHO_SERVICE_CLIENT_NANOPB_H
#define ECHO_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc/echo.pb.h"
#include "pw_rpc_c/pw_rpc_service_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ECHO_SERVICE_ID (0x14fbd052U)
#define ECHO_SERVICE_ECHO_METHOD_ID (0x8b470ee9U)

/*
 * rpc rpc Echo(EchoMessage) returns (EchoMessage) {}
 */
PW_RPC_UNARY_METHOD(EchoService, ECHO_SERVICE_ID, Echo,
                    ECHO_SERVICE_ECHO_METHOD_ID, pw_rpc_EchoMessage,
                    pw_rpc_EchoMessage);

#ifdef __cplusplus
}
#endif
#endif /* ECHO_SERVICE_CLIENT_NANOPB_H */
