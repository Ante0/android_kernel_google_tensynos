#include "pw_rpc_c/pw_rpc_client.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/string.h>
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#endif

#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_call.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"

PwStatus PwRpcClientInit(PwRpcClient* client, uint32_t rpc_channel_id,
                         PwRpcChannelOutputHandler tx_handler,
                         PwRpcChannelInputSetupFunc rx_setup_func,
                         void* transport_info) {
  if (!client || !rpc_channel_id || !tx_handler || !rx_setup_func ||
      !transport_info) {
    return kPwStatusInvalidArgument;
  }

  memset(client, 0, sizeof(PwRpcClient));
  client->rpc_channel_id = rpc_channel_id;
  client->output_handler = tx_handler;
  client->input_setup_func = rx_setup_func;
  client->transport_info = transport_info;

  PwRpcLockInit(&client->lock);
  client->next_call_id = 0;

  PwRpcCallListInit(&client->call_list);

  PwStatus status = client->input_setup_func(client);
  if (status != kPwStatusOk) {
    PwRpcCallListDeinit(&client->call_list);
    PwRpcLockDeinit(client->lock);
    return status;
  }

  client->is_ready = true;

  return kPwStatusOk;
}

PwStatus PwRpcClientDeinit(PwRpcClient* client) {
  if (!client || !client->is_ready) {
    return kPwStatusInvalidArgument;
  }
  client->is_ready = false;

  // No channel ID indicates that this client is not initialized.
  if (client->rpc_channel_id) {
    for (PwRpcCall* call = PwRpcCallListPop(&client->call_list); call != NULL;
         call = PwRpcCallListPop(&client->call_list)) {
      PwRpcFree(call);
    }
    PwRpcCallListDeinit(&client->call_list);
    PwRpcLockDeinit(client->lock);
  }
  memset(client, 0, sizeof(PwRpcClient));
  return kPwStatusOk;
}

static uint32_t PwRpcClientGetNextCallId(PwRpcClient* client) {
  PwRpcLockAcquire(client->lock);
  uint32_t next_call_id = client->next_call_id++;
  PwRpcLockRelease(client->lock);
  return next_call_id;
}

static PwStatus PwRpcClientSendWithStatus(
    PwRpcClient* client, PwRpcCall* call,
    pw_rpc_internal_PacketType packet_type, PwStatus client_status,
    const uint8_t* bytes, size_t len) {
  // Serialize data to a RPC packet
  uint8_t* buffer = NULL;
  size_t encoded_bytes_size = 0;
  PwStatus status = PwRpcPacketEncode(
      packet_type, call->call_id, call->channel_id, call->service_id,
      call->method_id, bytes, len, client_status, &buffer, &encoded_bytes_size);
  if (status != kPwStatusOk) {
    PwRpcFree(buffer);
    return status;
  }

  if (client->output_handler(client, buffer, encoded_bytes_size) !=
      kPwStatusOk) {
    PwRpcFree(buffer);
    return kPwStatusUnavailable;
  }

  PwRpcFree(buffer);
  return kPwStatusOk;
}

static PwStatus PwRpcClientSend(PwRpcClient* client, PwRpcCall* call,
                                pw_rpc_internal_PacketType packet_type,
                                const uint8_t* bytes, size_t len) {
  return PwRpcClientSendWithStatus(client, call, packet_type, kPwStatusOk,
                                   bytes, len);
}

static PwStatus PwRpcClientInvokeCommon(PwRpcClient* client,
                                        uint32_t service_id, uint32_t method_id,
                                        const uint8_t* bytes, size_t len,
                                        PwRpcCallOnNextCallback on_next,
                                        PwRpcCallOnCompleteCallback on_complete,
                                        PwRpcCallOnErrorCallback on_error,
                                        void* call_context, uint32_t* call_id) {
  if (!client || !client->is_ready || !service_id || !method_id) {
    return kPwStatusInvalidArgument;
  }
  PwRpcCall* call = (PwRpcCall*)PwRpcAllocate(sizeof(PwRpcCall));
  if (!call) {
    return kPwStatusResourceExhausted;
  }
  call->client = client;
  call->type = kPwRpcClientCall;
  call->channel_id = client->rpc_channel_id;
  call->service_id = service_id;
  call->method_id = method_id;
  call->on_next = on_next;
  call->on_complete = on_complete;
  call->on_error = on_error;
  call->context = call_context;
  call->state = kPwRpcCallAwaitingResponse;
  call->call_id = PwRpcClientGetNextCallId(client);

  if (PwRpcCallListPut(&client->call_list, call) != kPwStatusOk) {
    PwRpcFree(call);
    return kPwStatusInternal;
  }

  PwStatus status = PwRpcClientSend(
      client, call, pw_rpc_internal_PacketType_REQUEST, bytes, len);
  if (status != kPwStatusOk) {
    PwRpcCallListRemove(&client->call_list, call->call_id);
    PwRpcFree(call);
    return status;
  }

  if (call_id) {
    *call_id = call->call_id;
  }

  return kPwStatusOk;
}

