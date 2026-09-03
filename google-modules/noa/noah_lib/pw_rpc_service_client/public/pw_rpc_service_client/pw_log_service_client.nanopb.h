#ifndef PW_LOG_SERVICE_CLIENT_NANOPB_H
#define PW_LOG_SERVICE_CLIENT_NANOPB_H

#include "pw_log/proto/log.pb.h"
#include "pw_rpc_c/pw_rpc_call.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_service_client.h"
#include "pw_rpc_c/pw_status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PW_LOG_SERVICE_ID (0x0fcd342bU)
#define PW_LOG_SERVICE_LISTEN_METHOD_ID (0xa7a01a2dU)

typedef void (*LogStringCallback)(void* context, const char* str);

/*
 * rpc Listen(LogRequest) returns (stream LogEntries);
 */
PW_RPC_SERVER_STREAMING_METHOD(PwLogService, PW_LOG_SERVICE_ID, Listen,
                               PW_LOG_SERVICE_LISTEN_METHOD_ID,
                               pw_log_LogRequest, pw_log_LogEntries);

/**
 * @brief Handle log entries.
 *
 * @param[in] bytes Raw bytes.
 * @param[in] len The length of data.
 * @param[in] callback The callback to print logs.
 * @param[in] context The context which is passed to the callback.
 * @return The handling status
 * @retval kPwStatusOk Success
 * @retval kPwStatusInvalidArgument Invalid bytes, len, or response.
 */
PwStatus PwLogServiceListenOnNext(const uint8_t* bytes, size_t len,
                                  LogStringCallback callback, void* context);
#ifdef __cplusplus
}
#endif
#endif /* PW_LOG_SERVICE_CLIENT_NANOPB_H */
