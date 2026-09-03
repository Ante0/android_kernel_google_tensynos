#include <cstring>

#include "gtest/gtest.h"
#include "mock_transport.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc/echo.pb.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/echo_service_client.nanopb.h"

namespace pw::rpc {
namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;
constexpr char kTestEchoMsg[] = "test ncp-echo";

TEST(EchoServiceClientTestNanopb, InvokeEchoSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);
  pw_rpc_EchoMessage request;
  memcpy(request.msg, kTestEchoMsg, sizeof(kTestEchoMsg));
  EXPECT_EQ(
      EchoServiceEcho(&client, &request, nullptr, nullptr, nullptr, nullptr),
      kPwStatusOk);
  ASSERT_GT(test_transport_info.tx_bytes, 0U);

  uint8_t payload_buf[16];
  pw_rpc_internal_RpcPacket packet;
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, ECHO_SERVICE_ID);
  EXPECT_EQ(packet.method_id, ECHO_SERVICE_ECHO_METHOD_ID);

  pw_rpc_EchoMessage written_request;
  EXPECT_EQ(PwRpcClientDeserializeResponse(payload_buf, payload_len,
                                           pw_rpc_EchoMessage_fields,
                                           &written_request),
            kPwStatusOk);
  EXPECT_EQ(strlen(written_request.msg), strlen(kTestEchoMsg));
  EXPECT_STREQ(written_request.msg, kTestEchoMsg);

  PwRpcClientDeinit(&client);
}

TEST(EchoServiceClientTestNanopb, InvokeEchoInvalidArgumentNullClient) {
  pw_rpc_EchoMessage request;
  memcpy(request.msg, kTestEchoMsg, sizeof(kTestEchoMsg));
  EXPECT_EQ(
      EchoServiceEcho(nullptr, &request, nullptr, nullptr, nullptr, nullptr),
      kPwStatusInvalidArgument);
}

TEST(EchoServiceClientTestNanopb, InvokeEchoInvalidArgumentNullMsg) {
  PwRpcClient client;
  EXPECT_EQ(
      EchoServiceEcho(&client, nullptr, nullptr, nullptr, nullptr, nullptr),
      kPwStatusInvalidArgument);
}

constexpr char kTestMsg[] = "Hello NCP";

TEST(EchoServiceClientTestNanopb, DeserializeEchoResponseSuccess) {
  uint8_t buf[32] = {0};

  pw_rpc_EchoMessage response;
  memset(&response, 0, sizeof(pw_rpc_EchoMessage));
  memcpy(response.msg, kTestMsg, sizeof(kTestMsg));

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, pw_rpc_EchoMessage_fields, &response));

  memset(&response, 0, sizeof(pw_rpc_EchoMessage));
  EXPECT_EQ(
      EchoServiceEchoDeserializeResponse(buf, stream.bytes_written, &response),
      kPwStatusOk);
  EXPECT_EQ(strlen(response.msg), strlen(kTestMsg));
  EXPECT_STREQ(response.msg, kTestMsg);
}
}  // namespace
}  // namespace pw::rpc
