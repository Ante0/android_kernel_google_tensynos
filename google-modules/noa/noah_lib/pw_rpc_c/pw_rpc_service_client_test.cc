#include "pw_rpc_c/pw_rpc_service_client.h"

#include <optional>
#include <utility>

#include "gtest/gtest.h"
#include "pw_containers/vector.h"
#include "pw_rpc/nanopb/server_reader_writer.h"
#include "pw_rpc_c/client_server_testing.h"
#include "pw_rpc_c/pw_rpc_client.h"
#include "pw_rpc_service_client_test.rpc.pb.h"
#include "pw_status/status.h"

namespace noa::module::linux_rpc_client {

constexpr uint32_t kTestServiceId = 0x80f8a29c;
constexpr uint32_t kTestClientStreamingMethodId = 0x7f9a96bd;
constexpr uint32_t kTestServerStreamingMethodId = 0x2dc28bb5;
constexpr uint32_t kTestBidirectionalStreamingMethodId = 0xf5521f9c;
constexpr uint32_t kMaxPayloadSize = 128;

PW_RPC_SERVER_STREAMING_METHOD(Test, kTestServiceId, ServerStreamingMethod,
                               kTestServerStreamingMethodId,
                               noa_module_linux_rpc_client_Request,
                               noa_module_linux_rpc_client_Response);

PW_RPC_CLIENT_STREAMING_METHOD(Test, kTestServiceId, ClientStreamingMethod,
                               kTestClientStreamingMethodId,
                               noa_module_linux_rpc_client_Request,
                               noa_module_linux_rpc_client_Response);

PW_RPC_BIDIRECTIONAL_STREAMING_METHOD(Test, kTestServiceId,
                                      BidirectionalStreamingMethod,
                                      kTestBidirectionalStreamingMethodId,
                                      noa_module_linux_rpc_client_Request,
                                      noa_module_linux_rpc_client_Response);

namespace {
using Request = ::noa_module_linux_rpc_client_Request;
using Response = ::noa_module_linux_rpc_client_Response;

class ServiceMock : public pw_rpc::nanopb::Test::Service<ServiceMock> {
 public:
  void BidirectionalStreamingMethod(
      pw::rpc::NanopbServerReaderWriter<Request, Response>& call) {
    bidirectional_streaming_call_.emplace(std::move(call));
  }

  pw::rpc::NanopbServerReaderWriter<Request, Response>*
  BidirectionalStreamingCall() {
    return bidirectional_streaming_call_.has_value()
               ? &bidirectional_streaming_call_.value()
               : nullptr;
  }

  void ClientStreamingMethod(
      pw::rpc::NanopbServerReader<Request, Response>& call) {
    client_streaming_call_.emplace(std::move(call));
  }

  pw::rpc::NanopbServerReader<Request, Response>* ClientStreamingCall() {
    return client_streaming_call_.has_value() ? &client_streaming_call_.value()
                                              : nullptr;
  }

  void ServerStreamingMethod(const Request& request,
                             pw::rpc::NanopbServerWriter<Response>& call) {
    received_server_streaming_request_ = request;
    server_streaming_call_.emplace(std::move(call));
  }

  pw::rpc::NanopbServerWriter<Response>* ServerStreamingCall() {
    return server_streaming_call_.has_value() ? &server_streaming_call_.value()
                                              : nullptr;
  }

  const Request& ReceivedServerStreamingRequest() {
    return received_server_streaming_request_;
  }

 private:
  std::optional<pw::rpc::NanopbServerReaderWriter<Request, Response>>
      bidirectional_streaming_call_;
  std::optional<pw::rpc::NanopbServerReader<Request, Response>>
      client_streaming_call_;
  std::optional<pw::rpc::NanopbServerWriter<Response>> server_streaming_call_;
  Request received_server_streaming_request_;
};

class PwRpcServiceClientTest : public ::testing::Test {
 protected:
  void SetUp() override { rpc_context_.Server().RegisterService(service_); }

  PwRpcClientServerTestContext<> rpc_context_;
  ServiceMock service_;
};

class PwRpcServiceClientOnNextTest : public PwRpcServiceClientTest {
 protected:
  struct TestContext {
    uint32_t on_next_received_count;
    pw::Vector<std::uint8_t, kMaxPayloadSize> on_next_received_payload;
  };

  static void OnNextCallback(struct PwRpcCallStruct*, const uint8_t* payload,
                             size_t payload_size) {
    ++test_context_.on_next_received_count;
    test_context_.on_next_received_payload.resize(payload_size);
    std::memcpy(test_context_.on_next_received_payload.data(), payload,
                payload_size);
  }

  void SetUp() override {
    PwRpcServiceClientTest::SetUp();
    test_context_.on_next_received_count = 0;
    test_context_.on_next_received_payload.clear();
  }

