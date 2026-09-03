#ifndef IPSEC_TESTER_SERVICE_CLIENT_NANOPB_H
#define IPSEC_TESTER_SERVICE_CLIENT_NANOPB_H

#include "pw_rpc_c/pw_rpc_service_client.h"
#include "ipsec_test_service/ipsec_test_service.pb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SEND_DATA_SERVICE_ID (0x9358ee0c)
#define SEND_DATA_EXECUTE_METHOD_ID (0xa325bd5a)

/*
 * rpc SendData(Data) returns (GenericResponse)
 */
PW_RPC_UNARY_METHOD(IpsecTestService, SEND_DATA_SERVICE_ID, SendData,
                    SEND_DATA_EXECUTE_METHOD_ID,
                    noa_service_ipsec_test_service_SendDataCmd,
                    noa_service_ipsec_test_service_EmptyMessage);

#define RECEIVE_DATA_SERVICE_ID (0x9358ee0c)
#define RECEIVE_DATA_EXECUTE_METHOD_ID (0xdbd124fe)

/*
 * rpc ReceiveData(EmptyMessage) returns (GenericResponse)
 */
PW_RPC_UNARY_METHOD(IpsecTestService, RECEIVE_DATA_SERVICE_ID, ReceiveData,
                    RECEIVE_DATA_EXECUTE_METHOD_ID,
                    noa_service_ipsec_test_service_EmptyMessage,
                    noa_service_ipsec_test_service_ReceiveDataRsp);

#ifdef __cplusplus
}
#endif
#endif /* IPSEC_TESTER_SERVICE_CLIENT_NANOPB_H */
