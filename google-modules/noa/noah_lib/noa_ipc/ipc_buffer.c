#include "noa_ipc/ipc_buffer.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool IpcBufGetReader(struct IpcBufferData* data) {
  if (!data || !data->reader_ops) {
    return false;
  }
  return data->reader_ops->Get(data->context);
}

bool IpcBufPutReader(struct IpcBufferData* data) {
  if (!data || !data->reader_ops) {
    return false;
  }
  return data->reader_ops->Put(data->context);
}

int32_t IpcBufRead(struct IpcBufferData* data, char* buf, uint32_t buf_len,
                   uint32_t* read_bytes) {
  if (!data || !data->reader_ops) {
    return -EINVAL;
  }
  int32_t ret = data->reader_ops->Read(data->context, buf, buf_len, read_bytes);
  if (ret == 0) {
    ret = data->reader_ops->Ack(data->context);
  }
  return ret;
}

bool IpcBufIsDataAvailable(struct IpcBufferData* data) {
  return data && data->reader_ops &&
         data->reader_ops->IsDataAvailable(data->context);
}

int32_t IpcBufTryRead(struct IpcBufferData* data, char* buf, uint32_t buf_len,
                      uint32_t* read_bytes) {
  if (IpcBufIsDataAvailable(data)) {
    return IpcBufRead(data, buf, buf_len, read_bytes);
  }
  return -ENODATA;
}

bool IpcBufGetWriter(struct IpcBufferData* data) {
  if (!data || !data->writer_ops) {
    return false;
  }
  return data->writer_ops->Get(data->context);
}

bool IpcBufPutWriter(struct IpcBufferData* data) {
  if (!data || !data->writer_ops) {
    return false;
  }
  return data->writer_ops->Put(data->context);
}

int32_t IpcBufWrite(struct IpcBufferData* data, const char* bytes,
                    uint32_t len) {
  if (!data || !data->writer_ops) {
    return -EINVAL;
  }
  int32_t ret = data->writer_ops->Write(data->context, bytes, len);
  if (ret == 0) {
    ret = data->writer_ops->Commit(data->context);
  }
  return ret;
}

bool IpcBufIsWritable(struct IpcBufferData* data, uint32_t len) {
  return data && data->writer_ops &&
         data->writer_ops->IsWritable(data->context, len);
}

int32_t IpcBufTryWrite(struct IpcBufferData* data, const char* bytes,
                       uint32_t len) {
  if (IpcBufIsWritable(data, len)) {
    return IpcBufWrite(data, bytes, len);
  }
  return -EAGAIN;
}

#ifdef __cplusplus
}
#endif