  static inline TestContext test_context_;
};

class PwRpcServiceClientOnCompleteTest : public PwRpcServiceClientTest {
 protected:
  struct TestContext {
    uint32_t on_complete_count;
    PwStatus on_complete_status;
    pw::Vector<std::uint8_t, kMaxPayloadSize> on_complete_received_payload;
  };

  static void OnCompleteCallback(struct PwRpcCallStruct*,
                                 const uint8_t* payload, size_t payload_size,
                                 PwStatus status) {
    test_context_.on_complete_status = status;
    ++test_context_.on_complete_count;
    test_context_.on_complete_received_payload.resize(payload_size);
    std::memcpy(test_context_.on_complete_received_payload.data(), payload,
                payload_size);
  }

  void SetUp() override {
    PwRpcServiceClientTest::SetUp();
    test_context_.on_complete_count = 0;
    test_context_.on_complete_received_payload.clear();
  }

  static inline TestContext test_context_;
};

class PwRpcServiceClientOnErrorTest : public PwRpcServiceClientTest {
 protected:
  struct TestContext {
    PwStatus on_error_status;
    uint32_t on_error_count;
  };

  static void OnErrorCallback(struct PwRpcCallStruct*, PwStatus status) {
    test_context_.on_error_status = status;
    ++test_context_.on_error_count;
  }

  void SetUp() override {
    PwRpcServiceClientTest::SetUp();
    test_context_.on_error_count = 0;
  }

  static inline TestContext test_context_;
};

TEST_F(PwRpcServiceClientTest, ServerStreamingEstablishesCall) {
  constexpr Request kExpectedRequest = {.value = 42};
  ASSERT_EQ(kPwStatusOk, TestServerStreamingMethod(
                             &rpc_context_.Client(), &kExpectedRequest, nullptr,
                             nullptr, nullptr, nullptr, nullptr));
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_NE(service_.ServerStreamingCall(), nullptr);
  EXPECT_TRUE(service_.ServerStreamingCall()->active());
  EXPECT_EQ(service_.ReceivedServerStreamingRequest().value,
            kExpectedRequest.value);
}

TEST_F(PwRpcServiceClientOnNextTest, ServerStreamingOnNextTriggered) {
  constexpr Response kExpectedResponse = {.value = 42};

  // Initiate an RPC call and register the OnNextCallback to handle
  // received messages.
  Request request = {};
  ASSERT_EQ(kPwStatusOk, TestServerStreamingMethod(
                             &rpc_context_.Client(), &request, OnNextCallback,
                             nullptr, nullptr, nullptr, nullptr));
  rpc_context_.ForwardNewPackets();
  ASSERT_NE(service_.ServerStreamingCall(), nullptr);

  // Send a ServerStream message from the server.
  service_.ServerStreamingCall()->Write(kExpectedResponse);
  rpc_context_.ForwardNewPackets();

  Response received_response;
  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_EQ(test_context_.on_next_received_count, 1u);
  ASSERT_EQ(kPwStatusOk, TestServerStreamingMethodDeserializeResponse(
                             test_context_.on_next_received_payload.data(),
                             test_context_.on_next_received_payload.size(),
                             &received_response));
  EXPECT_EQ(received_response.value, kExpectedResponse.value);
}

TEST_F(PwRpcServiceClientOnCompleteTest, ServerStreamingOnCompleteTriggered) {
  constexpr pw::Status kExpectedCompleteStatus = pw::Status::DataLoss();

  // Initiate an RPC call and register the OnComplateCallback to handle
  // the ServerResponse packet that signals completion of the RPC call.
  Request request = {};
  ASSERT_EQ(kPwStatusOk, TestServerStreamingMethod(
                             &rpc_context_.Client(), &request, nullptr,
                             OnCompleteCallback, nullptr, nullptr, nullptr));
  rpc_context_.ForwardNewPackets();
  ASSERT_NE(service_.ServerStreamingCall(), nullptr);

  // Simulate the server sending a ServerResponse to complete the RPC call
  service_.ServerStreamingCall()->Finish(kExpectedCompleteStatus);
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_EQ(test_context_.on_complete_count, 1u);
  EXPECT_EQ(test_context_.on_complete_status,
            static_cast<PwStatus>(kExpectedCompleteStatus.code()));
  EXPECT_EQ(test_context_.on_complete_received_payload.size(), 0u);
}

TEST_F(PwRpcServiceClientOnErrorTest, ServerStreamingOnErrorTriggered) {
  rpc_context_.Server().UnregisterService(service_);

  // Initiate a RPC call.
  // The server will intentionally respond with a 'NotFound' ServerError since
  // the service has been unregistered.
  // The OnErrorCallback function will be triggered by this error.
  Request request = {};
  ASSERT_EQ(kPwStatusOk, TestServerStreamingMethod(
                             &rpc_context_.Client(), &request, nullptr, nullptr,
                             OnErrorCallback, nullptr, nullptr));
  rpc_context_.ForwardNewPackets();

  EXPECT_EQ(test_context_.on_error_status, kPwStatusNotFound);
  EXPECT_EQ(test_context_.on_error_count, 1u);
}

TEST_F(PwRpcServiceClientTest, ClientStreamingEstablishesCall) {
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk,
            TestClientStreamingMethod(&rpc_context_.Client(), nullptr, nullptr,
                                      nullptr, &call_id));
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_NE(service_.ClientStreamingCall(), nullptr);
  EXPECT_TRUE(service_.ClientStreamingCall()->active());
}

TEST_F(PwRpcServiceClientTest, ClientStreamingSendsCompletionRequest) {
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk,
            TestClientStreamingMethod(&rpc_context_.Client(), nullptr, nullptr,
                                      nullptr, &call_id));
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_NE(service_.ClientStreamingCall(), nullptr);

