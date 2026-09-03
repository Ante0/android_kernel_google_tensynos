#ifndef PW_RPC_CLIENT_H
#define PW_RPC_CLIENT_H
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>

#include "pb.h"
#else
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>

#include "pb.h"
#endif

#include "pw_rpc_c/pw_rpc_call.h"
#include "pw_rpc_c/pw_rpc_call_list.h"
#include "pw_rpc_c/pw_status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PwRpcClientStruct;
//
// Platform-specific implementation
//
// Setup a worker to read data from IPC RX channel.
typedef PwStatus (*PwRpcChannelInputSetupFunc)(
    struct PwRpcClientStruct* client);
// Write data to IPC TX channel.
typedef PwStatus (*PwRpcChannelOutputHandler)(struct PwRpcClientStruct* client,
                                              const uint8_t* buf, size_t len);

typedef struct PwRpcClientStruct {
  uint32_t rpc_channel_id;
  PwRpcChannelOutputHandler output_handler;
  PwRpcChannelInputSetupFunc input_setup_func;
  void* transport_info;

  // Runtime data
  bool is_ready;
  PwRpcLock lock;
  uint32_t next_call_id;
  PwRpcCallList call_list;
} PwRpcClient;

///
/// @brief Initialize the RPC client.
///
/// @param[in] client The client context holder.
/// @param[in] rpc_channel_id The RPC channel ID.
/// @param[in] tx_handler The handler which sends data out.
/// @param[in] rx_setup_func The function which sets up the rx worker.
/// @param[in] transport_info The information for sending and receiving data.
/// @return The initialization result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument any parameter is NULL pointer.
///
PwStatus PwRpcClientInit(PwRpcClient* client, uint32_t rpc_channel_id,
                         PwRpcChannelOutputHandler tx_handler,
                         PwRpcChannelInputSetupFunc rx_setup_func,
                         void* transport_info);

///
/// @brief Deinitialize the RPC client.
///
/// @param[in] client The client context holder.
/// @return The deinitialization result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client is NULL.
///
PwStatus PwRpcClientDeinit(PwRpcClient* client);

///
/// @brief Invoke an unary RPC.
///
/// @param[in] client The client context holder.
/// @param[in] service_id The service ID.
/// @param[in] method_id The method ID.
/// @param[in] bytes The method request payload.
/// @param[in] len The payload length.
/// @param[in] on_complete The callback for RPC response.
/// @param[in] on_error The callback for RPC error.
/// @param[in] call_context The context for the call.
/// @param[out] call_id The call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeUnary(PwRpcClient* client, uint32_t service_id,
                                uint32_t method_id, const uint8_t* bytes,
                                size_t len,
                                PwRpcCallOnCompleteCallback on_complete,
                                PwRpcCallOnErrorCallback on_error,
                                void* call_context, uint32_t* call_id);

///
/// @brief Invoke an unary RPC.
///
/// @param[in] client The client context holder.
/// @param[in] service_id The service ID.
/// @param[in] method_id The method ID.
/// @param[in] msg_fields The message field definition.
/// @param[in] msg_struct The message data.
/// @param[in] on_complete The callback for RPC response.
/// @param[in] on_error The callback for RPC error.
/// @param[in] call_context The context for the call.
/// @param[out] call_id The call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeUnaryPb(PwRpcClient* client, uint32_t service_id,
                                  uint32_t method_id,
                                  const pb_msgdesc_t* msg_fields,
                                  const void* msg_struct,
                                  PwRpcCallOnCompleteCallback on_complete,
                                  PwRpcCallOnErrorCallback on_error,
                                  void* call_context, uint32_t* call_id);

///
/// @brief Invoke an streaming RPC.
///
/// @param[in] client The client context holder.
/// @param[in] service_id The service ID.
/// @param[in] method_id The method ID.
/// @param[in] bytes The method request payload.
/// @param[in] len The payload length.
/// @param[in] on_next The callback for RPC streaming response.
/// @param[in] on_complete The callback for RPC response.
/// @param[in] on_error The callback for RPC error.
/// @param[in] call_context The context for the call.
/// @param[out] call_id The call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, or method ID
///                                  are empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeStreaming(PwRpcClient* client, uint32_t service_id,
                                    uint32_t method_id, const uint8_t* bytes,
                                    size_t len, PwRpcCallOnNextCallback on_next,
                                    PwRpcCallOnCompleteCallback on_complete,
                                    PwRpcCallOnErrorCallback on_error,
                                    void* call_context, uint32_t* call_id);

