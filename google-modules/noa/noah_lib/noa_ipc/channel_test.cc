#include <cerrno>
#include <cstring>

#include "common/compiler.h"
#include "gtest/gtest.h"
#include "noa_ipc/ipc_channel.h"

namespace noa::module::linux_rpc_client::noa_ipc {

namespace {

using noa::module::ipc::IpcChannelInfo;
using noa::module::ipc::kChannelEndpointApc;
using noa::module::ipc::kChannelEndpointNcp;
using noa::module::ipc::kChannelEndpointNep;
using noa::module::ipc::kChannelTypeCount;
using noa::module::ipc::kChannelTypeSram;

static constexpr ChannelEndpoint kSource = kChannelEndpointNcp;
static constexpr ChannelEndpoint kSink = kChannelEndpointApc;
static constexpr ChannelEndpoint kIrrelevantEndpoint = kChannelEndpointNep;
static constexpr uint8_t kChannelTestId = 4;

TEST(IpcChTest, SunnyCaseSharedMemory) {
  ATTR_ALIGNED(4) uint8_t test_channel_buf[128] = {0};
  IpcChannelInfo test_memory_channel_info =
      IPC_CHANNEL_INFO_MEMORY(kChannelTestId, 128, kSource, kSink,
                              kChannelTypeSram, (uint32_t)test_channel_buf);
  IpcChannelData context;
  memset(&context, 0, sizeof(IpcChannelData));

  ASSERT_EQ(IpcChInit(&context, kSource, &test_memory_channel_info, nullptr),
            0);

  ASSERT_TRUE(IpcChIsReady(&context));

  IpcChDeinit(&context);
}

TEST(IpcChTest, FailureNullInfo) {
  IpcChannelData context;
  memset(&context, 0, sizeof(IpcChannelData));
  ASSERT_EQ(IpcChInit(&context, kSource, nullptr, nullptr), -EINVAL);
  ASSERT_FALSE(IpcChIsReady(&context));

  char buf[32] = {0};
  ASSERT_EQ(IpcChWrite(&context, buf, sizeof(buf)), -EBUSY);
  uint32_t read_bytes = 0;
  ASSERT_EQ(IpcChRead(&context, buf, sizeof(buf), &read_bytes), -EBUSY);
}

TEST(IpcChTest, FailureDueToIrrelevantEndpoint) {
  IpcChannelData context;
  memset(&context, 0, sizeof(IpcChannelData));
  ATTR_ALIGNED(4) uint8_t test_channel_buf[128] = {0};
  IpcChannelInfo test_memory_channel_info =
      IPC_CHANNEL_INFO_MEMORY(kChannelTestId, 128, kSource, kSink,
                              kChannelTypeSram, (uint32_t)test_channel_buf);
  ASSERT_EQ(IpcChInit(&context, kIrrelevantEndpoint, &test_memory_channel_info,
                      nullptr),
            -EPERM);
  ASSERT_FALSE(IpcChIsReady(&context));
}

TEST(IpcChTest, FailureNoBuffer) {
  IpcChannelData context;
  memset(&context, 0, sizeof(IpcChannelData));
  ATTR_ALIGNED(4) uint8_t test_channel_buf[128] = {0};
  IpcChannelInfo test_memory_channel_info =
      IPC_CHANNEL_INFO_MEMORY(kChannelTestId, 128, kSource, kSink,
                              kChannelTypeSram, (uint32_t)test_channel_buf);
  test_memory_channel_info.config.type = kChannelTypeCount;
  ASSERT_EQ(IpcChInit(&context, kSource, &test_memory_channel_info, nullptr),
            -EINVAL);
  ASSERT_FALSE(IpcChIsReady(&context));

  // Still blocked by IsReady().
  char buf[32] = {0};
  ASSERT_EQ(IpcChWrite(&context, buf, sizeof(buf)), -EBUSY);
  uint32_t read_bytes = 0;
  ASSERT_EQ(IpcChRead(&context, buf, sizeof(buf), &read_bytes), -EBUSY);
}

TEST(IpcChTest, NoReaderForSource) {
  IpcChannelData context;
  memset(&context, 0, sizeof(IpcChannelData));
  ATTR_ALIGNED(4) uint8_t test_channel_buf[128] = {0};
  IpcChannelInfo test_memory_channel_info =
      IPC_CHANNEL_INFO_MEMORY(kChannelTestId, 128, kSource, kSink,
                              kChannelTypeSram, (uint32_t)test_channel_buf);

  ASSERT_EQ(IpcChInit(&context, kSource, &test_memory_channel_info, nullptr),
            0);
  ASSERT_TRUE(IpcChIsReady(&context));

  char buf[32] = {0};
  uint32_t read_bytes = 0;
  ASSERT_EQ(IpcChRead(&context, buf, sizeof(buf), &read_bytes), -ENXIO);
  IpcChDeinit(&context);
}

TEST(IpcChTest, NoWriterForSink) {
  IpcChannelData context;
  memset(&context, 0, sizeof(IpcChannelData));
  ATTR_ALIGNED(4) uint8_t test_channel_buf[128] = {0};
  IpcChannelInfo test_memory_channel_info =
      IPC_CHANNEL_INFO_MEMORY(kChannelTestId, 128, kSource, kSink,
                              kChannelTypeSram, (uint32_t)test_channel_buf);

  ASSERT_EQ(IpcChInit(&context, kSink, &test_memory_channel_info, nullptr), 0);
  // Only source side can mark Ready after initializing the buffer.
  ASSERT_FALSE(IpcChIsReady(&context));

  // Simulate remote source initialize the channel.
  IpcChannelData source_context;
  memset(&source_context, 0, sizeof(IpcChannelData));
  ASSERT_EQ(
      IpcChInit(&source_context, kSource, &test_memory_channel_info, nullptr),
      0);

  ASSERT_TRUE(IpcChIsReady(&source_context));

  char buf[32] = {0};
  ASSERT_EQ(IpcChWrite(&context, buf, sizeof(buf)), -ENXIO);

  IpcChDeinit(&context);
  IpcChDeinit(&source_context);
}

}  // namespace
}  // namespace noa::module::linux_rpc_client::noa_ipc
