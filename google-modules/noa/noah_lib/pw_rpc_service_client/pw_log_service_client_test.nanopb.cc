#include "gtest/gtest.h"
#include "mock_transport.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/pw_log_service_client.nanopb.h"

namespace pw::log {
namespace {

constexpr uint32_t kRpcChannelIdTest = 9;
struct TestTransport test_transport_info;

TEST(PwLogServiceClientTestNanopb, InvokeListenSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);

  pw_log_LogRequest request = {
      .dummy_field = '\0',
  };
  EXPECT_EQ(PwLogServiceListen(&client, &request, nullptr, nullptr, nullptr,
                               nullptr, nullptr),
            kPwStatusOk);
  EXPECT_GT(test_transport_info.tx_bytes, 0U);

  uint8_t decode_buf[16];
  pw_rpc_internal_RpcPacket packet;
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, decode_buf,
                              sizeof(decode_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, PW_LOG_SERVICE_ID);
  EXPECT_EQ(packet.method_id, PW_LOG_SERVICE_LISTEN_METHOD_ID);
  EXPECT_EQ(payload_len, 0U);

  PwRpcClientDeinit(&client);
}

TEST(PwLogServiceClientTestNanopb, InvokeListenInvalidArgumentNullClient) {
  EXPECT_EQ(PwLogServiceListen(nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr),
            kPwStatusInvalidArgument);
}
}  // namespace
}  // namespace pw::log
