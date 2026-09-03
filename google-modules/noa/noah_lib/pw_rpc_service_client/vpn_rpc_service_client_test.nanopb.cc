#include "pw_rpc_service_client/vpn_rpc_service_client.nanopb.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "gtest/gtest.h"
#include "pw_containers/vector.h"
#include "pw_rpc/nanopb/server_reader_writer.h"
#include "pw_rpc_c/client_server_testing.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "vpn_rpc_service/service.rpc.pb.h"

namespace noa::module::linux_rpc_client {
namespace {

class MockVpnRpcService
    : public noa::service::vpn_rpc_service::pw_rpc::nanopb::VpnRpc::Service<
          MockVpnRpcService> {
 public:
  using CallType =
      pw::rpc::NanopbServerReaderWriter<noa_service_vpn_rpc_service_ServerCmd,
                                        noa_service_vpn_rpc_service_ClientCmd>;

  void Connect(CallType& call) { last_call_.emplace(std::move(call)); }

  CallType* LastCall() {
    return last_call_.has_value() ? &last_call_.value() : nullptr;
  }

 private:
  std::optional<CallType> last_call_;
};

class VpnRpcServiceClientOnNextTest : public ::testing::Test {
 protected:
  struct TestContext {
    PwStatus on_next_status;
    uint32_t on_next_received_count;
    noa_service_vpn_rpc_service_ClientCmd on_next_received_message;
  };

  static void OnNextCallback(struct PwRpcCallStruct*, const uint8_t* payload,
                             size_t payload_size) {
    ++test_context_.on_next_received_count;
    test_context_.on_next_status = PwRpcClientDeserializeResponse(
        payload, payload_size, noa_service_vpn_rpc_service_ClientCmd_fields,
        &test_context_.on_next_received_message);
  }

  void SetUp() override { test_context_.on_next_received_count = 0; }

  static inline TestContext test_context_;
};

class VpnRpcServiceClientOnCompleteTest : public ::testing::Test {
 protected:
  struct TestContext {
    uint32_t on_complete_count;
    PwStatus on_complete_status;
  };

  static void OnCompleteCallback(struct PwRpcCallStruct*, const uint8_t*,
                                 size_t, PwStatus status) {
    test_context_.on_complete_status = status;
    ++test_context_.on_complete_count;
  }

  void SetUp() override { test_context_.on_complete_count = 0; }

  static inline TestContext test_context_;
};

class VpnRpcServiceClientOnErrorTest : public ::testing::Test {
 protected:
  struct TestContext {
    PwStatus on_error_status;
    uint32_t on_error_count;
  };

  static void OnErrorCallback(struct PwRpcCallStruct*, PwStatus status) {
    test_context_.on_error_status = status;
    ++test_context_.on_error_count;
  }

  void SetUp() override { test_context_.on_error_count = 0; }

  static inline TestContext test_context_;
};

TEST(VpnRpcServiceClientNanopbTest, EstablishesCallConnection) {
  PwRpcClientServerTestContext rpc_context;
  MockVpnRpcService mock_service;

  rpc_context.Server().RegisterService(mock_service);
  EXPECT_EQ(mock_service.LastCall(), nullptr);

  // Initiate a call connection.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, VpnRpcConnect(&rpc_context.Client(), nullptr, nullptr,
                                       nullptr, &call_id));
  rpc_context.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context.Status());
  ASSERT_NE(mock_service.LastCall(), nullptr);
  EXPECT_TRUE(mock_service.LastCall()->active());
}

TEST(VpnRpcServiceClientNanopbTest, VerifyClientStreamMessageContent) {
  constexpr pb_size_t kExpectedReceivedCommand =
      noa_module_vpn_ControllerCmd_xfrm_delete_sp_tag;
  constexpr noa_service_vpn_rpc_service_ServerCmd kExpectedReceivedMessage = {
      .has_controller_cmd = true,
      .controller_cmd = {.which_command = kExpectedReceivedCommand,
                         .command = {.xfrm_delete_sp = {.handle = 42}}}};

  PwRpcClientServerTestContext rpc_context;
  MockVpnRpcService mock_service;

  rpc_context.Server().RegisterService(mock_service);

  // Initiate a call connection.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, VpnRpcConnect(&rpc_context.Client(), nullptr, nullptr,
                                       nullptr, &call_id));
  rpc_context.ForwardNewPackets();
  ASSERT_NE(mock_service.LastCall(), nullptr);

  struct {
    noa_service_vpn_rpc_service_ServerCmd message;
    uint32_t count = 0;
  } received;

  // Set up the server's rx handler to capture and record received messages.
  mock_service.LastCall()->set_on_next(
      [&received](const noa_service_vpn_rpc_service_ServerCmd& message) {
        received.message = message;
        ++received.count;
      });

  // Send the test message from the client.
  ASSERT_EQ(kPwStatusOk, PwRpcClientInvokeStreamingNextPb(
                             &rpc_context.Client(), call_id,
                             noa_service_vpn_rpc_service_ServerCmd_fields,
                             &kExpectedReceivedMessage));

  rpc_context.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context.Status());
  ASSERT_EQ(received.count, 1u);
  EXPECT_EQ(received.message.has_controller_cmd,
            kExpectedReceivedMessage.has_controller_cmd);
  EXPECT_EQ(received.message.controller_cmd.which_command,
            kExpectedReceivedMessage.controller_cmd.which_command);
  EXPECT_EQ(
      received.message.controller_cmd.command.xfrm_delete_sp.handle,
      kExpectedReceivedMessage.controller_cmd.command.xfrm_delete_sp.handle);
}

