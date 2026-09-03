#ifndef NOA_IPC_BUFFER_H
#define NOA_IPC_BUFFER_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct IpcBufferWriterOps {
  bool (*Get)(void* context);
  bool (*Put)(void* context);
  int32_t (*Write)(void* context, const char* data, uint32_t len);
  int32_t (*Commit)(void* context);
  bool (*IsWritable)(void* context, uint32_t bytes);
};

struct IpcBufferReaderOps {
  bool (*Get)(void* context);
  bool (*Put)(void* context);
  int32_t (*Read)(void* context, char* buf, uint32_t buf_len,
                  uint32_t* read_bytes);
  int32_t (*Ack)(void* context);
  bool (*IsDataAvailable)(void* context);
};

struct IpcBufferData {
  void* context;
  struct IpcBufferReaderOps* reader_ops;
  struct IpcBufferWriterOps* writer_ops;
};

bool IpcBufGetReader(struct IpcBufferData* data);
bool IpcBufPutReader(struct IpcBufferData* data);
int32_t IpcBufRead(struct IpcBufferData* data, char* buf, uint32_t buf_len,
                   uint32_t* read_bytes);
bool IpcBufIsDataAvailable(struct IpcBufferData* data);
int32_t IpcBufTryRead(struct IpcBufferData* data, char* buf, uint32_t buf_len,
                      uint32_t* read_bytes);
bool IpcBufGetWriter(struct IpcBufferData* data);
bool IpcBufPutWriter(struct IpcBufferData* data);
int32_t IpcBufWrite(struct IpcBufferData* data, const char* bytes,
                    uint32_t len);
bool IpcBufIsWritable(struct IpcBufferData* data, uint32_t len);
int32_t IpcBufTryWrite(struct IpcBufferData* data, const char* bytes,
                       uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* NOA_IPC_BUFFER_H */
