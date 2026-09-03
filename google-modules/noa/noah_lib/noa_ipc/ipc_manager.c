#define PW_LOG_MODULE_NAME "ipc"
#include "noa_ipc/ipc_manager.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "noa_ipc/noa_ipc_os.h"

bool IpcMgrIsReady(struct IpcManagerData* data) {
  if (!data) {
    return false;
  }
  return !!(data->info);
}

int32_t IpcMgrInit(struct IpcManagerData* data,
                   enum ChannelEndpoint this_endpoint, struct IpcInfo* info,
                   void* context) {
  IpcMgrDeinit(data);
  if (!data) {
    return -EINVAL;
  }
  if (!info) {
    return -EINVAL;
  }
  if (info->magic != kIpcInfoMagic) {
    return -ENOENT;
  }
  if (info->version != kIpcVersion) {
    return -EPROTO;
  }

  data->endpoint = this_endpoint;
  data->info = info;
  data->context = context;

  struct IpcChannelInfo* channel_info = (struct IpcChannelInfo*)OsIoRemap(
      info->channel_info_addr,
      sizeof(struct IpcChannelInfo) * info->channel_num, data->context);
  if (info->channel_num && !channel_info) {
    return -EINVAL;
  }

  data->channel_info = channel_info;
  data->channel_data = (struct IpcChannelData*)NoaIpcAllocate(
      data->info->channel_num * sizeof(struct IpcChannelData));
  memset(data->channel_data, 0,
         data->info->channel_num * sizeof(struct IpcChannelData));

  for (uint8_t i = 0; i < data->info->channel_num; i++) {
    int32_t ret = IpcChInit(&data->channel_data[i], data->endpoint,
                            &data->channel_info[i], context);
    if (ret == -EPERM) {
      continue;
    }
    if (ret) {
      IpcMgrDeinit(data);
      return -EINVAL;
    }
  }
  return 0;
}

int32_t IpcMgrDeinit(struct IpcManagerData* data) {
  if (!data) {
    return -EINVAL;
  }
  if (data->channel_data) {
    for (int32_t i = 0; i < data->info->channel_num; i++) {
      IpcChDeinit(&data->channel_data[i]);
    }
    NoaIpcFree(data->channel_data);
  }
  data->channel_data = NULL;
  OsIoUnmap(data->channel_info, data->context);
  return 0;
}

struct IpcChannelData* IpcMgrGetChannel(struct IpcManagerData* data,
                                        uint8_t id) {
  if (!data) {
    return NULL;
  }
  if (id == kIpcInvalidChannelId) {
    return NULL;
  }

  for (int32_t i = 0; i < data->info->channel_num; i++) {
    if (IpcChGetId(&data->channel_data[i]) == id) {
      return &data->channel_data[i];
    }
  }
  return NULL;
}
