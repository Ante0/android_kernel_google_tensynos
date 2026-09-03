#pragma once
#include <array>
#include <cstddef>
#include <cstring>

#include "pw_assert/assert.h"
#include "pw_bytes/span.h"
#include "pw_containers/vector.h"
#include "pw_result/result.h"
#include "pw_rpc/nanopb/client_testing.h"
#include "pw_rpc/server.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_span/span.h"
#include "pw_status/status.h"

namespace noa::module::linux_rpc_client {
namespace internal {

inline constexpr size_t kDefaultMaxPackets = 16;
inline constexpr size_t kDefaultPacketEncodeDecodeBufferSizeBytes = 256;
inline constexpr size_t kDefaultPacketBufferSizeBytes = 256;

pw::Status ForwardPacket(pw::rpc::Server& server, PwRpcClient& client,
                         pw::ConstByteSpan packet, pw::ByteSpan decode_buffer);

}  // namespace internal

/// @brief A helper class for testing client-server communication.
///
/// This class provides a pre-configured environment with instances of:
///  * pw::Server (for simulating the server-side)
///  * PwRpcClient (for simulating the client-side)
///
/// The channel underlying the client-server interaction is built upon the
/// pw::rpc::NanopbClientTestContext.
///
/// Within this test context, all packets transmitted between the server
/// and client are temporarily buffered. The `ForwardNewPackets` method can be
/// invoked to explicitly forward these buffered packets.
///
/// Example:
/// @code{.cpp}
///   PwRpcClientServerTestContext context;
///
///   // Register service to the pw::rpc::Server instance.
///   SomeUnaryServiceImplementation service;
///   context.Server().RegisterService(service);
///
///   // Initiate request from the PwRpcClient instance.
///   EstablishCallAndRequest(context.Client(), ResponseCallback);
///
///   // Forward buffered packets.
///   context.ForwardNewPackets();
///
///   PW_TRY(context.Status());
///   // ... Check response
///
/// @endcode
///
/// @tparam kMaxPackets The upper limit on the number of packets that can be
/// held in the buffer within the context.
///
/// @tparam kPacketEncodeDecodeBufferSizeBytes The size (in bytes) of the buffer
/// used for packet encoding and decoding.
///
/// @tparam kPacketBufferSizeBytes The size (in bytes) of the buffer designated
/// for storing packets.
template <size_t kMaxPackets = internal::kDefaultMaxPackets,
          size_t kPacketEncodeDecodeBufferSizeBytes =
              internal::kDefaultPacketEncodeDecodeBufferSizeBytes,
          size_t kPacketBufferSizeBytes =
              internal::kDefaultPacketBufferSizeBytes>
class PwRpcClientServerTestContext {
 public:
  PwRpcClientServerTestContext()
      : client_test_context_(),
        server_(pw::span(&client_test_context_.channel(), 1)) {

    // Initializes the client instance with the channel from
    // client_test_context_.
    // The ClientOutputHandler function is used to process outgoing packets.
    // The input setup callback is set to NoopInputSetup as it's not required.
    PwRpcClientInit(&client_, client_test_context_.channel().id(),
                    ClientOutputHandler, NoopInputSetup, this);

    // Configures a callback to be invoked whenever a packet is sent over the
    // channel output in client_test_context_, to store the transmitted packets
    // in the buffer.
    client_test_context_.output().set_on_send([this](pw::ConstByteSpan packet,
                                                     pw::Status status) {
      status_.Update(status);

      size_t buffer_position = packet_buffer_.size();

      PW_ASSERT(buffer_position + packet.size() <= packet_buffer_.max_size());
      packet_buffer_.resize(buffer_position + packet.size());
      std::memcpy(&packet_buffer_[buffer_position], packet.data(),
                  packet.size());

      PW_ASSERT(packets_.size() < packets_.max_size());
      packets_.push_back(
          pw::span(&packet_buffer_[buffer_position], packet.size()));
    });
  }

  ~PwRpcClientServerTestContext() { PwRpcClientDeinit(&client_); }

  PwRpcClientServerTestContext(const PwRpcClientServerTestContext&) = delete;
  PwRpcClientServerTestContext& operator=(const PwRpcClientServerTestContext&) =
      delete;
  PwRpcClientServerTestContext(PwRpcClientServerTestContext&&) = delete;
  PwRpcClientServerTestContext& operator=(PwRpcClientServerTestContext&&) =
      delete;

  /// @brief Provides access to the underlying PwRpcClient instance.
  ///
  /// @return A reference to the PwRpcClient instance.
  PwRpcClient& Client() PW_ATTRIBUTE_LIFETIME_BOUND { return client_; }

  /// @brief Provides access to the NanopbClientTestContext instance.
  ///
  /// This context could be used to check for specific packets received or sent
  /// during the test, aiding in verification of RPC interactions.
  ///
  /// @return A reference to the NanopbClientTestContext instance.
  pw::rpc::NanopbClientTestContext<
      kMaxPackets, kPacketEncodeDecodeBufferSizeBytes, kPacketBufferSizeBytes>&
  ClientTestContext() PW_ATTRIBUTE_LIFETIME_BOUND {
    return client_test_context_;
  }

  /// @brief Forwards buffered packets to their intended destinations.
  ///
  /// In this test context all packets sent by the server and client are
  /// buffered. This function is responsible for forwarding all packets
  /// currently held in the buffer to their respective destination.
  ///
  /// After forwarding all packets, buffer is cleared.
  void ForwardNewPackets() {
    for (size_t i = 0; i < packets_.size(); ++i) {
      internal::ForwardPacket(server_, client_, packets_[i],
                              client_decode_buffer_);
    }
    packets_.clear();
    packet_buffer_.clear();
  }

  /// @brief Provides access to the pw::rpc::Server instance.
  ///
  /// @return A reference to the pw::rpc::Server instance.
  pw::rpc::Server& Server() PW_ATTRIBUTE_LIFETIME_BOUND { return server_; }

  /// @brief Returns the current status of the test context.
  ///
  /// @return The status indicating success or failure of recent operations.
  pw::Status Status() { return status_; }

 private:
  // Output handler to forward packets to the output channel.
  static PwStatus ClientOutputHandler(PwRpcClient* client, const uint8_t* buf,
                                      size_t len) {
    pw::ConstByteSpan packet = pw::as_bytes(pw::span(buf, len));
    pw::Status status =
        static_cast<PwRpcClientServerTestContext*>(client->transport_info)
            ->SendPacketToChannelOutput(packet);
    return static_cast<PwStatus>(status.code());
  }

  static PwStatus NoopInputSetup(PwRpcClient*) { return kPwStatusOk; }

  pw::Status SendPacketToChannelOutput(pw::ConstByteSpan packet) {
    pw::Status send_status = client_test_context_.output().Send(packet);
    status_.Update(send_status);
    return send_status;
  }

  pw::rpc::NanopbClientTestContext<
      kMaxPackets, kPacketEncodeDecodeBufferSizeBytes, kPacketBufferSizeBytes>
      client_test_context_;
  pw::rpc::Server server_;
  PwRpcClient client_;
  std::array<std::byte, kPacketEncodeDecodeBufferSizeBytes>
      client_decode_buffer_;
  pw::Vector<pw::ConstByteSpan, kMaxPackets> packets_;
  pw::Vector<std::byte, kPacketBufferSizeBytes> packet_buffer_;
  pw::Status status_;
};

}  // namespace noa::module::linux_rpc_client
