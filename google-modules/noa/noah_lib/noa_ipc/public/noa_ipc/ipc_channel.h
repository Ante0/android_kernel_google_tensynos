#ifndef NOA_IPC_CHANNEL_H
#define NOA_IPC_CHANNEL_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#include "noa_ipc/ipc.h"
#include "noa_ipc/ipc_buffer.h"

#ifdef __cplusplus
extern "C" {
using noa::module::ipc::ChannelEndpoint;
using noa::module::ipc::IpcChannelInfo;
#endif

struct IpcChannelData {
  bool is_ready;
  enum ChannelEndpoint endpoint;
  struct IpcChannelInfo* info;
  void* notifier;
  struct IpcBufferData buffer_data;
  void* context;
  // For memory based buffer.
  void* buffer_os_addr;
};

int32_t IpcChInit(struct IpcChannelData* data,
                  enum ChannelEndpoint this_endpoint,
                  struct IpcChannelInfo* info, void* context);
void IpcChDeinit(struct IpcChannelData* data);
bool IpcChIsReady(struct IpcChannelData* data);
uint8_t IpcChGetId(struct IpcChannelData* data);
int32_t IpcChWrite(struct IpcChannelData* data, const char* buf, uint32_t len);
int32_t IpcChRead(struct IpcChannelData* data, char* buf, uint32_t buf_len,
                  uint32_t* read_bytes);

#ifdef __cplusplus
}
#endif
#endif /* NOA_IPC_CHANNEL_H */
