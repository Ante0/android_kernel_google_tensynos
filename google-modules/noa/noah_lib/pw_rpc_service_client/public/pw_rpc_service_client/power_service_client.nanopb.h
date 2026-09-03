#ifndef POWER_SERVICE_CLIENT_NANOPB_H
#define POWER_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "power_service/power_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_SERVICE_ID (0x02a92570)
#define POWER_SERVICE_POWER_OFF_METHOD_ID (0xb036bf32)

/*
 * rpc PowerOff(Request) returns (Response)
 */
PW_RPC_UNARY_METHOD(PowerService, POWER_SERVICE_ID, PowerOff,
                    POWER_SERVICE_POWER_OFF_METHOD_ID,
                    noa_service_power_service_Request,
                    noa_service_power_service_Response);
#ifdef __cplusplus
}
#endif
#endif /* POWER_SERVICE_CLIENT_NANOPB_H */