PwStatus PwRpcClientInvokeUnary(PwRpcClient* client, uint32_t service_id,
                                uint32_t method_id, const uint8_t* bytes,
                                size_t len,
                                PwRpcCallOnCompleteCallback on_complete,
                                PwRpcCallOnErrorCallback on_error,
                                void* call_context, uint32_t* call_id) {
  return PwRpcClientInvokeCommon(client, service_id, method_id, bytes, len,
                                 NULL, on_complete, on_error, call_context,
                                 call_id);
}

PwStatus PwRpcClientInvokeStreaming(PwRpcClient* client, uint32_t service_id,
                                    uint32_t method_id, const uint8_t* bytes,
                                    size_t len, PwRpcCallOnNextCallback on_next,
                                    PwRpcCallOnCompleteCallback on_complete,
                                    PwRpcCallOnErrorCallback on_error,
                                    void* call_context, uint32_t* call_id) {
  return PwRpcClientInvokeCommon(client, service_id, method_id, bytes, len,
                                 on_next, on_complete, on_error, call_context,
                                 call_id);
}

PwStatus PwRpcClientInvokeStreamingNext(PwRpcClient* client, uint32_t call_id,
                                        const uint8_t* bytes, size_t len) {
  if (!client || !client->is_ready || !bytes || !len) {
    return kPwStatusInvalidArgument;
  }

  PwRpcCall* call = PwRpcCallListGet(&client->call_list, call_id);
  if (!call) {
    return kPwStatusNotFound;
  }

  return PwRpcClientSend(client, call, pw_rpc_internal_PacketType_CLIENT_STREAM,
                         bytes, len);
}

PwStatus PwRpcClientInvokeStreamingComplete(PwRpcClient* client,
                                            uint32_t call_id) {
  if (!client || !client->is_ready) {
    return kPwStatusInvalidArgument;
  }

  PwRpcCall* call = PwRpcCallListGet(&client->call_list, call_id);
  if (!call) {
    return kPwStatusNotFound;
  }

  return PwRpcClientSend(client, call,
                         pw_rpc_internal_PacketType_CLIENT_REQUEST_COMPLETION,
                         NULL, 0);
}

PwStatus PwRpcClientInvokeError(PwRpcClient* client, PwStatus client_status,
                                uint32_t call_id) {
  if (!client || !client->is_ready) {
    return kPwStatusInvalidArgument;
  }

  PwRpcCall* call = PwRpcCallListGet(&client->call_list, call_id);
  if (!call) {
    return kPwStatusNotFound;
  }

  PwStatus status = PwRpcClientSendWithStatus(
      client, call, pw_rpc_internal_PacketType_CLIENT_ERROR, client_status,
      NULL, 0);

  // The server side won't respond to a client error, the client shall
  // drop the existing call proactively.
  if (status == kPwStatusOk) {
    PwRpcClientDropCall(client, call_id);
  }

  return status;
}

PwStatus PwRpcClientInvokeCancel(PwRpcClient* client, uint32_t call_id) {
  return PwRpcClientInvokeError(client, kPwStatusCancelled, call_id);
}

PwStatus PwRpcClientDropCall(PwRpcClient* client, uint32_t call_id) {
  if (!client || !client->is_ready) {
    return kPwStatusInvalidArgument;
  }

  PwRpcCall* call = PwRpcCallListGet(&client->call_list, call_id);
  if (!call) {
    return kPwStatusNotFound;
  }
  PwRpcCallListRemove(&client->call_list, call->call_id);
  PwRpcFree(call);
  return kPwStatusOk;
}

static PwStatus PwRpcClientInvokeCommonPb(
    PwRpcClient* client, uint32_t service_id, uint32_t method_id,
    const pb_msgdesc_t* msg_fields, const void* msg_struct,
    PwRpcCallOnNextCallback on_next, PwRpcCallOnCompleteCallback on_complete,
    PwRpcCallOnErrorCallback on_error, void* call_context, uint32_t* call_id) {
  if (!client || !client->is_ready || !service_id || !method_id ||
      !msg_fields || !msg_struct) {
    return kPwStatusInvalidArgument;
  }

  size_t encoded_bytes_size = 0;
  if (!pb_get_encoded_size(&encoded_bytes_size, msg_fields, msg_struct)) {
    return kPwStatusAborted;
  }

  uint8_t* buffer = NULL;

  if (encoded_bytes_size > 0) {
    buffer = (uint8_t*)PwRpcAllocate(encoded_bytes_size);
    if (!buffer) {
      return kPwStatusResourceExhausted;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encoded_bytes_size);
    if (!pb_encode(&stream, msg_fields, msg_struct)) {
      if (buffer) {
        PwRpcFree(buffer);
      }
      return kPwStatusAborted;
    }
  }

  PwStatus status = PwRpcClientInvokeCommon(
      client, service_id, method_id, buffer, encoded_bytes_size, on_next,
      on_complete, on_error, call_context, call_id);
  if (buffer) {
    PwRpcFree(buffer);
  }
  return status;
}

