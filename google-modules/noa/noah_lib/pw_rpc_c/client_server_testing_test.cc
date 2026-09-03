#include "pw_rpc_c/client_server_testing.h"

#include "gtest/gtest.h"
#include "pw_rpc/echo.pb.h"
#include "pw_rpc/echo_service_nanopb.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_c/pw_status.h"

namespace noa::module::linux_rpc_client {
namespace {

struct TestContext {
  pw_rpc_EchoMessage response;
  PwStatus status;
} test_context;

void OnEchoResponse(struct PwRpcCallStruct*, const uint8_t* payload,
                    size_t payload_size, PwStatus status) {
  test_context.status = PwRpcClientDeserializeResponse(
      payload, payload_size, pw_rpc_EchoMessage_fields, &test_context.response);
  if (status != kPwStatusOk) {
    test_context.status = status;
  }
}

TEST(PwRpcClientServerTest, EchoServiceEchoSucceeds) {
  constexpr uint32_t kEchoServiceId = 0x14fbd052U;
  constexpr uint32_t kEchoServiceEchoMethodId = 0x8b470ee9U;

  PwRpcClientServerTestContext rpc_context;

  pw::rpc::EchoService echo_service;
  rpc_context.Server().RegisterService(echo_service);

  pw_rpc_EchoMessage test_message{"ClientServerTest"};

  ASSERT_EQ(kPwStatusOk,
            PwRpcClientInvokeUnaryPb(
                &rpc_context.Client(), kEchoServiceId, kEchoServiceEchoMethodId,
                pw_rpc_EchoMessage_fields, &test_message, OnEchoResponse,
                nullptr, nullptr, nullptr));

  rpc_context.ForwardNewPackets();
  ASSERT_EQ(kPwStatusOk, test_context.status);
  EXPECT_STREQ(test_message.msg, test_context.response.msg);
}

}  // namespace
}  // namespace noa::module::linux_rpc_client
