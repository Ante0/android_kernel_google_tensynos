#ifndef NOA_IPC_MANAGER_H
#define NOA_IPC_MANAGER_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <inttypes.h>
#include <stdint.h>
#endif

#include "noa_ipc/ipc.h"
#include "noa_ipc/ipc_channel.h"

#ifdef __cplusplus
extern "C" {
using noa::module::ipc::IpcChannelInfo;
using noa::module::ipc::IpcInfo;
#endif

struct IpcManagerData {
  enum ChannelEndpoint endpoint;
  struct IpcInfo* info;
  struct IpcChannelInfo* channel_info;
  struct IpcChannelData* channel_data;
  void* context;
};

int32_t IpcMgrInit(struct IpcManagerData* data,
                   enum ChannelEndpoint this_endpoint, struct IpcInfo* info,
                   void* context);
int32_t IpcMgrDeinit(struct IpcManagerData* data);
bool IpcMgrIsReady(struct IpcManagerData* data);
struct IpcChannelData* IpcMgrGetChannel(struct IpcManagerData* data,
                                        uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* NOA_IPC_MANAGER_H */