///
/// @brief Invoke an streaming RPC with Probobuf struct.
///
/// @param[in] client The client context holder.
/// @param[in] service_id The service ID.
/// @param[in] method_id The method ID.
/// @param[in] msg_fields The message field definition.
/// @param[in] msg_struct The message data.
/// @param[in] on_next The callback for RPC streaming response.
/// @param[in] on_complete The callback for RPC response.
/// @param[in] on_error The callback for RPC error.
/// @param[in] call_context The context for the call.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, or method ID
///                                  are empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeStreamingPb(
    PwRpcClient* client, uint32_t service_id, uint32_t method_id,
    const pb_msgdesc_t* msg_fields, const void* msg_struct,
    PwRpcCallOnNextCallback on_next, PwRpcCallOnCompleteCallback on_complete,
    PwRpcCallOnErrorCallback on_error, void* call_context, uint32_t* call_id);

///
/// @brief Invoke next client streaming RPC.
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @param[in] bytes The method request payload.
/// @param[in] len The payload length.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusNotFound No call which is associated to the call id.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeStreamingNext(PwRpcClient* client, uint32_t call_id,
                                        const uint8_t* bytes, size_t len);

///
/// @brief Invoke next client streaming RPC with Probobuf struct.
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @param[in] msg_fields The message field definition.
/// @param[in] msg_struct The message data.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusNotFound No call which is associated to the call id.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeStreamingNextPb(PwRpcClient* client, uint32_t call_id,
                                          const pb_msgdesc_t* msg_fields,
                                          const void* msg_struct);

///
/// @brief Invoke client streaming completion.
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeStreamingComplete(PwRpcClient* client,
                                            uint32_t call_id);

///
/// @brief Invoke client error.
///
/// @param[in] client The client context holder.
/// @param[in] status The error status.
/// @param[in] call_id The streaming RPC call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeError(PwRpcClient* client, PwStatus status,
                                uint32_t call_id);

///
/// @brief Invoke client cancellation.
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @return The invoking result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client, bytes, service ID, method ID
///                                  is empty.
/// @retval kPwStatusResourceExhausted Cannot allocate enough space.
/// @retval kPwStatusAborted Protobuf encoding failure.
/// @retval kPwStatusUnavailable Cannot send data out.
/// @retval kPwStatusInternal Internal errors due to call object list operation.
///
PwStatus PwRpcClientInvokeCancel(PwRpcClient* client, uint32_t call_id);

///
/// @brief Drop a call from the client
///
/// @param[in] client The client context holder.
/// @param[in] call_id The streaming RPC call id.
/// @return The dropping result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client is invalid.
/// @retval kPwStatusNotFound No call which is associated with this call ID.
///
PwStatus PwRpcClientDropCall(PwRpcClient* client, uint32_t call_id);

///
/// @brief Drop calls for a service from the client
///
/// @param[in] client The client context holder.
/// @param[in] service_id The streaming RPC call id.
/// @return The dropping result
/// @retval kPwStatusOk Success.
/// @retval kPwStatusInvalidArgument The client is invalid.
/// @retval kPwStatusNotFound No call which is associated with this call ID.
///
PwStatus PwRpcClientDropServiceCall(PwRpcClient* client, uint32_t service_id);

///
/// @brief Process incoming RPC packet and dispatch to the callback.
///
/// @param[in] client The client context holder.
/// @param[in] bytes The packet raw bytes.
/// @param[in] bytes_len The data length.
/// @param[in] payload_buf The buffer for payload bytes.
/// @param[in] payload_buf_len The payload buffer length.
/// @return The processing result.
/// @retval kPwStatusInvalidArgument The client, or bytes are empty.
/// @retval kPwStatusOk Success.
/// @retval kPwStatusAborted Cannot decode the packet bytes.
/// @retval kPwStatusNotFound No pending call for this packet.
///
PwStatus PwRpcClientProcessPacket(PwRpcClient* client, const uint8_t* bytes,
                                  size_t bytes_len, uint8_t* payload_buf,
                                  size_t payload_buf_len);

///
/// @brief Deserailize the method response.
///
/// @param[in] bytes The packet raw bytes.
/// @param[in] len The data length.
/// @param[in] msg_fields The message field definition.
/// @param[out] msg_struct The message holder pointer.
/// @return The deserialization result.
/// @retval kPwStatusInvalidArgument The client, or bytes are empty.
/// @retval kPwStatusOk Success.
/// @retval kPwStatusAborted Cannot decode the packet bytes.
///
PwStatus PwRpcClientDeserializeResponse(const uint8_t* bytes, size_t len,
                                        const pb_msgdesc_t* msg_fields,
                                        void* msg_struct);
#ifdef __cplusplus
}
#endif
#endif /* PW_RPC_CLIENT_H */