PwStatus PwRpcClientInvokeUnaryPb(PwRpcClient* client, uint32_t service_id,
                                  uint32_t method_id,
                                  const pb_msgdesc_t* msg_fields,
                                  const void* msg_struct,
                                  PwRpcCallOnCompleteCallback on_complete,
                                  PwRpcCallOnErrorCallback on_error,
                                  void* call_context, uint32_t* call_id) {
  return PwRpcClientInvokeCommonPb(client, service_id, method_id, msg_fields,
                                   msg_struct, NULL, on_complete, on_error,
                                   call_context, call_id);
}

PwStatus PwRpcClientInvokeStreamingPb(
    PwRpcClient* client, uint32_t service_id, uint32_t method_id,
    const pb_msgdesc_t* msg_fields, const void* msg_struct,
    PwRpcCallOnNextCallback on_next, PwRpcCallOnCompleteCallback on_complete,
    PwRpcCallOnErrorCallback on_error, void* call_context, uint32_t* call_id) {
  return PwRpcClientInvokeCommonPb(client, service_id, method_id, msg_fields,
                                   msg_struct, on_next, on_complete, on_error,
                                   call_context, call_id);
}

PwStatus PwRpcClientInvokeStreamingNextPb(PwRpcClient* client, uint32_t call_id,
                                          const pb_msgdesc_t* msg_fields,
                                          const void* msg_struct) {
  if (!client || !msg_fields || !msg_struct) {
    return kPwStatusInvalidArgument;
  }

  PwRpcCall* call = PwRpcCallListGet(&client->call_list, call_id);
  if (!call) {
    return kPwStatusNotFound;
  }

  size_t encoded_bytes_size = 0;
  if (!pb_get_encoded_size(&encoded_bytes_size, msg_fields, msg_struct)) {
    return kPwStatusAborted;
  }

  uint8_t* buffer = NULL;

  if (encoded_bytes_size > 0) {
    buffer = (uint8_t*)PwRpcAllocate(encoded_bytes_size);
    if (!buffer) {
      return kPwStatusResourceExhausted;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, encoded_bytes_size);
    if (!pb_encode(&stream, msg_fields, msg_struct)) {
      if (buffer) {
        PwRpcFree(buffer);
      }
      return kPwStatusAborted;
    }
  }

  PwStatus status =
      PwRpcClientSend(client, call, pw_rpc_internal_PacketType_CLIENT_STREAM,
                      buffer, encoded_bytes_size);
  if (buffer) {
    PwRpcFree(buffer);
  }
  return status;
}

PwStatus PwRpcClientProcessPacket(PwRpcClient* client, const uint8_t* bytes,
                                  size_t bytes_len, uint8_t* payload_buf,
                                  size_t payload_buf_len) {
  if (!client || !client->is_ready || !bytes || !bytes_len || !payload_buf ||
      !payload_buf_len) {
    return kPwStatusInvalidArgument;
  }

  // Deserialize bytes to a RPC packet.

  size_t payload_len = 0;
  pw_rpc_internal_RpcPacket rpc_packet;
  PwStatus status =
      PwRpcPacketDecode(bytes, bytes_len, payload_buf, payload_buf_len,
                        &rpc_packet, &payload_len);
  if (status != kPwStatusOk) {
    return status;
  }

  // Retrieve the call by the call ID.
  PwRpcCall* call = PwRpcCallListGet(&client->call_list, rpc_packet.call_id);
  if (!call) {
    return kPwStatusNotFound;
  }
  // Call the callback
  bool free_call = false;
  switch (rpc_packet.type) {
    case pw_rpc_internal_PacketType_RESPONSE:
      if (call->on_complete) {
        call->on_complete(call, payload_buf, payload_len,
                          (PwStatus)rpc_packet.status);
      }
      free_call = true;
      break;
    case pw_rpc_internal_PacketType_SERVER_ERROR:
      if (call->on_error) {
        call->on_error(call, (PwStatus)rpc_packet.status);
      }
      free_call = true;
      break;
    case pw_rpc_internal_PacketType_SERVER_STREAM:
      if (call->on_next) {
        call->on_next(call, payload_buf, payload_len);
      }
      break;
    default:
      break;
  }
  if (free_call) {
    PwRpcCallListRemove(&client->call_list, rpc_packet.call_id);
    PwRpcFree(call);
  }

  return kPwStatusOk;
}

PwStatus PwRpcClientDeserializeResponse(const uint8_t* bytes, size_t len,
                                        const pb_msgdesc_t* msg_fields,
                                        void* msg_struct) {
  if (!msg_fields || !msg_struct) {
    return kPwStatusInvalidArgument;
  }
  // empty field will be omitted.
  if (!bytes || !len) {
    return kPwStatusOk;
  }

  pb_istream_t stream = pb_istream_from_buffer(bytes, len);
  if (!pb_decode(&stream, msg_fields, msg_struct)) {
    return kPwStatusAborted;
  }
  return kPwStatusOk;
}
