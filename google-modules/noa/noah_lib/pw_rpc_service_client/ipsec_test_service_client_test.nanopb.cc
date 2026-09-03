#include "string.h"

#include "gtest/gtest.h"
#include "mock_transport.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/ipsec_test_service_client.nanopb.h"

namespace noa::service::ipsec_test_service {
namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;

constexpr uint8_t kTestCmd[] = "Custom data in the buffer";

TEST(IpsecTestServiceClientTestNanopb, InvokeSendDataSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  noa_service_ipsec_test_service_SendDataCmd request = {
      .id = 1,
      .offset = 0,
      .total = sizeof(kTestCmd),
      .data = {.size = sizeof(kTestCmd), .bytes = {0}},
  };
  memcpy(request.data.bytes, kTestCmd, sizeof(kTestCmd));
  EXPECT_EQ(IpsecTestServiceSendData(&client, &request, nullptr, nullptr, nullptr,
                                   nullptr),
            kPwStatusOk);
  EXPECT_GT(test_transport_info.tx_bytes, 0U);

  pw_rpc_internal_RpcPacket packet;
  uint8_t payload_buf[64];
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, SEND_DATA_SERVICE_ID);
  EXPECT_EQ(packet.method_id, SEND_DATA_EXECUTE_METHOD_ID);
  EXPECT_GT(payload_len, 0U);

  noa_service_ipsec_test_service_SendDataCmd written_request;
  memset(&written_request, 0, sizeof(noa_service_ipsec_test_service_SendDataCmd));
  pb_istream_t stream = pb_istream_from_buffer(payload_buf, payload_len);
  pb_decode(&stream, noa_service_ipsec_test_service_SendDataCmd_fields,
            &written_request);
  EXPECT_EQ(written_request.id, 1U);
  EXPECT_EQ(written_request.offset, 0U);
  EXPECT_EQ(written_request.total, sizeof(kTestCmd));
  EXPECT_EQ(written_request.data.size, sizeof(kTestCmd));
  EXPECT_EQ(memcmp(written_request.data.bytes, reinterpret_cast<const uint8_t*>((kTestCmd)), sizeof(kTestCmd)), 0);

  PwRpcClientDeinit(&client);
}

TEST(IpsecTestServiceClientTestNanopb, InvokeReceiveDataSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);


  noa_service_ipsec_test_service_EmptyMessage request;
  EXPECT_EQ(IpsecTestServiceReceiveData(&client, &request, nullptr, nullptr, nullptr,
                                   nullptr),
            kPwStatusOk);
  EXPECT_GT(test_transport_info.tx_bytes, 0U);

  pw_rpc_internal_RpcPacket packet;
  uint8_t payload_buf[64];
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, RECEIVE_DATA_SERVICE_ID);
  EXPECT_EQ(packet.method_id, RECEIVE_DATA_EXECUTE_METHOD_ID);
  EXPECT_EQ(payload_len, 0U);

  PwRpcClientDeinit(&client);
}

TEST(IpsecTestServiceClientTestNanopb, DeserializeReceiveDataResponseSuccess) {
  uint8_t buf[128] = {0};

  noa_service_ipsec_test_service_ReceiveDataRsp response;
  memset(&response, 0, sizeof(noa_service_ipsec_test_service_ReceiveDataRsp));

  response.has_output = true;
  response.output.id = 1U;
  response.output.offset = 0;
  response.output.total = 15;
  response.output.data.size = 15;
  memset(response.output.data.bytes, 0x7a, 15);

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_ipsec_test_service_ReceiveDataRsp_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_ipsec_test_service_ReceiveDataRsp));
  EXPECT_EQ(IpsecTestServiceReceiveDataDeserializeResponse(buf, stream.bytes_written,
                                                      &response),
            kPwStatusOk);
}

}  // namespace
}  // namespace noa::service::ipsec_test_service