TEST(VpnRpcServiceClientNanopbTest, VerifyClientStreamMessagesOrder) {
  constexpr std::array kExpectedReceivedCommandSequence = {
      noa_module_vpn_ControllerCmd_xfrm_delete_sa_tag,
      noa_module_vpn_ControllerCmd_xfrm_add_sp_tag,
      noa_module_vpn_ControllerCmd_xfrm_delete_sp_tag,
  };

  PwRpcClientServerTestContext rpc_context;
  MockVpnRpcService mock_service;

  rpc_context.Server().RegisterService(mock_service);

  // Initiate a call connection.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, VpnRpcConnect(&rpc_context.Client(), nullptr, nullptr,
                                       nullptr, &call_id));
  rpc_context.ForwardNewPackets();
  ASSERT_NE(mock_service.LastCall(), nullptr);

  pw::Vector<pb_size_t, kExpectedReceivedCommandSequence.size()>
      received_commands;

  // Set up the server's rx handler to capture and record command types.
  mock_service.LastCall()->set_on_next(
      [&received_commands](
          const noa_service_vpn_rpc_service_ServerCmd& message) {
        if (message.has_controller_cmd) {
          received_commands.push_back(message.controller_cmd.which_command);
        }
      });

  // Iterate through the expected command sequence and send each command type
  // from the client.
  for (pb_size_t command_type : kExpectedReceivedCommandSequence) {
    noa_service_vpn_rpc_service_ServerCmd command;

    command.has_controller_cmd = true;
    command.controller_cmd.which_command = command_type;

    EXPECT_EQ(kPwStatusOk,
              PwRpcClientInvokeStreamingNextPb(
                  &rpc_context.Client(), call_id,
                  noa_service_vpn_rpc_service_ServerCmd_fields, &command));
  }

  rpc_context.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context.Status());
  EXPECT_TRUE(std::equal(kExpectedReceivedCommandSequence.begin(),
                         kExpectedReceivedCommandSequence.end(),
                         received_commands.begin()));
}

TEST_F(VpnRpcServiceClientOnNextTest, CallbackTriggered) {
  constexpr noa_service_vpn_rpc_service_ClientCmd kExpectedReceivedMessage = {
      .which_command = noa_service_vpn_rpc_service_ClientCmd_adaptor_cmd_tag,
      .command = {.adaptor_cmd = {}}};

  PwRpcClientServerTestContext rpc_context;
  MockVpnRpcService mock_service;

  rpc_context.Server().RegisterService(mock_service);
  uint32_t call_id;

  // Initiate an RPC call and register the `OnNextCallback` to handle
  // received messages.
  ASSERT_EQ(kPwStatusOk, VpnRpcConnect(&rpc_context.Client(), OnNextCallback,
                                       nullptr, nullptr, &call_id));
  rpc_context.ForwardNewPackets();
  ASSERT_NE(mock_service.LastCall(), nullptr);

  // Send the test message from the server.
  mock_service.LastCall()->Write(kExpectedReceivedMessage);
  rpc_context.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context.Status());
  EXPECT_EQ(test_context_.on_next_status, kPwStatusOk);
  EXPECT_EQ(test_context_.on_next_received_count, 1u);
  EXPECT_EQ(test_context_.on_next_received_message.which_command,
            kExpectedReceivedMessage.which_command);
}

TEST_F(VpnRpcServiceClientOnCompleteTest, CallbackTriggered) {
  PwRpcClientServerTestContext rpc_context;
  MockVpnRpcService mock_service;

  rpc_context.Server().RegisterService(mock_service);

  // Initiate an RPC call and register the `OnComplateCallback` to handle
  // the ServerResponse packet that signals completion of the RPC call.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, VpnRpcConnect(&rpc_context.Client(), nullptr,
                                       OnCompleteCallback, nullptr, &call_id));
  rpc_context.ForwardNewPackets();
  ASSERT_NE(mock_service.LastCall(), nullptr);

  // Simulate the server sending a ServerResponse to complete the RPC call
  mock_service.LastCall()->Finish();
  rpc_context.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context.Status());
  EXPECT_EQ(test_context_.on_complete_status, kPwStatusOk);
  EXPECT_EQ(test_context_.on_complete_count, 1u);
}

TEST_F(VpnRpcServiceClientOnErrorTest, CallbackTriggered) {
  PwRpcClientServerTestContext rpc_context;
  MockVpnRpcService mock_service;

  rpc_context.Server().RegisterService(mock_service);

  // Initiate an RPC call to establish a connection.
  // Register the OnErrorCallback function to be invoked if the server sends
  // a ServerError packet, which indicates an unexpected termination of the
  // RPC call.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, VpnRpcConnect(&rpc_context.Client(), nullptr, nullptr,
                                       OnErrorCallback, &call_id));
  rpc_context.ForwardNewPackets();

  // Simulate an error by sending a messagge to a non-existent service.
  // This will trigger a NotFound ServerError.
  rpc_context.Server().UnregisterService(mock_service);
  noa_service_vpn_rpc_service_ServerCmd command;
  EXPECT_EQ(kPwStatusOk,
            PwRpcClientInvokeStreamingNextPb(
                &rpc_context.Client(), call_id,
                noa_service_vpn_rpc_service_ServerCmd_fields, &command));
  rpc_context.ForwardNewPackets();

  EXPECT_EQ(test_context_.on_error_status, kPwStatusNotFound);
  EXPECT_EQ(test_context_.on_error_count, 1u);
}

}  // namespace
}  // namespace noa::module::linux_rpc_client
