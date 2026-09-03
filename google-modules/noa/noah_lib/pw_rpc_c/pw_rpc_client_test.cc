#include "pw_rpc_c/pw_rpc_client.h"
#include "gtest/gtest.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "pw_rpc/internal/packet.pb.h"
#include "pw_rpc_c/pw_rpc_call.h"
#include "pw_rpc_c/pw_rpc_call_list.h"
#include "pw_rpc_c/pw_rpc_packet.h"
#include "pw_rpc_c/pw_status.h"

namespace noa::module::linux_rpc_client::pw_rpc_c {
namespace {

constexpr uint32_t kRpcChannelIdInvalid = 0;
constexpr uint32_t kRpcChannelIdTest = 9;
constexpr uint32_t kTestCallId = 99;

typedef struct {
  uint8_t tx_buf[1024];
  size_t tx_bytes;
  uint8_t rx_buf[1024];
} TestTransport;

PwStatus TestRpcChannelInputWorker(PwRpcClient* /* client */) {
  return kPwStatusOk;
}

PwStatus TestRpcChannelOutputHandler(PwRpcClient* client, const uint8_t* buf,
                                     size_t len) {
  TestTransport* transport_info = (TestTransport*)client->transport_info;
  transport_info->tx_bytes = 0;

  if (sizeof(transport_info->tx_buf) < len) {
    return kPwStatusResourceExhausted;
  }
  memcpy(transport_info->tx_buf, buf, len);
  transport_info->tx_bytes = len;
  return kPwStatusOk;
}

TEST(PwRpcClientTest, InitSunnyCase) {
  TestTransport test_transport_info;
  PwRpcClient client;
  EXPECT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);
  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, InitFailWithoutClient) {
  TestTransport test_transport_info;
  EXPECT_EQ(
      PwRpcClientInit(nullptr, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusInvalidArgument);
}

TEST(PwRpcClientTest, InitFailWithoutValidRpcChannel) {
  TestTransport test_transport_info;
  PwRpcClient client;
  EXPECT_EQ(PwRpcClientInit(
                &client, kRpcChannelIdInvalid, TestRpcChannelOutputHandler,
                TestRpcChannelInputWorker, (void*)&test_transport_info),
            kPwStatusInvalidArgument);
}

TEST(PwRpcClientTest, InitFailWithoutTxHandler) {
  TestTransport test_transport_info;
  PwRpcClient client;
  EXPECT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, nullptr,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusInvalidArgument);
}

TEST(PwRpcClientTest, InitFailWithoutRxWorker) {
  TestTransport test_transport_info;
  PwRpcClient client;
  EXPECT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      nullptr, (void*)&test_transport_info),
      kPwStatusInvalidArgument);
}

TEST(PwRpcClientTest, InitFailWithoutTransportInfo) {
  PwRpcClient client;
  EXPECT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, nullptr),
      kPwStatusInvalidArgument);
}

TEST(PwRpcClientTest, DeinitWithoutInit) {
  PwRpcClient client;
  memset(&client, 0, sizeof(PwRpcClient));
  EXPECT_EQ(PwRpcClientDeinit(&client), kPwStatusInvalidArgument);
}

TEST(PwRpcClientTest, DeinitFailWithoutClient) {
  EXPECT_EQ(PwRpcClientDeinit(nullptr), kPwStatusInvalidArgument);
}

constexpr uint32_t kServiceId = 0xabcd1234;
constexpr uint32_t kMethodId = 0xdeadbeef;
constexpr char kPayload[] = "Hello";

void OnNext(PwRpcCall* call, const uint8_t* payload, size_t payload_size) {
  *((pw_rpc_internal_PacketType*)call->context) =
      pw_rpc_internal_PacketType_SERVER_STREAM;
  EXPECT_EQ(payload_size, sizeof(kPayload));
  EXPECT_STREQ((const char* const)payload, kPayload);
}