  ASSERT_EQ(kPwStatusOk,
            TestClientStreamingMethodComplete(&rpc_context_.Client(), call_id));
  rpc_context_.ForwardNewPackets();

  EXPECT_GT(rpc_context_.ClientTestContext()
                .output()
                .client_stream_end_packets<
                    pw_rpc::nanopb::Test::ClientStreamingMethod>(),
            0u);
}

TEST_F(PwRpcServiceClientTest, ClientStreamingSendsClientStreamMessage) {
  constexpr Request kExpectedReceivedMessage = {.value = 42};

  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk,
            TestClientStreamingMethod(&rpc_context_.Client(), nullptr, nullptr,
                                      nullptr, &call_id));
  rpc_context_.ForwardNewPackets();
  ASSERT_NE(service_.ClientStreamingCall(), nullptr);

  struct {
    Request message;
    uint32_t count = 0;
  } received;

  // Set up the server's rx handler to capture and record received messages.
  service_.ClientStreamingCall()->set_on_next(
      [&received](const Request& message) {
        received.message = message;
        ++received.count;
      });

  // Send the test message from the client.
  ASSERT_EQ(kPwStatusOk,
            TestClientStreamingMethodNext(&rpc_context_.Client(), call_id,
                                          &kExpectedReceivedMessage));
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_EQ(received.count, 1u);
  EXPECT_EQ(received.message.value, kExpectedReceivedMessage.value);
}

TEST_F(PwRpcServiceClientOnCompleteTest, ClientStreamingOnCompleteTriggered) {
  constexpr Response kExpectedResponse = {.value = 42};
  constexpr pw::Status kExpectedCompleteStatus = pw::Status::Aborted();

  // Initiate an RPC call and register the OnComplateCallback to handle
  // the ServerResponse packet that signals completion of the RPC call.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, TestClientStreamingMethod(&rpc_context_.Client(),
                                                   OnCompleteCallback, nullptr,
                                                   nullptr, &call_id));
  rpc_context_.ForwardNewPackets();
  ASSERT_NE(service_.ClientStreamingCall(), nullptr);

  // Simulate the server sending a ServerResponse to complete the RPC call.
  // This packet contains a message payload for client streaming RPC.
  service_.ClientStreamingCall()->Finish(kExpectedResponse,
                                         kExpectedCompleteStatus);
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_EQ(test_context_.on_complete_count, 1u);

  Response received_response;
  ASSERT_EQ(kPwStatusOk, TestClientStreamingMethodDeserializeResponse(
                             test_context_.on_complete_received_payload.data(),
                             test_context_.on_complete_received_payload.size(),
                             &received_response));
  EXPECT_EQ(received_response.value, kExpectedResponse.value);

  EXPECT_EQ(test_context_.on_complete_status,
            static_cast<PwStatus>(kExpectedCompleteStatus.code()));
}

TEST_F(PwRpcServiceClientOnErrorTest, ClientStreamingOnErrorTriggered) {
  rpc_context_.Server().UnregisterService(service_);

  // Initiate a RPC call.
  // The server will intentionally respond with a 'NotFound' ServerError since
  // the service has been unregistered.
  // The OnErrorCallback function will be triggered by this error.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk,
            TestClientStreamingMethod(&rpc_context_.Client(), nullptr,
                                      OnErrorCallback, nullptr, &call_id));
  rpc_context_.ForwardNewPackets();

  EXPECT_EQ(test_context_.on_error_status, kPwStatusNotFound);
  EXPECT_EQ(test_context_.on_error_count, 1u);
}

TEST_F(PwRpcServiceClientTest, BidirectionalStreamingEstablishesCall) {
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethod(
                             &rpc_context_.Client(), nullptr, nullptr, nullptr,
                             nullptr, &call_id));
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_NE(service_.BidirectionalStreamingCall(), nullptr);
  EXPECT_TRUE(service_.BidirectionalStreamingCall()->active());
}

