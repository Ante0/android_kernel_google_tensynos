#include <stdint.h>

#include "gtest/gtest.h"
#include "mock_transport.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/power_service_client.nanopb.h"

namespace noa::service::power_service {
namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;

TEST(PowerServiceClientTestNanopb, InvokePowerOffSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  noa_service_power_service_Request request = {.delay = 1};
  EXPECT_EQ(PowerServicePowerOff(&client, &request, nullptr, nullptr, nullptr, nullptr),
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
  EXPECT_EQ(packet.service_id, (uint32_t)POWER_SERVICE_ID);
  EXPECT_EQ(packet.method_id, (uint32_t)POWER_SERVICE_POWER_OFF_METHOD_ID);
  EXPECT_GT(payload_len, 0U);

  noa_service_power_service_Request written_request = {};
  pb_istream_t stream = pb_istream_from_buffer(payload_buf, payload_len);
  pb_decode(&stream, noa_service_power_service_Request_fields,
            &written_request);
  EXPECT_EQ(written_request.delay, request.delay);

  PwRpcClientDeinit(&client);
}

}  // namespace
}  // namespace noa::service::power_service
