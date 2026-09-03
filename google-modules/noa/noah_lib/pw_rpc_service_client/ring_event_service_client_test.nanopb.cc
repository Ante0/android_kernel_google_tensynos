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
#include "pw_rpc_service_client/ring_event_service_client.nanopb.h"
#include "ring_event_service.pb.h"

namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;
constexpr noa_service_ring_event_service_EventType kTestEvent =
    noa_service_ring_event_service_EventType_RING_ACTIVATE;
constexpr uint32_t kTestPort = 1;
constexpr uint32_t kTestType = 2;

TEST(RingEventServiceClientTestNanopb, InvokeActionSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);

  noa_service_ring_event_service_Request request;
  request.event = kTestEvent;
  request.port = kTestPort;
  request.type = kTestType;
  EXPECT_EQ(RingEventServiceAction(&client, &request, nullptr, nullptr, nullptr,
                                   nullptr, nullptr),
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
  EXPECT_EQ(packet.service_id, RING_EVENT_SERVICE_ID);
  EXPECT_EQ(packet.method_id, RING_EVENT_SERVICE_ACTION_METHOD_ID);

  noa_service_ring_event_service_Request written_request;
  EXPECT_EQ(
      PwRpcClientDeserializeResponse(
          payload_buf, payload_len,
          noa_service_ring_event_service_Request_fields, &written_request),
      kPwStatusOk);
  EXPECT_EQ(written_request.event, kTestEvent);
  EXPECT_EQ(written_request.port, kTestPort);
  EXPECT_EQ(written_request.type, kTestType);

  PwRpcClientDeinit(&client);
}

TEST(RingEventServiceClientTestNanopb, DeserializeActionResponseSuccess) {
  uint8_t buf[32] = {0};

  noa_service_ring_event_service_Response response;
  memset(&response, 0, sizeof(noa_service_ring_event_service_Response));

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_ring_event_service_Response_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_ring_event_service_Response));
  EXPECT_EQ(RingEventServiceActionDeserializeResponse(buf, stream.bytes_written,
                                                      &response),
            kPwStatusOk);
}
}  // namespace
