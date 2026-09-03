#include "pw_rpc_c/pw_rpc_packet.h"
#include "gtest/gtest.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_status.h"

namespace noa::module::linux_rpc_client::pw_rpc_c {
namespace {
constexpr pw_rpc_internal_PacketType kPacketType =
    pw_rpc_internal_PacketType_REQUEST;
constexpr uint32_t kTestChannelId = 9;
constexpr uint32_t kTestServiceId = 0xabcd1234;
constexpr uint32_t kTestMethodId = 0xdeadbeef;
constexpr uint32_t kTestCallId = 99;
constexpr uint8_t kPayload[] = {0x5a, 0x64, 0x33, 0x12};

TEST(PwRpcPacketTest, EncodeAndDecode) {
  uint8_t* encoded_bytes = nullptr;
  size_t encoded_bytes_size = 0;
  EXPECT_EQ(PwRpcPacketEncode(kPacketType, kTestCallId, kTestChannelId,
                              kTestServiceId, kTestMethodId, kPayload,
                              sizeof(kPayload), kPwStatusOk, &encoded_bytes,
                              &encoded_bytes_size),
            kPwStatusOk);
  ASSERT_NE(encoded_bytes, nullptr);

  pw_rpc_internal_RpcPacket decoded_packet;
  uint8_t payload_buf[32];
  size_t payload_len = 0;
  EXPECT_EQ(
      PwRpcPacketDecode(encoded_bytes, encoded_bytes_size, payload_buf,
                        sizeof(payload_buf), &decoded_packet, &payload_len),
      kPwStatusOk);
  EXPECT_EQ(decoded_packet.call_id, kTestCallId);
  EXPECT_EQ(decoded_packet.channel_id, kTestChannelId);
  EXPECT_EQ(decoded_packet.service_id, kTestServiceId);
  EXPECT_EQ(decoded_packet.method_id, kTestMethodId);
  EXPECT_EQ(payload_len, sizeof(kPayload));

  free(encoded_bytes);
}

TEST(PwRpcPacketTest, EncodeFailureInvalidArgument) {
  uint8_t* encoded_bytes = nullptr;
  size_t encoded_bytes_size = 0;
  EXPECT_EQ(PwRpcPacketEncode(kPacketType, kTestCallId, kTestChannelId,
                              kTestServiceId, kTestMethodId, kPayload,
                              sizeof(kPayload), kPwStatusOk, nullptr,
                              &encoded_bytes_size),
            kPwStatusInvalidArgument);
  EXPECT_EQ(
      PwRpcPacketEncode(kPacketType, kTestCallId, kTestChannelId,
                        kTestServiceId, kTestMethodId, kPayload,
                        sizeof(kPayload), kPwStatusOk, &encoded_bytes, nullptr),
      kPwStatusInvalidArgument);
}

TEST(PwRpcPacketTest, DecodeFailureInvalidArgument) {
  uint8_t* encoded_bytes = nullptr;
  size_t encoded_bytes_size = 0;
  EXPECT_EQ(PwRpcPacketEncode(kPacketType, kTestCallId, kTestChannelId,
                              kTestServiceId, kTestMethodId, kPayload,
                              sizeof(kPayload), kPwStatusOk, &encoded_bytes,
                              &encoded_bytes_size),
            kPwStatusOk);
  ASSERT_NE(encoded_bytes, nullptr);

  pw_rpc_internal_RpcPacket decoded_packet;
  uint8_t payload_buf[32];
  size_t payload_len = 0;
  EXPECT_EQ(
      PwRpcPacketDecode(nullptr, encoded_bytes_size, payload_buf,
                        sizeof(payload_buf), &decoded_packet, &payload_len),
      kPwStatusInvalidArgument);
  EXPECT_EQ(
      PwRpcPacketDecode(encoded_bytes, encoded_bytes_size, nullptr,
                        sizeof(payload_buf), &decoded_packet, &payload_len),
      kPwStatusInvalidArgument);
  EXPECT_EQ(PwRpcPacketDecode(encoded_bytes, encoded_bytes_size, payload_buf,
                              sizeof(payload_buf), nullptr, &payload_len),
            kPwStatusInvalidArgument);
  EXPECT_EQ(PwRpcPacketDecode(encoded_bytes, encoded_bytes_size, payload_buf,
                              sizeof(payload_buf), &decoded_packet, nullptr),
            kPwStatusInvalidArgument);

  free(encoded_bytes);
}

}  // namespace
}  // namespace noa::module::linux_rpc_client::pw_rpc_c
