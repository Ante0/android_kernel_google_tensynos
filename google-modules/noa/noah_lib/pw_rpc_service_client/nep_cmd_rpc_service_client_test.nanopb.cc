#include <cstring>

#include "gtest/gtest.h"
#include "mock_transport.h"
#include "nep_cmd_service.pb.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/nep_cmd_rpc_service_client.nanopb.h"

namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;
constexpr uint32_t kTestId = 45;
constexpr char kTestCmd[] = {"TestCommand"};
constexpr noa_service_nep_cmd_service_CmdResult kTestResult =
    noa_service_nep_cmd_service_CmdResult_FAILURE;

TEST(NepCmdServiceClientTestNanopb, InvokeCommandSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker,
                      reinterpret_cast<void*>(&test_transport_info)),
      kPwStatusOk);
  noa_service_nep_cmd_service_Request request;
  request.cmd = kTestId;
  memcpy(request.msg.bytes, reinterpret_cast<const uint8_t*>(kTestCmd),
         sizeof(kTestCmd));
  request.msg.size = sizeof(kTestCmd);

  EXPECT_EQ(NepCmdRpcServiceCommand(&client, &request, nullptr, nullptr,
                                    nullptr, nullptr),
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
  EXPECT_EQ(packet.service_id, NEP_CMD_RPC_SERVICE_ID);
  EXPECT_EQ(packet.method_id, NEP_CMD_RPC_SERVICE_COMMAND_METHOD_ID);

  noa_service_nep_cmd_service_Request written_request;
  EXPECT_EQ(PwRpcClientDeserializeResponse(
                payload_buf, payload_len,
                noa_service_nep_cmd_service_Request_fields, &written_request),
            kPwStatusOk);
  EXPECT_EQ(written_request.cmd, kTestId);
  EXPECT_EQ(written_request.msg.size, sizeof(kTestCmd));
  EXPECT_STREQ(reinterpret_cast<const char*>(written_request.msg.bytes),
               kTestCmd);

  PwRpcClientDeinit(&client);
}

TEST(NepCmdServiceClientTestNanopb, DeserializeCommandResponseSuccess) {
  uint8_t buf[32] = {0};

  noa_service_nep_cmd_service_Response response;
  memset(&response, 0, sizeof(noa_service_nep_cmd_service_Response));
  response.result = kTestResult;

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_nep_cmd_service_Response_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_nep_cmd_service_Response));
  EXPECT_EQ(NepCmdRpcServiceCommandDeserializeResponse(
                buf, stream.bytes_written, &response),
            kPwStatusOk);
  EXPECT_EQ(response.result, kTestResult);
}
}  // namespace
