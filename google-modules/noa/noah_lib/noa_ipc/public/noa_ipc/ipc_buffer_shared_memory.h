#ifndef NOA_IPC_BUFFER_SHM_H
#define NOA_IPC_BUFFER_SHM_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#endif

#include "noa_ipc/ipc_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Intrusive Buffer Layout:
// --------------
// | Info       |
// --------------
// | Buffer     |
// |            |
// --------------
struct BufferInfo {
  uint32_t magic;
  uint32_t size;
  volatile uint32_t write_index;
  volatile uint32_t read_index;

  volatile uint8_t preamble_mode : 1;
  volatile uint8_t writer_num : 1;
  volatile uint8_t reader_num : 1;
} __attribute__((aligned(4)));

int32_t IpcBufShmInit(struct IpcBufferData* data, void* base, uint32_t size,
                      bool preamble_mode, void* notifier, bool is_source);
int32_t IpcBufShmDeinit(struct IpcBufferData* data);
#ifdef __cplusplus
}
#endif

#endif /* NOA_IPC_BUFFER_SHM_H */