void OnComplete(PwRpcCall* call, const uint8_t* payload, size_t payload_size,
                PwStatus status) {
  (void)(status);
  *((pw_rpc_internal_PacketType*)call->context) =
      pw_rpc_internal_PacketType_RESPONSE;
  call->state = kPwRpcCallComplete;
  EXPECT_EQ(status, kPwStatusOk);
  EXPECT_EQ(payload_size, sizeof(kPayload));
  EXPECT_STREQ((const char* const)payload, kPayload);
}

void OnError(PwRpcCall* call, PwStatus error) {
  (void)(error);
  *((pw_rpc_internal_PacketType*)call->context) =
      pw_rpc_internal_PacketType_SERVER_ERROR;
  call->state = kPwRpcCallComplete;
  EXPECT_EQ(error, kPwStatusUnknown);
}

TEST(PwRpcClientTest, InvokeUnary) {
  TestTransport test_transport_info;
  uint8_t* encoded_bytes;
  size_t encoded_byte_len = 0;

  ASSERT_EQ(PwRpcPacketEncode(pw_rpc_internal_PacketType_REQUEST, 0,
                              kRpcChannelIdTest, kServiceId, kMethodId,
                              reinterpret_cast<const uint8_t*>(kPayload),
                              sizeof(kPayload), kPwStatusOk, &encoded_bytes,
                              &encoded_byte_len),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  ASSERT_EQ(PwRpcClientInvokeUnary(&client, kServiceId, kMethodId,
                                   (const uint8_t*)kPayload, sizeof(kPayload),
                                   OnComplete, OnError, nullptr, nullptr),
            kPwStatusOk);

  pw_rpc_internal_RpcPacket packet;
  uint8_t payload_buf[16] = {0};
  size_t payload_size = 0;
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &packet, &payload_size);
  EXPECT_EQ(test_transport_info.tx_bytes, encoded_byte_len);
  EXPECT_EQ(packet.type, pw_rpc_internal_PacketType_REQUEST);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, kServiceId);
  EXPECT_EQ(packet.method_id, kMethodId);
  EXPECT_EQ(packet.status, kPwStatusOk);
  EXPECT_EQ(packet.call_id, 0U);
  EXPECT_EQ(memcmp(payload_buf, kPayload, payload_size), 0);
  free(encoded_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, InvokeStreaming) {
  TestTransport test_transport_info;
  uint8_t* encoded_bytes;
  size_t encoded_byte_len = 0;

  ASSERT_EQ(PwRpcPacketEncode(pw_rpc_internal_PacketType_REQUEST, 0,
                              kRpcChannelIdTest, kServiceId, kMethodId,
                              reinterpret_cast<const uint8_t*>(kPayload),
                              sizeof(kPayload), kPwStatusOk, &encoded_bytes,
                              &encoded_byte_len),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  ASSERT_EQ(
      PwRpcClientInvokeStreaming(&client, kServiceId, kMethodId,
                                 (const uint8_t*)kPayload, sizeof(kPayload),
                                 OnNext, OnComplete, OnError, nullptr, nullptr),
      kPwStatusOk);

  pw_rpc_internal_RpcPacket packet;
  uint8_t payload_buf[16] = {0};
  size_t payload_size = 0;
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &packet, &payload_size);
  EXPECT_EQ(test_transport_info.tx_bytes, encoded_byte_len);
  EXPECT_EQ(packet.type, pw_rpc_internal_PacketType_REQUEST);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, kServiceId);
  EXPECT_EQ(packet.method_id, kMethodId);
  EXPECT_EQ(packet.status, kPwStatusOk);
  EXPECT_EQ(packet.call_id, 0U);
  EXPECT_EQ(memcmp(payload_buf, kPayload, payload_size), 0);
  free(encoded_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, InvokeClientStreaming) {
  TestTransport test_transport_info;
  uint8_t* encoded_bytes;
  size_t encoded_byte_len = 0;

  // Encode a request packet.
  ASSERT_EQ(PwRpcPacketEncode(pw_rpc_internal_PacketType_REQUEST, 0,
                              kRpcChannelIdTest, kServiceId, kMethodId,
                              reinterpret_cast<const uint8_t*>(kPayload),
                              sizeof(kPayload), kPwStatusOk, &encoded_bytes,
                              &encoded_byte_len),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  uint32_t call_id = 0;
  ASSERT_EQ(PwRpcClientInvokeStreaming(&client, kServiceId, kMethodId,
                                       (const uint8_t*)kPayload,
                                       sizeof(kPayload), OnNext, OnComplete,
                                       OnError, nullptr, &call_id),
            kPwStatusOk);

  pw_rpc_internal_RpcPacket packet;
  uint8_t payload_buf[16] = {0};
  size_t payload_size = 0;
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &packet, &payload_size);
  EXPECT_EQ(test_transport_info.tx_bytes, encoded_byte_len);
  EXPECT_EQ(packet.type, pw_rpc_internal_PacketType_REQUEST);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, kServiceId);
  EXPECT_EQ(packet.method_id, kMethodId);
  EXPECT_EQ(packet.status, kPwStatusOk);
  EXPECT_EQ(packet.call_id, 0U);
  EXPECT_EQ(memcmp(payload_buf, kPayload, payload_size), 0);
  free(encoded_bytes);

  // Encode a streaming packet.
  ASSERT_EQ(PwRpcPacketEncode(pw_rpc_internal_PacketType_CLIENT_STREAM, 0,
                              kRpcChannelIdTest, kServiceId, kMethodId,
                              reinterpret_cast<const uint8_t*>(kPayload),
                              sizeof(kPayload), kPwStatusOk, &encoded_bytes,
                              &encoded_byte_len),
            kPwStatusOk);
  // Fire several next packets.
  for (int32_t i = 0; i < 3; i++) {
    ASSERT_EQ(PwRpcClientInvokeStreamingNext(
                  &client, call_id, (const uint8_t*)kPayload, sizeof(kPayload)),
              kPwStatusOk);
    PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                      payload_buf, sizeof(payload_buf), &packet, &payload_size);
    EXPECT_EQ(test_transport_info.tx_bytes, encoded_byte_len);
    EXPECT_EQ(packet.type, pw_rpc_internal_PacketType_CLIENT_STREAM);
    EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
    EXPECT_EQ(packet.service_id, kServiceId);
    EXPECT_EQ(packet.method_id, kMethodId);
    EXPECT_EQ(packet.status, kPwStatusOk);
    EXPECT_EQ(packet.call_id, 0U);
    EXPECT_EQ(memcmp(payload_buf, kPayload, payload_size), 0);
  }
  free(encoded_bytes);

  // Encode a complete packet.
  ASSERT_EQ(
      PwRpcPacketEncode(pw_rpc_internal_PacketType_CLIENT_REQUEST_COMPLETION, 0,
                        kRpcChannelIdTest, kServiceId, kMethodId, nullptr, 0,
                        kPwStatusOk, &encoded_bytes, &encoded_byte_len),
      kPwStatusOk);
  ASSERT_EQ(PwRpcClientInvokeStreamingComplete(&client, call_id), kPwStatusOk);
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &packet, &payload_size);
  EXPECT_EQ(test_transport_info.tx_bytes, encoded_byte_len);
  EXPECT_EQ(packet.type, pw_rpc_internal_PacketType_CLIENT_REQUEST_COMPLETION);
  EXPECT_EQ(packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(packet.service_id, kServiceId);
  EXPECT_EQ(packet.method_id, kMethodId);
  EXPECT_EQ(packet.status, kPwStatusOk);
  EXPECT_EQ(packet.call_id, 0U);
  free(encoded_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, InvokeUnaryPb) {
  TestTransport test_transport_info;
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  pw_rpc_internal_RpcPacket rpc_packet;
  rpc_packet.type = pw_rpc_internal_PacketType_REQUEST;
  rpc_packet.channel_id = kRpcChannelIdTest;
  rpc_packet.service_id = kServiceId;
  rpc_packet.method_id = kMethodId;

  ASSERT_EQ(
      PwRpcClientInvokeUnaryPb(&client, kServiceId, kMethodId,
                               pw_rpc_internal_RpcPacket_fields, &rpc_packet,
                               OnComplete, OnError, nullptr, nullptr),
      kPwStatusOk);

  pw_rpc_internal_RpcPacket decoded_packet;
  uint8_t payload_buf[16] = {0};
  size_t payload_size = 0;
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &decoded_packet,
                    &payload_size);
  EXPECT_EQ(decoded_packet.type, pw_rpc_internal_PacketType_REQUEST);
  EXPECT_EQ(decoded_packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(decoded_packet.service_id, kServiceId);
  EXPECT_EQ(decoded_packet.method_id, kMethodId);
  EXPECT_EQ(decoded_packet.status, kPwStatusOk);
  EXPECT_EQ(decoded_packet.call_id, 0U);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClient, InvokeStreamingPb) {
  TestTransport test_transport_info;
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  pw_rpc_internal_RpcPacket rpc_packet;
  rpc_packet.type = pw_rpc_internal_PacketType_REQUEST;
  rpc_packet.channel_id = kRpcChannelIdTest;
  rpc_packet.service_id = kServiceId;
  rpc_packet.method_id = kMethodId;

  ASSERT_EQ(PwRpcClientInvokeStreamingPb(&client, kServiceId, kMethodId,
                                         pw_rpc_internal_RpcPacket_fields,
                                         &rpc_packet, OnNext, OnComplete,
                                         OnError, nullptr, nullptr),
            kPwStatusOk);

  pw_rpc_internal_RpcPacket decoded_packet;
  uint8_t payload_buf[16] = {0};
  size_t payload_size = 0;
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &decoded_packet,
                    &payload_size);
  EXPECT_EQ(decoded_packet.type, pw_rpc_internal_PacketType_REQUEST);
  EXPECT_EQ(decoded_packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(decoded_packet.service_id, kServiceId);
  EXPECT_EQ(decoded_packet.method_id, kMethodId);
  EXPECT_EQ(decoded_packet.status, kPwStatusOk);
  EXPECT_EQ(decoded_packet.call_id, 0U);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClient, InvokeClientStreamingPb) {
  TestTransport test_transport_info;
  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  pw_rpc_internal_RpcPacket rpc_packet;
  rpc_packet.type = pw_rpc_internal_PacketType_REQUEST;
  rpc_packet.channel_id = kRpcChannelIdTest;
  rpc_packet.service_id = kServiceId;
  rpc_packet.method_id = kMethodId;

  uint32_t call_id = 0;
  ASSERT_EQ(PwRpcClientInvokeStreamingPb(&client, kServiceId, kMethodId,
                                         pw_rpc_internal_RpcPacket_fields,
                                         &rpc_packet, OnNext, OnComplete,
                                         OnError, nullptr, &call_id),
            kPwStatusOk);

  pw_rpc_internal_RpcPacket decoded_packet;
  uint8_t payload_buf[16] = {0};
  size_t payload_size = 0;
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &decoded_packet,
                    &payload_size);
  EXPECT_EQ(decoded_packet.type, pw_rpc_internal_PacketType_REQUEST);
  EXPECT_EQ(decoded_packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(decoded_packet.service_id, kServiceId);
  EXPECT_EQ(decoded_packet.method_id, kMethodId);
  EXPECT_EQ(decoded_packet.status, kPwStatusOk);
  EXPECT_EQ(decoded_packet.call_id, 0U);

  // Fire several next packets.
  for (int32_t i = 0; i < 3; i++) {
    ASSERT_EQ(
        PwRpcClientInvokeStreamingNextPb(
            &client, call_id, pw_rpc_internal_RpcPacket_fields, &rpc_packet),
        kPwStatusOk);
    PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                      payload_buf, sizeof(payload_buf), &decoded_packet,
                      &payload_size);
    EXPECT_EQ(decoded_packet.type, pw_rpc_internal_PacketType_CLIENT_STREAM);
    EXPECT_EQ(decoded_packet.channel_id, kRpcChannelIdTest);
    EXPECT_EQ(decoded_packet.service_id, kServiceId);
    EXPECT_EQ(decoded_packet.method_id, kMethodId);
    EXPECT_EQ(decoded_packet.status, kPwStatusOk);
    EXPECT_EQ(decoded_packet.call_id, 0U);
  }

  // Encode a complete packet.
  ASSERT_EQ(PwRpcClientInvokeStreamingComplete(&client, call_id), kPwStatusOk);
  PwRpcPacketDecode(test_transport_info.tx_buf, test_transport_info.tx_bytes,
                    payload_buf, sizeof(payload_buf), &decoded_packet,
                    &payload_size);
  EXPECT_EQ(decoded_packet.type,
            pw_rpc_internal_PacketType_CLIENT_REQUEST_COMPLETION);
  EXPECT_EQ(decoded_packet.channel_id, kRpcChannelIdTest);
  EXPECT_EQ(decoded_packet.service_id, kServiceId);
  EXPECT_EQ(decoded_packet.method_id, kMethodId);
  EXPECT_EQ(decoded_packet.status, kPwStatusOk);
  EXPECT_EQ(decoded_packet.call_id, 0U);

  PwRpcClientDeinit(&client);
}

