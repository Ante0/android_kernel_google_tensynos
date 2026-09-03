#include "pw_rpc_c/pw_rpc_packet.h"

#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_os.h"
#include "pw_rpc_c/pw_status.h"

typedef struct {
  const uint8_t* bytes;
  size_t len;
} PayloadEncodingInfo;

static bool EncodePayload(pb_ostream_t* stream, const pb_field_t* field,
			  void* const* arg) {
  PayloadEncodingInfo* payload_encoding_info = (PayloadEncodingInfo*)*arg;
  if (!pb_encode_tag_for_field(stream, field)) {
    return false;
  }
  // No payload, simply ignore it.
  if (!payload_encoding_info->bytes || !payload_encoding_info->len) {
    return true;
  }
  return pb_encode_string(stream, payload_encoding_info->bytes,
                          payload_encoding_info->len);
}

PwStatus PwRpcPacketEncode(pw_rpc_internal_PacketType packet_type,
                           uint32_t call_id, uint32_t channel_id,
                           uint32_t service_id, uint32_t method_id,
                           const uint8_t* payload, size_t payload_len,
                           uint32_t status, uint8_t** encoded_bytes,
                           size_t* encoded_bytes_len) {
  if (!encoded_bytes || !encoded_bytes_len) {
    return kPwStatusInvalidArgument;
  }
  PayloadEncodingInfo payload_encoding_info = {
      .bytes = payload,
      .len = payload_len,
  };
  pw_rpc_internal_RpcPacket packet = {
      .type = packet_type,
      .channel_id = channel_id,
      .service_id = service_id,
      .method_id = method_id,
      .payload =
          {
              .funcs =
                  {
                      .encode = EncodePayload,
                  },
              .arg = (void*)&payload_encoding_info,
          },
      .status = status,
      .call_id = call_id,
  };
  if (!payload || !payload_len) {
    packet.payload.funcs.encode = NULL;
    packet.payload.arg = NULL;
  }

  // Callback will write fields one by one, but the service side needs to read them all.
  if (!pb_get_encoded_size(encoded_bytes_len, pw_rpc_internal_RpcPacket_fields,
                           &packet)) {
    return kPwStatusAborted;
  }
  uint8_t* buffer = (uint8_t*)PwRpcAllocate(*encoded_bytes_len);
  if (!buffer) {
    return kPwStatusResourceExhausted;
  }

  pb_ostream_t stream = pb_ostream_from_buffer(buffer, *encoded_bytes_len);
  if (!pb_encode(&stream, pw_rpc_internal_RpcPacket_fields, &packet)) {
    PwRpcFree(buffer);
    return kPwStatusAborted;
  }
  *encoded_bytes = buffer;
  return kPwStatusOk;
}

typedef struct {
  uint8_t* buffer;
  size_t size;
  size_t len;
} PayloadDecodingInfo;

static bool ReadPayload(pb_istream_t* stream, const pb_field_t* field, void** arg) {
  (void)field;
  PayloadDecodingInfo* payload_decoding_info = (PayloadDecodingInfo*)*arg;
  if (payload_decoding_info->size < stream->bytes_left) {
    return false;
  }
  payload_decoding_info->len = stream->bytes_left;
  if (!pb_read(stream, payload_decoding_info->buffer, stream->bytes_left)) {
    return false;
  }
  return true;
}

PwStatus PwRpcPacketDecode(const uint8_t* bytes, size_t bytes_len,
                           uint8_t* payload_buf, size_t payload_buf_len,
                           pw_rpc_internal_RpcPacket* packet,
                           size_t* payload_len) {
  if (!bytes || !payload_buf || !packet || !payload_len) {
    return kPwStatusInvalidArgument;
  }
  PayloadDecodingInfo payload_decoding_info = {
      .buffer = payload_buf,
      .size = payload_buf_len,
      .len = 0,
  };
  memset(packet, 0, sizeof(pw_rpc_internal_RpcPacket));
  packet->payload.funcs.decode = ReadPayload;
  packet->payload.arg = (void*)&payload_decoding_info;

  pb_istream_t stream = pb_istream_from_buffer(bytes, bytes_len);
  if (!pb_decode(&stream, pw_rpc_internal_RpcPacket_fields, packet)) {
    return kPwStatusAborted;
  }
  *payload_len = payload_decoding_info.len;
  return kPwStatusOk;
}
