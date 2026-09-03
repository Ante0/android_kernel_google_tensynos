#ifndef PCIE_RPC_SERVICE_CLIENT_NANOPB_H
#define PCIE_RPC_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "pcie_service/pcie_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCIE_CMD_SERVICE_ID (0x3577fb5bU)
#define PCIE_CMD_SERVICE_REGIONCONFIGCOMMAND_METHOD_ID (0xd0c229deU)
#define PCIE_CMD_SERVICE_MSG_MAX_SIZE (512U)
#define UNUSED(x) UNUSED_##x __attribute__((__unused__))

PW_RPC_UNARY_METHOD(PCIeCmdService, PCIE_CMD_SERVICE_ID, RegionConfigCommand,
                    PCIE_CMD_SERVICE_REGIONCONFIGCOMMAND_METHOD_ID,
                    noa_service_pcie_service_RegionConfigRequest,
                    noa_service_pcie_service_RegionConfigResponse);

#ifdef __cplusplus
}
#endif
#endif /* PCIE_RPC_SERVICE_CLIENT_NANOPB_H */

