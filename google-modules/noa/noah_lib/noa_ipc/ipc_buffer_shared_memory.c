#define PW_LOG_MODULE_NAME "ipc"
#include "noa_ipc/ipc_buffer_shared_memory.h"

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>
#ifdef CONFIG_64BIT
#define ALIGNMENT_MASK (0x7)
#else
#define ALIGNMENT_MASK (0x3)
#endif
#else
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define ALIGNMENT_MASK (0x3)
#endif

#include "noa_ipc/noa_ipc_os.h"

const uint32_t kMagicInitCode = 0x12345678;

inline static void* IpcBufMemSet(void* dest, const char v, size_t len) {
  size_t start = (size_t)dest;
  size_t end = start + len;

  while (start < end) {
    size_t jump_len = (end - start) & ~ALIGNMENT_MASK;
    if ((end & ALIGNMENT_MASK) || !jump_len) {
      *((char*)end - 1) = v;
      end--;
    } else {
      memset((char*)(end - jump_len), v, jump_len);
      end -= (jump_len);
    }
  };
  return dest;
}

inline static void* IpcBufMemCpy(void* dest, const void* src, size_t len) {
  char* d = (char*)dest;
  const char* s = (const char*)src;
  while (len > 0) {
    d[len - 1] = s[len - 1];
    len--;
  }
  return dest;
}

inline static uint32_t Min(uint32_t a, uint32_t b) { return a < b ? a : b; }

struct IpcBufferShmData {
  // maintain local indexes for 2-pass read and write.
  // remote indexes are updated on Commit() and Ack().
  uint32_t writing_index;
  uint32_t reading_index;
  // maintain fixed variable locally to avoid accessing remote memory space.
  char* buffer_base;
  uint32_t buffer_size;
  bool preamble_mode;
  bool is_ready;
  bool enable_debug_log;
  bool is_source;

  struct BufferInfo* buffer_info;
  void* notifier;
  void* data_available_notif;
  void* writer;
  void* reader;
};

