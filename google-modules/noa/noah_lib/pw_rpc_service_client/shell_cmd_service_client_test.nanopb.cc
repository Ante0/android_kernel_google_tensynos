#include "string.h"

#include "gtest/gtest.h"
#include "mock_transport.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"
#include "pw_rpc_service_client/shell_cmd_service_client.nanopb.h"

namespace noa::service::shell_cmd_service {
namespace {

constexpr uint32_t kRpcChannelIdTest = 9;

struct TestTransport test_transport_info;

constexpr uint8_t kTestCmd[] = "test ncp-echo";

TEST(ShellCmdServiceClientTestNanopb, InvokeExecuteSuccess) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  noa_service_shell_cmd_service_Request request = {
      .cmd_string = {0},
  };
  memcpy(request.cmd_string, kTestCmd, sizeof(kTestCmd));
  EXPECT_EQ(ShellCmdServiceExecute(&client, &request, nullptr, nullptr, nullptr,
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
  EXPECT_EQ(packet.service_id, SHELL_CMD_SERVICE_ID);
  EXPECT_EQ(packet.method_id, SHELL_CMD_SERVICE_EXECUTE_METHOD_ID);
  EXPECT_GT(payload_len, 0U);

  noa_service_shell_cmd_service_Request written_request;
  memset(&written_request, 0, sizeof(noa_service_shell_cmd_service_Request));
  pb_istream_t stream = pb_istream_from_buffer(payload_buf, payload_len);
  pb_decode(&stream, noa_service_shell_cmd_service_Request_fields,
            &written_request);
  EXPECT_STREQ(written_request.cmd_string,
               reinterpret_cast<const char*>((kTestCmd)));

  PwRpcClientDeinit(&client);
}

TEST(ShellCmdServiceClientTestNanopb, InvokeExecuteInvalidArgumentNullClient) {
  noa_service_shell_cmd_service_Request request = {
      .cmd_string = {0},
  };
  memcpy(request.cmd_string, kTestCmd, sizeof(kTestCmd));
  EXPECT_EQ(ShellCmdServiceExecute(nullptr, &request, nullptr, nullptr, nullptr,
                                   nullptr),
            kPwStatusInvalidArgument);
}

TEST(ShellCmdServiceClientTestNanopb, InvokeExecuteInvalidArgumentNullRequest) {
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  EXPECT_EQ(ShellCmdServiceExecute(&client, nullptr, nullptr, nullptr, nullptr,
                                   nullptr),
            kPwStatusInvalidArgument);
  PwRpcClientDeinit(&client);
}

TEST(ShellCmdServiceClientTestNanopb, DeserializeExecuteResponseSuccess) {
  uint8_t buf[128] = {0};

  noa_service_shell_cmd_service_Response response;
  memset(&response, 0, sizeof(noa_service_shell_cmd_service_Response));
  response.result = noa_service_shell_cmd_service_CmdResult_FAILURE;

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_shell_cmd_service_Response_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_shell_cmd_service_Response));
  EXPECT_EQ(ShellCmdServiceExecuteDeserializeResponse(buf, stream.bytes_written,
                                                      &response),
            kPwStatusOk);
  EXPECT_EQ(response.result, noa_service_shell_cmd_service_CmdResult_FAILURE);
}

TEST(ShellCmdServiceClientTestNanopb, DeserializeExecuteResponseOkNullBytes) {
  noa_service_shell_cmd_service_Response response;
  EXPECT_EQ(ShellCmdServiceExecuteDeserializeResponse(nullptr, 32, &response),
            kPwStatusOk);
  EXPECT_EQ(response.result, noa_service_shell_cmd_service_CmdResult_SUCCESS);
}

TEST(ShellCmdServiceClientTestNanopb, DeserializeExecuteResponseOkZeroLen) {
  uint8_t buf[32] = {0};
  noa_service_shell_cmd_service_Response response;
  EXPECT_EQ(ShellCmdServiceExecuteDeserializeResponse(buf, 0, &response),
            kPwStatusOk);
  EXPECT_EQ(response.result, noa_service_shell_cmd_service_CmdResult_SUCCESS);
}

TEST(ShellCmdServiceClientTestNanopb,
     DeserializeExecuteResponseInvalidArgumentNullResponse) {
  uint8_t buf[32] = {0};

  noa_service_shell_cmd_service_Response response;
  memset(&response, 0, sizeof(noa_service_shell_cmd_service_Response));
  response.result = noa_service_shell_cmd_service_CmdResult_FAILURE;

  pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
  ASSERT_TRUE(pb_encode(&stream, noa_service_shell_cmd_service_Response_fields,
                        &response));

  memset(&response, 0, sizeof(noa_service_shell_cmd_service_Response));
  EXPECT_EQ(
      ShellCmdServiceExecuteDeserializeResponse(buf, sizeof(buf), nullptr),
      kPwStatusInvalidArgument);
}
}  // namespace
}  // namespace noa::service::shell_cmd_service