void PrepareCall(PwRpcClient* client,
                 pw_rpc_internal_PacketType* response_type) {
  // This will be freed in PwRpcClientProcessPacket().
  PwRpcCall* call = (PwRpcCall*)malloc(sizeof(PwRpcCall));
  call->client = client;
  call->type = kPwRpcClientCall;
  call->channel_id = kRpcChannelIdTest;
  call->service_id = kServiceId;
  call->method_id = kMethodId;
  call->on_next = OnNext;
  call->on_complete = OnComplete;
  call->on_error = OnError;
  call->context = (void*)response_type;
  call->state = kPwRpcCallAwaitingResponse;
  call->call_id = kTestCallId;
  ASSERT_EQ(PwRpcCallListPut(&client->call_list, call), kPwStatusOk);
}

TEST(PwRpcClientTest, ProcessResponsePacket) {
  TestTransport test_transport_info;
  uint8_t* encoded_response_bytes;
  size_t encoded_bytes = 0;
  ASSERT_EQ(PwRpcPacketEncode(pw_rpc_internal_PacketType_RESPONSE, kTestCallId,
                              kRpcChannelIdTest, kServiceId, kMethodId,
                              reinterpret_cast<const uint8_t*>(kPayload),
                              sizeof(kPayload), kPwStatusOk,
                              &encoded_response_bytes, &encoded_bytes),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  pw_rpc_internal_PacketType response_type = pw_rpc_internal_PacketType_REQUEST;
  PrepareCall(&client, &response_type);

  uint8_t decode_buf[128];
  EXPECT_EQ(
      PwRpcClientProcessPacket(&client, encoded_response_bytes, encoded_bytes,
                               decode_buf, sizeof(decode_buf)),
      kPwStatusOk);
  EXPECT_EQ(response_type, pw_rpc_internal_PacketType_RESPONSE);
  free(encoded_response_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, ProcessStreamingPacket) {
  TestTransport test_transport_info;
  uint8_t* encoded_response_bytes;
  size_t encoded_bytes = 0;
  ASSERT_EQ(PwRpcPacketEncode(
                pw_rpc_internal_PacketType_SERVER_STREAM, kTestCallId,
                kRpcChannelIdTest, kServiceId, kMethodId,
                reinterpret_cast<const uint8_t*>(kPayload), sizeof(kPayload),
                kPwStatusOk, &encoded_response_bytes, &encoded_bytes),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  pw_rpc_internal_PacketType response_type = pw_rpc_internal_PacketType_REQUEST;
  PrepareCall(&client, &response_type);

  uint8_t decode_buf[128];
  EXPECT_EQ(
      PwRpcClientProcessPacket(&client, encoded_response_bytes, encoded_bytes,
                               decode_buf, sizeof(decode_buf)),
      kPwStatusOk);
  EXPECT_EQ(response_type, pw_rpc_internal_PacketType_SERVER_STREAM);
  free(encoded_response_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, ProcessErrorPacket) {
  TestTransport test_transport_info;
  uint8_t* encoded_response_bytes;
  size_t encoded_bytes = 0;
  ASSERT_EQ(PwRpcPacketEncode(
                pw_rpc_internal_PacketType_SERVER_ERROR, kTestCallId,
                kRpcChannelIdTest, kServiceId, kMethodId,
                reinterpret_cast<const uint8_t*>(kPayload), sizeof(kPayload),
                kPwStatusUnknown, &encoded_response_bytes, &encoded_bytes),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  pw_rpc_internal_PacketType response_type = pw_rpc_internal_PacketType_REQUEST;
  PrepareCall(&client, &response_type);

  uint8_t decode_buf[128];
  EXPECT_EQ(
      PwRpcClientProcessPacket(&client, encoded_response_bytes, encoded_bytes,
                               decode_buf, sizeof(decode_buf)),
      kPwStatusOk);
  EXPECT_EQ(response_type, pw_rpc_internal_PacketType_SERVER_ERROR);
  free(encoded_response_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, ProcessResponsePacketWithoutCompleteCallback) {
  TestTransport test_transport_info;
  uint8_t* encoded_response_bytes;
  size_t encoded_bytes = 0;
  ASSERT_EQ(PwRpcPacketEncode(pw_rpc_internal_PacketType_RESPONSE, kTestCallId,
                              kRpcChannelIdTest, kServiceId, kMethodId,
                              reinterpret_cast<const uint8_t*>(kPayload),
                              sizeof(kPayload), kPwStatusOk,
                              &encoded_response_bytes, &encoded_bytes),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  PwRpcCall* call = (PwRpcCall*)malloc(sizeof(PwRpcCall));
  call->client = &client;
  call->type = kPwRpcClientCall;
  call->on_complete = nullptr;
  call->on_error = OnError;
  call->call_id = kTestCallId;
  ASSERT_EQ(PwRpcCallListPut(&client.call_list, call), kPwStatusOk);

  uint8_t decode_buf[128];
  EXPECT_EQ(
      PwRpcClientProcessPacket(&client, encoded_response_bytes, encoded_bytes,
                               decode_buf, sizeof(decode_buf)),
      kPwStatusOk);
  free(encoded_response_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, ProcessResponsePacketWithoutErrorCallback) {
  TestTransport test_transport_info;
  uint8_t* encoded_response_bytes;
  size_t encoded_bytes = 0;
  ASSERT_EQ(PwRpcPacketEncode(
                pw_rpc_internal_PacketType_SERVER_ERROR, kTestCallId,
                kRpcChannelIdTest, kServiceId, kMethodId,
                reinterpret_cast<const uint8_t*>(kPayload), sizeof(kPayload),
                kPwStatusOk, &encoded_response_bytes, &encoded_bytes),
            kPwStatusOk);

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  PwRpcCall* call = (PwRpcCall*)malloc(sizeof(PwRpcCall));
  call->client = &client;
  call->type = kPwRpcClientCall;
  call->on_complete = OnComplete;
  call->on_error = nullptr;
  call->call_id = kTestCallId;
  ASSERT_EQ(PwRpcCallListPut(&client.call_list, call), kPwStatusOk);

  uint8_t decode_buf[128];
  EXPECT_EQ(
      PwRpcClientProcessPacket(&client, encoded_response_bytes, encoded_bytes,
                               decode_buf, sizeof(decode_buf)),
      kPwStatusOk);
  free(encoded_response_bytes);

  PwRpcClientDeinit(&client);
}

TEST(PwRpcClientTest, DropAwaitingCall) {
  TestTransport test_transport_info;

  PwRpcClient client;
  ASSERT_EQ(
      PwRpcClientInit(&client, kRpcChannelIdTest, TestRpcChannelOutputHandler,
                      TestRpcChannelInputWorker, (void*)&test_transport_info),
      kPwStatusOk);

  pw_rpc_internal_PacketType response_type = pw_rpc_internal_PacketType_REQUEST;
  PrepareCall(&client, &response_type);

  // Drop non-existent call.
  EXPECT_EQ(PwRpcClientDropCall(&client, kTestCallId + 1), kPwStatusNotFound);
  // Drop existent call.
  EXPECT_EQ(PwRpcClientDropCall(&client, kTestCallId), kPwStatusOk);

  PwRpcClientDeinit(&client);
}
}  // namespace
}  // namespace noa::module::linux_rpc_client::pw_rpc_c
