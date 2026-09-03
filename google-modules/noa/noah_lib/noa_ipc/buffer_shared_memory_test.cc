#include <errno.h>
#include <cstdint>

#include "noa_ipc/ipc_buffer_shared_memory.h"

#include "gtest/gtest.h"

namespace noa::module::linux_rpc_client::noa_ipc {

namespace {
TEST(BufferSharedMemory, InitSunnyCase) {
  IpcBufferData data;
  char buf[256];
  // source with raw mode
  EXPECT_EQ(IpcBufShmInit(&data, (void*)buf, sizeof(buf), false, nullptr, true),
            0);
  EXPECT_NE(data.context, nullptr);
  EXPECT_EQ(IpcBufShmDeinit(&data), 0);
  EXPECT_EQ(data.context, nullptr);
  // source with preamble mode
  EXPECT_EQ(IpcBufShmInit(&data, (void*)buf, sizeof(buf), true, nullptr, true),
            0);
  EXPECT_NE(data.context, nullptr);
  EXPECT_EQ(IpcBufShmDeinit(&data), 0);
  EXPECT_EQ(data.context, nullptr);
  // sink with raw mode
  EXPECT_EQ(
      IpcBufShmInit(&data, (void*)buf, sizeof(buf), false, nullptr, false), 0);
  EXPECT_NE(data.context, nullptr);
  EXPECT_EQ(IpcBufShmDeinit(&data), 0);
  EXPECT_EQ(data.context, nullptr);
  // sink with preamble mode
  EXPECT_EQ(IpcBufShmInit(&data, (void*)buf, sizeof(buf), true, nullptr, false),
            0);
  EXPECT_NE(data.context, nullptr);
  EXPECT_EQ(IpcBufShmDeinit(&data), 0);
  EXPECT_EQ(data.context, nullptr);
}

TEST(BufferSharedMemory, FailureInitNoData) {
  char buf[256];
  bool preamble_mode = false;
  EXPECT_EQ(IpcBufShmInit(nullptr, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, true),
            -EINVAL);
}

TEST(BufferSharedMemory, FailureInitBufferTooSmall) {
  IpcBufferData data;
  bool preamble_mode = false;
  char buf[sizeof(BufferInfo) - 1];
  EXPECT_EQ(IpcBufShmInit(&data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, true),
            -EINVAL);
}

TEST(BufferSharedMemory, FailureDeinitNoData) {
  EXPECT_EQ(IpcBufShmDeinit(nullptr), -EINVAL);
}

TEST(BufferSharedMemory, FailureDeinitNoShmData) {
  IpcBufferData data;
  data.context = nullptr;
  EXPECT_EQ(IpcBufShmDeinit(&data), -EINVAL);
}

TEST(BufferSharedMemory, WriterAndReaderForSource) {
  IpcBufferData source_data;
  bool preamble_mode = false;
  char buf[256];
  // source with raw mode
  EXPECT_EQ(IpcBufShmInit(&source_data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, true),
            0);
  EXPECT_NE(source_data.context, nullptr);

  EXPECT_TRUE(IpcBufGetWriter(&source_data));
  EXPECT_TRUE(IpcBufPutWriter(&source_data));
  EXPECT_FALSE(IpcBufGetReader(&source_data));
  EXPECT_FALSE(IpcBufPutReader(&source_data));
  EXPECT_EQ(IpcBufShmDeinit(&source_data), 0);
  EXPECT_EQ(source_data.context, nullptr);
}

TEST(BufferSharedMemory, ReaderAndReaderForSink) {
  IpcBufferData sink_data;
  bool preamble_mode = false;
  char buf[256];
  // source with raw mode
  EXPECT_EQ(IpcBufShmInit(&sink_data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, false),
            0);
  EXPECT_NE(sink_data.context, nullptr);

  EXPECT_FALSE(IpcBufGetWriter(&sink_data));
  EXPECT_FALSE(IpcBufPutWriter(&sink_data));
  EXPECT_TRUE(IpcBufGetReader(&sink_data));
  EXPECT_TRUE(IpcBufPutReader(&sink_data));
  EXPECT_EQ(IpcBufShmDeinit(&sink_data), 0);
  EXPECT_EQ(sink_data.context, nullptr);
}

void VerifySimplyWriteAndRead(bool preamble_mode) {
  char send_msg[] = "hello world 1234";

  IpcBufferData tx_data, rx_data;
  char buf[256];
  char read_buf[32];
  EXPECT_EQ(IpcBufShmInit(&tx_data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, true),
            0);
  EXPECT_EQ(IpcBufShmInit(&rx_data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, false),
            0);

  EXPECT_EQ(IpcBufWrite(&tx_data, send_msg, sizeof(send_msg)), 0);
  uint32_t read_bytes;
  EXPECT_EQ(IpcBufRead(&rx_data, read_buf, sizeof(read_buf), &read_bytes), 0);

  EXPECT_EQ(read_bytes, strlen(send_msg) + 1);
  EXPECT_EQ(strcmp(send_msg, read_buf), 0);

  EXPECT_EQ(IpcBufShmDeinit(&tx_data), 0);
  EXPECT_EQ(IpcBufShmDeinit(&rx_data), 0);
}

TEST(BufferSharedMemory, RawModeWriteAndRead) {
  VerifySimplyWriteAndRead(false);
}

TEST(BufferSharedMemory, PreambleModeWriteAndRead) {
  VerifySimplyWriteAndRead(true);
}

TEST(BufferSharedMemory, TryWriteWithoutSpace) {
  bool preamble_mode = false;
  char send_msg[] = "a";

  IpcBufferData tx_data;
  char buf[sizeof(BufferInfo)];
  EXPECT_EQ(IpcBufShmInit(&tx_data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, true),
            0);

  EXPECT_EQ(IpcBufTryWrite(&tx_data, send_msg, 1), -EAGAIN);

  EXPECT_EQ(IpcBufShmDeinit(&tx_data), 0);
}

TEST(BufferSharedMemory, TryReadWithoutData) {
  bool preamble_mode = false;
  char buf[256];
  char read_buf[32];

  IpcBufferData tx_data, rx_data;
  EXPECT_EQ(IpcBufShmInit(&tx_data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, true),
            0);
  EXPECT_EQ(IpcBufShmInit(&rx_data, (void*)buf, sizeof(buf), preamble_mode,
                          nullptr, false),
            0);

  uint32_t read_bytes;
  EXPECT_EQ(IpcBufTryRead(&rx_data, read_buf, sizeof(read_buf), &read_bytes),
            -ENODATA);

  EXPECT_EQ(IpcBufShmDeinit(&tx_data), 0);
  EXPECT_EQ(IpcBufShmDeinit(&rx_data), 0);
}
}  // namespace
}  // namespace noa::module::linux_rpc_client::noa_ipc
