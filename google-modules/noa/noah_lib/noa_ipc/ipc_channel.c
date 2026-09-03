#define PW_LOG_MODULE_NAME "ipc"
#include "noa_ipc/ipc_channel.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/stddef.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#endif

#include "noa_ipc/noa_ipc_os.h"

#include "noa_ipc/ipc_buffer.h"
#include "noa_ipc/ipc_buffer_shared_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

inline static bool IpcChIsValid(struct IpcChannelData* data) {
  return data->info;
}

static bool IpcChIsMemoryBased(struct IpcChannelData* data) {
  return IpcChIsValid(data) && (data->info->config.type == kChannelTypeSram ||
                                data->info->config.type == kChannelTypeDram);
}

static bool IpcChIsSource(struct IpcChannelData* data) {
  return IpcChIsValid(data) && (data->info->config.source == data->endpoint);
}

static bool IpcChIsSink(struct IpcChannelData* data) {
  return IpcChIsValid(data) && (data->info->config.sink == data->endpoint);
}

int32_t IpcChInit(struct IpcChannelData* data,
                  enum ChannelEndpoint this_endpoint,
                  struct IpcChannelInfo* info, void* context) {
  IpcChDeinit(data);

  if (!info) {
    return -EINVAL;
  }
  data->info = info;
  data->endpoint = this_endpoint;
  if (!IpcChIsSource(data) && !IpcChIsSink(data)) {
    IpcChDeinit(data);
    return -EPERM;
  }
  data->context = context;

  // create IPC buffer by info->config.type
  if (IpcChIsMemoryBased(data)) {
    data->buffer_os_addr = OsIoRemap(info->transport_info.memory.buffer_base,
                                     info->config.size, data->context);
    int32_t mba_id = IpcChIsSource(data)
                         ? info->transport_info.memory.source_mba_client_id
                         : info->transport_info.memory.sink_mba_client_id;
    if (mba_id >= 0) {
      NoaIpcNotifierInit(&data->notifier, &mba_id, IpcChIsSource(data),
                         IpcChIsSink(data), context);
    }

    IpcBufShmInit(&data->buffer_data, data->buffer_os_addr,
                  data->info->config.size, true, data->notifier,
                  IpcChIsSource(data));

    if (IpcChIsSource(data)) {
      if (!IpcBufGetWriter(&data->buffer_data)) {
        IpcChDeinit(data);
        return -ENXIO;
      }
      // Mark this channel is ready to use for source and sink.
      data->info->status.magic = kChannelInfoMagic;
      NoaIpcFlushDCache(data->info, sizeof(struct IpcChannelInfo));
    }
    if (IpcChIsSink(data)) {
      if (!IpcBufGetReader(&data->buffer_data)) {
        IpcChDeinit(data);
        return -ENXIO;
      }
    }
  } else {
    IpcChDeinit(data);
    return -EINVAL;
  }

  return 0;
}

void IpcChDeinit(struct IpcChannelData* data) {
  // Put null writer/reader is harmless.
  IpcBufPutWriter(&data->buffer_data);
  IpcBufPutReader(&data->buffer_data);

  if (IpcChIsMemoryBased(data)) {
    IpcBufShmDeinit(&data->buffer_data);
    OsIoUnmap(data->buffer_os_addr, data->context);
  }

  NoaIpcNotifierDeinit(data->notifier);
  data->notifier = NULL;

  data->info = NULL;
  data->endpoint = kChannelEndpointCount;
}

int32_t IpcChWrite(struct IpcChannelData* data, const char* buf, uint32_t len) {
  if (!IpcChIsReady(data)) {
    return -EBUSY;
  }
  if (!IpcChIsSource(data)) {
    return -ENXIO;
  }
  return IpcBufWrite(&data->buffer_data, buf, len);
}

int32_t IpcChRead(struct IpcChannelData* data, char* buf, uint32_t buf_len,
                  uint32_t* read_bytes) {
  if (!IpcChIsReady(data)) {
    return -EBUSY;
  }
  if (!IpcChIsSink(data)) {
    return -ENXIO;
  }
  return IpcBufRead(&data->buffer_data, buf, buf_len, read_bytes);
}

bool IpcChIsReady(struct IpcChannelData* data) {
  if (data->is_ready) {
    return true;
  }
  if (!IpcChIsValid(data)) {
    return false;
  }

  data->is_ready = (data->info->status.magic == kChannelInfoMagic);
  return data->is_ready;
}

uint8_t IpcChGetId(struct IpcChannelData* data) {
  if (!IpcChIsValid(data)) {
    return kIpcInvalidChannelId;
  }
  return data->info->config.id;
}
#ifdef __cplusplus
}
#endif
