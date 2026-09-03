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
#include "pw_rpc_service_client/tea_rpc_service_client.nanopb.h"
#include "tea_rpc_service.pb.h"

namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;

TEST(TeaRpcServiceClientTestNanopb, InvokePushEventsSuccess) {
  PwRpcClient client;

  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);

  noa_service_tea_rpc_service_PushEventsRequest request =
      noa_service_tea_rpc_service_PushEventsRequest_init_zero;

  EXPECT_EQ(TeaRpcServicePushEvents(&client, &request,
                                    nullptr,
                                    nullptr,
                                    nullptr,
                                    nullptr,
				    nullptr),
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
  EXPECT_EQ(packet.service_id, TEA_RPC_SERVICE_ID);
  EXPECT_EQ(packet.method_id, TEA_RPC_SERVICE_PUSHEVENTS_METHOD_ID);
  EXPECT_EQ(payload_len, 0U);

  PwRpcClientDeinit(&client);
}

TEST(TeaRpcServiceClientTestNanopb, DeserializePushEventsResponseSuccess) {
  uint8_t buf[32] = {0};

  noa_service_tea_rpc_service_PushEventsResponse response =
      noa_service_tea_rpc_service_PushEventsResponse_init_zero;

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_tea_rpc_service_PushEventsResponse_fields,
                        &response));
  ASSERT_EQ(stream.bytes_written, 0U);

  response = noa_service_tea_rpc_service_PushEventsResponse_init_zero;


  EXPECT_EQ(TeaRpcServicePushEventsDeserializeResponse(buf, stream.bytes_written,
                                                       &response),
            kPwStatusOk);
}

} // namespace
