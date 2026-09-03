#include "string.h"

#include "gtest/gtest.h"
#include "mock_transport.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/dma_service_client.nanopb.h"

namespace noa::service::dma_service {
namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;

constexpr uint8_t kTestMsg[] = "test dma msg";

TEST(DmaServiceClientTestNanopb, InvokeWriteMessageSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  noa_service_dma_service_WriteMessageRequest request = {};
  memcpy(request.msg, kTestMsg, sizeof(kTestMsg));
  EXPECT_EQ(DmaServiceWriteMessage(&client, &request, nullptr, nullptr, nullptr,
                                   nullptr),
            kPwStatusOk);
  EXPECT_GT(test_transport_info.tx_bytes, 0U);

  pw_rpc_internal_RpcPacket packet;
  uint8_t payload_buf[32];
  size_t payload_len = 0;
  EXPECT_EQ(PwRpcPacketDecode(test_transport_info.tx_buf,
                              test_transport_info.tx_bytes, payload_buf,
                              sizeof(payload_buf), &packet, &payload_len),
            kPwStatusOk);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, DMA_SERVICE_ID);
  EXPECT_EQ(packet.method_id, DMA_SERVICE_WRITE_MESSAGE_METHOD_ID);
  EXPECT_GT(payload_len, 0U);

  noa_service_dma_service_WriteMessageRequest written_request = {};
  pb_istream_t stream = pb_istream_from_buffer(payload_buf, payload_len);
  pb_decode(&stream, noa_service_dma_service_WriteMessageRequest_fields,
            &written_request);
  EXPECT_STREQ(written_request.msg,
               reinterpret_cast<const char*>((kTestMsg)));

  PwRpcClientDeinit(&client);
}

TEST(DmaServiceClientTestNanopb, InvokeWriteMessageInvalidArgumentNullRequest) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  EXPECT_EQ(DmaServiceWriteMessage(&client, nullptr, nullptr, nullptr, nullptr,
                                   nullptr),
            kPwStatusInvalidArgument);
  PwRpcClientDeinit(&client);
}

TEST(DmaServiceClientTestNanopb, DeserializeWriteResponse) {
  uint8_t buf[128] = {0};

  noa_service_dma_service_WriteResponse response = {};

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_dma_service_WriteResponse_fields,
                        &response));

  response = {};
  EXPECT_EQ(DmaServiceWriteMessageDeserializeResponse(buf, stream.bytes_written,
                                                      &response),
            kPwStatusOk);
}

TEST(DmaServiceClientTestNanopb, DeserializeWriteMessageResponseOkNullBytes) {
  noa_service_dma_service_WriteResponse response;
  EXPECT_EQ(DmaServiceWriteMessageDeserializeResponse(nullptr, 32, &response),
            kPwStatusOk);
}

TEST(DmaServiceClientTestNanopb, DeserializeWriteMessageResponseOkZeroLen) {
  uint8_t buf[32] = {0};
  noa_service_dma_service_WriteResponse response;
  EXPECT_EQ(DmaServiceWriteMessageDeserializeResponse(buf, 0, &response),
            kPwStatusOk);
}

TEST(DmaServiceClientTestNanopb, InvokeDmaCopyDataInvalidArgumentNullRequest) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  EXPECT_EQ(DmaServiceDmaCopyData(&client, nullptr, nullptr, nullptr, nullptr,
                                   nullptr),
            kPwStatusInvalidArgument);
  PwRpcClientDeinit(&client);
}


}  // namespace
}  // namespace noa::service::dma_service
