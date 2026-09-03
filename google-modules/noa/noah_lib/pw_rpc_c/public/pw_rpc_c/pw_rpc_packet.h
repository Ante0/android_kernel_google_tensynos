#ifndef PW_RPC_PACKET_H
#define PW_RPC_PACKET_H
#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#endif

#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode a RPC packet to bytes.
 *
 * @param[in] packet_type The packet type..
 * @param[in] call_id The call ID.
 * @param[in] channel_id The RPC channel ID.
 * @param[in] service_id The RPC service ID.
 * @param[in] method_id The RPC method ID.
 * @param[in] payload The encoded bytes for the method arguments.
 * @param[in] payload_len The length encoded bytes.
 * @param[in] status The status.
 * @param[out] encoded_bytes The encoded bytes of the packet, the caller shall
                             free this buffer.
 * @param[out] encoded_bytes_len The length of encoded bytes.
 * @return The encoding result
 * @retval kPwStatusOk Success.
 * @retval kPwStatusResourceExhausted Cannot allocate enough buffer.
 * @retval kPwStatusAborted Cannot encode a packet to bytes.
 */
PwStatus PwRpcPacketEncode(pw_rpc_internal_PacketType packet_type,
                           uint32_t call_id, uint32_t channel_id,
                           uint32_t service_id, uint32_t method_id,
                           const uint8_t* payload, size_t payload_len,
                           uint32_t status, uint8_t** encoded_bytes,
                           size_t* encoded_bytes_len);

/**
 * @brief Initialize the RPC client.
 *
 * @param[in] bytes The encoded bytes
 * @param[in] bytes_len The length of encoded bytes.
 * @param[in] payload_buf The buffer for storing the payload.
 * @param[in] payload_buf_len The length of the payload buffer.
 * @param[out] packet The pakcet holder.
 * @param[out] payload_len The length of the payload.
 * @return The decoding result
 * @retval kPwStatusOk Success.
 * @retval kPwStatusAborted Cannot decode bytes to a packet.
 */
PwStatus PwRpcPacketDecode(const uint8_t* bytes, size_t bytes_len,
                           uint8_t* payload_buf, size_t payload_buf_len,
                           pw_rpc_internal_RpcPacket* packet,
                           size_t* payload_len);

#ifdef __cplusplus
}
#endif
#endif /* PW_RPC_PACKET_H */
