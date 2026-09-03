#include <cstring>

#include "gtest/gtest.h"
#include "mock_transport.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/wlan_rpc_service_client.nanopb.h"
#include "wlan_rpc_service.pb.h"

namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;
constexpr uint32_t kTestId = 45;
constexpr char kTestCmd[] = {"TestCommand"};
constexpr noa_service_wlan_rpc_service_CmdResult kTestResult =
    noa_service_wlan_rpc_service_CmdResult_FAILURE;
constexpr char kTestMsg[] = {"Failure"};

TEST(WlanRpcServiceClientTestNanopb, InvokeCommandSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);
  noa_service_wlan_rpc_service_Request request;
  request.id = kTestId;
  memcpy(request.msg.bytes, reinterpret_cast<const uint8_t*>(kTestCmd),
         sizeof(kTestCmd));
  request.msg.size = sizeof(kTestCmd);

  EXPECT_EQ(WlanRpcServiceCommand(&client, &request, nullptr, nullptr, nullptr,
                                  nullptr),
            kPwStatusOk);
  ASSERT_GT(test_transport_info.tx_bytes, 0U);

  uint8_t payload_buf[32];
  pw_rpc_internal_RpcPacket packet;
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, WLAN_RPC_SERVICE_ID);
  EXPECT_EQ(packet.method_id, WLAN_RPC_SERVICE_COMMAND_METHOD_ID);

  noa_service_wlan_rpc_service_Request written_request;
  EXPECT_EQ(PwRpcClientDeserializeResponse(
                payload_buf, payload_len,
                noa_service_wlan_rpc_service_Request_fields, &written_request),
            kPwStatusOk);
  EXPECT_EQ(written_request.id, kTestId);
  EXPECT_EQ(written_request.msg.size, sizeof(kTestCmd));
  EXPECT_STREQ(reinterpret_cast<const char*>(written_request.msg.bytes),
               kTestCmd);

  PwRpcClientDeinit(&client);
}

TEST(WlanRpcServiceClientTestNanopb, DeserializeCommandResponseSuccess) {
  uint8_t buf[32] = {0};

  noa_service_wlan_rpc_service_Response response;
  memset(&response, 0, sizeof(noa_service_wlan_rpc_service_Response));
  response.result = kTestResult;
  response.msg.size = sizeof(kTestMsg);
  memcpy(response.msg.bytes, kTestMsg, sizeof(kTestMsg));

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_wlan_rpc_service_Response_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_wlan_rpc_service_Response));
  EXPECT_EQ(WlanRpcServiceCommandDeserializeResponse(buf, stream.bytes_written,
                                                     &response),
            kPwStatusOk);
  EXPECT_EQ(response.result, kTestResult);
  EXPECT_EQ(response.msg.size, sizeof(kTestMsg));
  EXPECT_STREQ(reinterpret_cast<const char*>(response.msg.bytes), kTestMsg);
}

TEST(WlanRpcServiceClientTestNanopb, InvokeEventSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);
  uint32_t call_id = 0;
  EXPECT_EQ(WlanRpcServiceEvent(&client, nullptr, nullptr, nullptr, nullptr,
                                &call_id),
            kPwStatusOk);
  ASSERT_GT(test_transport_info.tx_bytes, 0U);

  uint8_t payload_buf[32];
  pw_rpc_internal_RpcPacket packet;
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, WLAN_RPC_SERVICE_ID);
  EXPECT_EQ(packet.method_id, WLAN_RPC_SERVICE_EVENT_METHOD_ID);

  noa_service_wlan_rpc_service_Response request;
  request.result = kTestResult;
  memcpy(request.msg.bytes, reinterpret_cast<const uint8_t*>(kTestMsg),
         sizeof(kTestMsg));
  request.msg.size = sizeof(kTestMsg);
  EXPECT_EQ(WlanRpcServiceEventNext(&client, call_id, &request), kPwStatusOk);
  EXPECT_EQ(WlanRpcServiceEventNext(&client, call_id, &request), kPwStatusOk);
  EXPECT_EQ(WlanRpcServiceEventComplete(&client, call_id), kPwStatusOk);

  PwRpcClientDeinit(&client);
}

TEST(WlanRpcServiceClientTestNanopb, DeserializeEventResponseSuccess) {
  uint8_t buf[32] = {0};

  noa_service_wlan_rpc_service_Request response;
  memset(&response, 0, sizeof(noa_service_wlan_rpc_service_Request));
  response.id = kTestId;
  response.msg.size = sizeof(kTestMsg);
  memcpy(response.msg.bytes, kTestMsg, sizeof(kTestMsg));

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_wlan_rpc_service_Request_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_wlan_rpc_service_Request));
  EXPECT_EQ(WlanRpcServiceEventDeserializeResponse(buf, stream.bytes_written,
                                                   &response),
            kPwStatusOk);
  EXPECT_EQ(response.id, kTestId);
  EXPECT_EQ(response.msg.size, sizeof(kTestMsg));
  EXPECT_STREQ(reinterpret_cast<const char*>(response.msg.bytes), kTestMsg);
}
}  // namespace