//
// Private functions
//
static bool CheckContext(void* context) {
  if (!context) {
    return false;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;
  if (!shm_data->buffer_info) {
    return false;
  }
  return true;
}

static void IpcBufShmReset(struct IpcBufferShmData* shm_data) {
  shm_data->buffer_base = NULL;
  shm_data->buffer_size = 0x0;
  shm_data->preamble_mode = false;
  shm_data->is_ready = false;
  shm_data->enable_debug_log = false;
  shm_data->is_source = false;
  shm_data->buffer_info = NULL;
  shm_data->notifier = NULL;
  shm_data->writer = NULL;
  shm_data->reader = NULL;
}

static bool IpcBufShmIsReady(struct IpcBufferShmData* shm_data) {
  if (shm_data->is_ready) {
    return true;
  }

  if (shm_data->buffer_info->magic == kMagicInitCode) {
    shm_data->is_ready = true;
  }
  return shm_data->is_ready;
}

static uint32_t IpcBufShmGetRemainingBytes(struct IpcBufferShmData* shm_data) {
  NoaIpcInvalidateDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  uint32_t w_idx = shm_data->buffer_info->write_index;
  uint32_t r_idx = shm_data->buffer_info->read_index;

  // Reserve 1 byte to avoid overlapping read_index.
  if (shm_data->buffer_size < 1) {
    return 0;
  }

  return (w_idx >= r_idx) ? shm_data->buffer_size - (w_idx - r_idx) - 1
                          : r_idx - w_idx - 1;
}

static int32_t IpcBufShmRawWrite(struct IpcBufferShmData* shm_data,
                                 const char* data, uint32_t data_len) {
  uint32_t bytes_until_wrap = shm_data->buffer_size - shm_data->writing_index;
  uint32_t bytes_to_copy = Min(data_len, bytes_until_wrap);

  IpcBufMemCpy(shm_data->buffer_base + shm_data->writing_index, data,
               bytes_to_copy);
  NoaIpcFlushDCache(shm_data->buffer_base + shm_data->writing_index,
                    bytes_to_copy);
  if (bytes_to_copy < data_len) {
    IpcBufMemCpy(shm_data->buffer_base, data + bytes_to_copy,
                 data_len - bytes_to_copy);
    NoaIpcFlushDCache(shm_data->buffer_base, data_len - bytes_to_copy);
    shm_data->writing_index = data_len - bytes_to_copy;
  } else {
    shm_data->writing_index += data_len;
  }
  return 0;
}

static int32_t IpcBufShmRawRead(struct IpcBufferShmData* shm_data, char* buf,
                                uint32_t len_to_read) {
  uint32_t bytes_until_wrap = shm_data->buffer_size - shm_data->reading_index;
  uint32_t bytes_to_copy = Min(len_to_read, bytes_until_wrap);
  NoaIpcInvalidateDCache(shm_data->buffer_base + shm_data->reading_index,
                         bytes_to_copy);
  IpcBufMemCpy(buf, shm_data->buffer_base + shm_data->reading_index,
               bytes_to_copy);
  if (bytes_to_copy < len_to_read) {
    NoaIpcInvalidateDCache(shm_data->buffer_base, len_to_read - bytes_to_copy);
    IpcBufMemCpy(buf + bytes_to_copy, shm_data->buffer_base,
                 len_to_read - bytes_to_copy);
    shm_data->reading_index = len_to_read - bytes_to_copy;
  } else {
    shm_data->reading_index += len_to_read;
  }
  return 0;
}

static int32_t IpcBufShmOnNotified(void* context) {
  if (!context) {
    return -EINVAL;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;
  NoaIpcComplete(shm_data->data_available_notif);
  return 0;
}

//
// Ops
//
static bool IpcBufShmGetWriter(void* context) {
  if (!CheckContext(context)) {
    return false;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;

  if (!shm_data->is_source) {
    return false;
  }
  if (shm_data->buffer_info->writer_num == 0) {
    shm_data->buffer_info->writer_num = 1;
    NoaIpcFlushDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
    shm_data->writer = context;
    return true;
  }
  return false;
}

static bool IpcBufShmPutWriter(void* context) {
  if (!CheckContext(context)) {
    return false;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;

  if (context != shm_data->writer) {
    return false;
  }
  shm_data->buffer_info->writer_num = 0;
  shm_data->writer = NULL;
  NoaIpcFlushDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  return true;
}

static bool IpcBufShmGetReader(void* context) {
  if (!CheckContext(context)) {
    return false;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;
  if (shm_data->is_source) {
    return false;
  }
  if (shm_data->buffer_info->reader_num == 0) {
    shm_data->buffer_info->reader_num = 1;
    NoaIpcFlushDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
    shm_data->reader = context;
    return true;
  }
  return false;
}

static bool IpcBufShmPutReader(void* context) {
  if (!CheckContext(context)) {
    return false;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;
  if (context != shm_data->reader) {
    return false;
  }
  shm_data->buffer_info->reader_num = 0;
  shm_data->reader = NULL;
  NoaIpcFlushDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  return true;
}

static bool IpcBufShmIsWritable(void* context, uint32_t len) {
  if (!CheckContext(context)) {
    return false;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;

  if (!IpcBufShmIsReady(shm_data)) {
    return false;
  }
  if (shm_data->preamble_mode) {
    // Add 4-byte size preamble
    return (sizeof(len) + len) <= IpcBufShmGetRemainingBytes(shm_data);
  } else {
    return len <= IpcBufShmGetRemainingBytes(shm_data);
  }
}

static bool IpcBufShmIsDataAvailable(void* context) {
  if (!CheckContext(context)) {
    return false;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;
  if (!IpcBufShmIsReady(shm_data)) {
    return false;
  }
  return shm_data->buffer_info->write_index !=
         shm_data->buffer_info->read_index;
}

static int32_t IpcBufShmWrite(void* context, const char* bytes, uint32_t len) {
  if (!CheckContext(context)) {
    return -ENODEV;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;

  while (!IpcBufShmIsWritable(shm_data, len)) {
    if (!NoaIpcYield()) {
      return -EPIPE;
    }
  }

  // Reset local variable to recover the writing failure.
  NoaIpcInvalidateDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  shm_data->writing_index = shm_data->buffer_info->write_index;

  int32_t ret = 0;
  if (shm_data->preamble_mode) {
    // Write the data size
    ret = IpcBufShmRawWrite(shm_data, (const char*)&len, sizeof(len));
    if (ret != 0) {
      return ret;
    }
  }

  // Write the data
  ret = IpcBufShmRawWrite(shm_data, bytes, len);
  if (ret != 0) {
    return ret;
  }

  return ret;
};

static int32_t IpcBufShmCommit(void* context) {
  if (!CheckContext(context)) {
    return -ENODEV;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;

  shm_data->buffer_info->write_index = shm_data->writing_index;
  NoaIpcFlushDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  if (shm_data->notifier) {
    NoaIpcNotify(shm_data->notifier);
  }
  return 0;
}

static int32_t IpcBufShmRead(void* context, char* buf, uint32_t buf_len,
			     uint32_t* read_bytes) {
  if (!CheckContext(context)) {
    return -ENODEV;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;

  *read_bytes = 0;
  while (!IpcBufShmIsDataAvailable(shm_data)) {
    if (shm_data->notifier) {
      if (NoaIpcWaitFor(shm_data->data_available_notif)) {
        return -EPIPE;
      }
    } else {
      if (!NoaIpcYield()) {
        return -EPIPE;
      }
    }
  }

  // Reset local variable to recover the reading failure.
  NoaIpcInvalidateDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  shm_data->reading_index = shm_data->buffer_info->read_index;

  int32_t ret = 0;
  uint32_t data_bytes = 0;
  if (shm_data->preamble_mode) {
    ret = IpcBufShmRawRead(shm_data, (char*)&data_bytes, sizeof(uint32_t));
    if (ret != 0) {
      return ret;
    }
  } else {
    NoaIpcInvalidateDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
    uint32_t cur_write_index = shm_data->buffer_info->write_index;
    if (cur_write_index > shm_data->reading_index) {
      data_bytes = cur_write_index - shm_data->reading_index;
    } else {
      data_bytes =
          cur_write_index + (shm_data->buffer_size - shm_data->reading_index);
      data_bytes = Min(data_bytes, buf_len);
    }
  }

  *read_bytes = data_bytes;
  if (buf_len < data_bytes) {
    return -ENOSPC;
  }

  ret = IpcBufShmRawRead(shm_data, buf, data_bytes);
  if (ret != 0) {
    return ret;
  }
  return 0;
}

static int32_t IpcBufShmAck(void* context) {
  if (!CheckContext(context)) {
    return -ENODEV;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)context;

  shm_data->buffer_info->read_index = shm_data->reading_index;
  NoaIpcFlushDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  return 0;
}

struct IpcBufferReaderOps ipc_buf_shm_reader_ops = {
    .Get = IpcBufShmGetReader,
    .Put = IpcBufShmPutReader,
    .Read = IpcBufShmRead,
    .Ack = IpcBufShmAck,
    .IsDataAvailable = IpcBufShmIsDataAvailable,
};

struct IpcBufferWriterOps ipc_buf_shm_writer_ops = {
    .Get = IpcBufShmGetWriter,
    .Put = IpcBufShmPutWriter,
    .Write = IpcBufShmWrite,
    .Commit = IpcBufShmCommit,
    .IsWritable = IpcBufShmIsWritable,
};

//
// Public functions
//
int32_t IpcBufShmInit(struct IpcBufferData* data, void* base, uint32_t size,
                      bool preamble_mode, void* notifier, bool is_source) {
  if (!data) {
    return -EINVAL;
  }

  if (size < sizeof(struct BufferInfo)) {
    return -EINVAL;
  }
  struct IpcBufferShmData* shm_data =
      (struct IpcBufferShmData*)NoaIpcAllocate(sizeof(struct IpcBufferShmData));
  if (!shm_data) {
    return -ENOMEM;
  }
  IpcBufShmReset(shm_data);

  shm_data->buffer_info = (struct BufferInfo*)base;
  shm_data->notifier = notifier;
  shm_data->is_source = is_source;

  // Source side does the buffer initialization.
  if (shm_data->is_source) {
    IpcBufMemSet(shm_data->buffer_info, 0, sizeof(struct BufferInfo));
    shm_data->buffer_info->magic = kMagicInitCode;
    shm_data->buffer_info->size = size - sizeof(struct BufferInfo);
    shm_data->buffer_info->preamble_mode = preamble_mode;
    shm_data->buffer_info->write_index = 0;
    shm_data->buffer_info->read_index = 0;
  }

  NoaIpcSignalInit(&shm_data->data_available_notif);

  NoaIpcInvalidateDCache(shm_data->buffer_info, sizeof(struct BufferInfo));
  shm_data->writing_index = shm_data->buffer_info->write_index;
  shm_data->reading_index = shm_data->buffer_info->read_index;
  // Register RX notification for the sink side.
  if (shm_data->notifier && !shm_data->is_source) {
    NoaIpcRegisterNotificationHandler(shm_data->notifier, IpcBufShmOnNotified,
                                      shm_data);
  }
  // Cache fixed variables.
  shm_data->buffer_base = (char*)base + sizeof(struct BufferInfo);
  shm_data->buffer_size = size - sizeof(struct BufferInfo);
  shm_data->preamble_mode = preamble_mode;

  // Fill the parent data structure
  data->context = shm_data;
  data->reader_ops = &ipc_buf_shm_reader_ops;
  data->writer_ops = &ipc_buf_shm_writer_ops;
  return 0;
}

int32_t IpcBufShmDeinit(struct IpcBufferData* data) {
  if (!data || !data->context) {
    return -EINVAL;
  }
  struct IpcBufferShmData* shm_data = (struct IpcBufferShmData*)data->context;
  if (shm_data->buffer_info) {
    if (shm_data->writer) {
      IpcBufShmPutWriter(shm_data);
    }
    if (shm_data->reader) {
      IpcBufShmPutReader(shm_data);
    }
  }
  NoaIpcSignalDeinit(shm_data->data_available_notif);
  NoaIpcFree(data->context);
  data->context = NULL;
  return 0;
}
