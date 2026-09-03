#include <cerrno>
#include <cstring>

#include "common/compiler.h"
#include "gtest/gtest.h"
#include "noa_ipc/ipc_manager.h"

namespace noa::module::linux_rpc_client::noa_ipc {
namespace {

using noa::module::ipc::kChannelEndpointApc;
using noa::module::ipc::kChannelEndpointNcp;
using noa::module::ipc::kChannelTypeSram;

#define SAMPLE_SERVICE_NAME "service"

static constexpr uint8_t kTxChannelId = 4;
static constexpr uint8_t kRxChannelId = 7;
static constexpr uint8_t kNonExistChannelId = 8;
static constexpr uint32_t kBufferSize = 64;
static ATTR_ALIGNED(4) uint8_t tx_channel_buf[128] = {0};
static ATTR_ALIGNED(4) uint8_t rx_channel_buf[128] = {0};

static IpcChannelInfo channels[2] = {
    IPC_CHANNEL_INFO_MEMORY(kTxChannelId, kBufferSize, kChannelEndpointNcp,
                            kChannelEndpointApc, kChannelTypeSram,
                            (uint32_t)tx_channel_buf),
    IPC_CHANNEL_INFO_MEMORY(kRxChannelId, kBufferSize, kChannelEndpointApc,
                            kChannelEndpointNcp, kChannelTypeSram,
                            (uint32_t)rx_channel_buf)};

TEST(IpcMgrTest, LeastInfoSunnyCase) {
  IpcInfo info_minimal = NOA_IPC_INFO(0, 0);

  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, &info_minimal, nullptr),
            0);
  ASSERT_EQ(IpcMgrDeinit(&context), 0);
}

TEST(IpcMgrTest, FailureDueToNullInfo) {
  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, nullptr, nullptr),
            -EINVAL);
}

TEST(IpcMgrTest, FailureDueToNullChannelInfo) {
  IpcInfo info_minimal = NOA_IPC_INFO(0, 0);

  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  info_minimal.channel_num = 1;
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, &info_minimal, nullptr),
            -EINVAL);
}

TEST(IpcMgrTest, FailureDueToInvalidMagic) {
  IpcInfo info_minimal = NOA_IPC_INFO(0, 0);

  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  info_minimal.magic = 0x11;
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, &info_minimal, nullptr),
            -ENOENT);
}

TEST(IpcMgrTest, FailureDueToInvalidVersion) {
  IpcInfo info_minimal = NOA_IPC_INFO(0, 0);

  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  info_minimal.version = kIpcVersion + 1;
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, &info_minimal, nullptr),
            -EPROTO);
}

TEST(IpcMgrTest, RequestChannelWithInvalidChannelId) {
  IpcInfo info_minimal = NOA_IPC_INFO(0, 0);

  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, &info_minimal, nullptr),
            0);
  ASSERT_EQ(IpcMgrGetChannel(&context, kIpcInvalidChannelId), nullptr);
  ASSERT_EQ(IpcMgrDeinit(&context), 0);
}

TEST(IpcMgrTest, RequestChannelWithNonExistChannelId) {
  IpcInfo info_minimal = NOA_IPC_INFO(0, 0);

  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, &info_minimal, nullptr),
            0);
  ASSERT_EQ(IpcMgrGetChannel(&context, kNonExistChannelId), nullptr);
  ASSERT_EQ(IpcMgrDeinit(&context), 0);
}

TEST(IpcMgrTest, SunnyCaseWithChannels) {
  IpcInfo info_minimal = NOA_IPC_INFO(reinterpret_cast<uint32_t>(channels), 2);

  IpcManagerData context;
  memset(&context, 0, sizeof(IpcManagerData));
  ASSERT_EQ(IpcMgrInit(&context, kChannelEndpointNcp, &info_minimal, nullptr),
            0);
  IpcChannelData* ch = IpcMgrGetChannel(&context, kTxChannelId);
  ASSERT_NE(ch, nullptr);
  ASSERT_EQ(IpcChGetId(ch), kTxChannelId);
  ASSERT_EQ(IpcMgrDeinit(&context), 0);
}

}  // namespace
}  // namespace noa::module::linux_rpc_client::noa_ipc
