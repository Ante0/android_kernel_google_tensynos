#include "pw_rpc_c/client_server_testing.h"

#include "pw_bytes/span.h"
#include "pw_rpc/packet_meta.h"
#include "pw_rpc/server.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_status/status.h"

namespace noa::module::linux_rpc_client::internal {

pw::Status ForwardPacket(pw::rpc::Server& server, PwRpcClient& client,
                         pw::ConstByteSpan packet, pw::ByteSpan decode_buffer) {
  if (pw::rpc::PacketMeta::FromBuffer(packet)->destination_is_server()) {
    return server.ProcessPacket(packet);
  } else {
    return static_cast<pw_Status>(PwRpcClientProcessPacket(
        &client,
        // Packet data is read-only in PwRpcClientProcessPacket.
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(packet.data())),
        packet.size(), reinterpret_cast<uint8_t*>(decode_buffer.data()),
        decode_buffer.size()));
  }
}

}  // namespace noa::module::linux_rpc_client::internal
