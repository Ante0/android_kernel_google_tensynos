#include <errno.h>
#include <cstdint>

#include "ipc/ipc_buffer_shared_memory.h"

#include "noa_ipc/ipc_buffer_shared_memory.h"

#include "gtest/gtest.h"

namespace noa::module::linux_rpc_client::noa_ipc {
namespace {
using noa::module::ipc::IpcBuffer;
using noa::module::ipc::IpcBufferSharedMemory;

enum Direction {
  kLinuxToNoa,
  kNoaToLinux,
};

void VerifySimpleMixedWriteAndRead(bool preamble_mode, enum Direction dir) {
  char send_msg[] = "HelloWorld";

  char buf[128];
  char read_buf[32];

  int32_t ret = 0;

  // Linux
  IpcBufferData linux_ipc_buf_context;
  // NoA
  IpcBufferSharedMemory noa_ipc_shm;

  // Linux
  ret = IpcBufShmInit(&linux_ipc_buf_context, (void*)buf, sizeof(buf),
                      preamble_mode, nullptr, dir == kLinuxToNoa);
  EXPECT_EQ(ret, 0);

  // NoA
  ret = noa_ipc_shm.Init((void*)buf, sizeof(buf), preamble_mode, nullptr,
                         dir == kNoaToLinux);
  EXPECT_EQ(ret, 0);

  uint32_t read_bytes;
  if (dir == kLinuxToNoa) {
    ret = IpcBufWrite(&linux_ipc_buf_context, send_msg, sizeof(send_msg));
    EXPECT_EQ(ret, 0);

    IpcBuffer::Reader* reader;
    reader = noa_ipc_shm.GetReader();
    ASSERT_NE(reader, nullptr);
    ret = reader->Read(read_buf, sizeof(read_buf), &read_bytes);
    EXPECT_EQ(ret, 0);

    noa_ipc_shm.PutReader(reader);
  } else if (dir == kNoaToLinux) {
    IpcBuffer::Writer* writer;
    writer = noa_ipc_shm.GetWriter();
    ASSERT_NE(writer, nullptr);

    ret = writer->Write(send_msg, sizeof(send_msg));
    EXPECT_EQ(ret, 0);

    ret = IpcBufRead(&linux_ipc_buf_context, read_buf, sizeof(read_buf),
                     &read_bytes);
    EXPECT_EQ(ret, 0);

    noa_ipc_shm.PutWriter(writer);
  }

  EXPECT_EQ(read_bytes, strlen(send_msg) + 1);
  EXPECT_EQ(strcmp(send_msg, read_buf), 0);

  IpcBufShmDeinit(&linux_ipc_buf_context);
  noa_ipc_shm.Deinit();
}

TEST(MixedBufferSharedMemory, RawModeLinuxWriteNoaRead) {
  bool preamble_mode = false;
  VerifySimpleMixedWriteAndRead(preamble_mode, kLinuxToNoa);
}

TEST(MixedBufferSharedMemory, RawModeLinuxReadNoaWrite) {
  bool preamble_mode = false;
  VerifySimpleMixedWriteAndRead(preamble_mode, kNoaToLinux);
}

TEST(MixedBufferSharedMemory, NonRawModeLinuxWriteNoaRead) {
  bool preamble_mode = true;
  VerifySimpleMixedWriteAndRead(preamble_mode, kLinuxToNoa);
}

TEST(MixedBufferSharedMemory, NonRawModeLinuxReadNoaWrite) {
  bool preamble_mode = true;
  VerifySimpleMixedWriteAndRead(preamble_mode, kNoaToLinux);
}

}  // namespace
}  // namespace noa::module::linux_rpc_client::noa_ipc