TEST_F(PwRpcServiceClientTest, BidirectionalStreamingSendsCompletionRequest) {
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethod(
                             &rpc_context_.Client(), nullptr, nullptr, nullptr,
                             nullptr, &call_id));
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_NE(service_.BidirectionalStreamingCall(), nullptr);

  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethodComplete(
                             &rpc_context_.Client(), call_id));
  rpc_context_.ForwardNewPackets();

  EXPECT_GT(rpc_context_.ClientTestContext()
                .output()
                .client_stream_end_packets<
                    pw_rpc::nanopb::Test::BidirectionalStreamingMethod>(),
            0u);
}

TEST_F(PwRpcServiceClientTest, BidirectionalStreamingSendsClientStreamMessage) {
  constexpr Request kExpectedReceivedMessage = {.value = 42};

  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethod(
                             &rpc_context_.Client(), nullptr, nullptr, nullptr,
                             nullptr, &call_id));
  rpc_context_.ForwardNewPackets();
  ASSERT_NE(service_.BidirectionalStreamingCall(), nullptr);

  struct {
    Request message;
    uint32_t count = 0;
  } received;

  // Set up the server's rx handler to capture and record received messages.
  service_.BidirectionalStreamingCall()->set_on_next(
      [&received](const Request& message) {
        received.message = message;
        ++received.count;
      });

  // Send the test message from the client.
  ASSERT_EQ(kPwStatusOk,
            TestBidirectionalStreamingMethodNext(
                &rpc_context_.Client(), call_id, &kExpectedReceivedMessage));
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_EQ(received.count, 1u);
  EXPECT_EQ(received.message.value, kExpectedReceivedMessage.value);
}

TEST_F(PwRpcServiceClientOnNextTest, BidirectionalStreamingOnNextTriggered) {
  constexpr Response kExpectedReceivedMessage = {.value = 42};

  // Initiate an RPC call and register the OnNextCallback to handle
  // received messages.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethod(
                             &rpc_context_.Client(), OnNextCallback, nullptr,
                             nullptr, nullptr, &call_id));
  rpc_context_.ForwardNewPackets();
  ASSERT_NE(service_.BidirectionalStreamingCall(), nullptr);

  // Send the test message from the server.
  service_.BidirectionalStreamingCall()->Write(kExpectedReceivedMessage);
  rpc_context_.ForwardNewPackets();

  Response received_response;
  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_EQ(test_context_.on_next_received_count, 1u);
  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethodDeserializeResponse(
                             test_context_.on_next_received_payload.data(),
                             test_context_.on_next_received_payload.size(),
                             &received_response));
  EXPECT_EQ(received_response.value, kExpectedReceivedMessage.value);
}

TEST_F(PwRpcServiceClientOnCompleteTest,
       BidirectionalStreamingOnCompleteTriggered) {
  constexpr pw::Status kExpectedCompleteStatus = pw::Status::InvalidArgument();

  // Initiate an RPC call and register the OnComplateCallback to handle
  // the ServerResponse packet that signals completion of the RPC call.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethod(
                             &rpc_context_.Client(), nullptr,
                             OnCompleteCallback, nullptr, nullptr, &call_id));
  rpc_context_.ForwardNewPackets();
  ASSERT_NE(service_.BidirectionalStreamingCall(), nullptr);

  // Simulate the server sending a ServerResponse to complete the RPC call
  service_.BidirectionalStreamingCall()->Finish(kExpectedCompleteStatus);
  rpc_context_.ForwardNewPackets();

  ASSERT_EQ(pw::OkStatus(), rpc_context_.Status());
  ASSERT_EQ(test_context_.on_complete_count, 1u);
  EXPECT_EQ(test_context_.on_complete_status,
            static_cast<PwStatus>(kExpectedCompleteStatus.code()));
  EXPECT_EQ(test_context_.on_complete_received_payload.size(), 0u);
}

TEST_F(PwRpcServiceClientOnErrorTest, BidirectionalStreamingOnErrorTriggered) {
  rpc_context_.Server().UnregisterService(service_);

  // Initiate a RPC call.
  // The server will intentionally respond with a 'NotFound' ServerError since
  // the service has been unregistered.
  // The OnErrorCallback function will be triggered by this error.
  uint32_t call_id;
  ASSERT_EQ(kPwStatusOk, TestBidirectionalStreamingMethod(
                             &rpc_context_.Client(), nullptr, nullptr,
                             OnErrorCallback, nullptr, &call_id));
  rpc_context_.ForwardNewPackets();

  EXPECT_EQ(test_context_.on_error_status, kPwStatusNotFound);
  EXPECT_EQ(test_context_.on_error_count, 1u);
}

}  // namespace
}  // namespace noa::module::linux_rpc_client
