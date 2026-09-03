#ifndef NOA_IPC_OS_H
#define NOA_IPC_OS_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Memory functions
void NoaIpcFlushDCache(volatile void* addr, int32_t dsize);
void NoaIpcInvalidateDCache(volatile void* addr, int32_t dsize);

// Notify function
void NoaIpcNotifierInit(void** notifier_data, void* init_data, bool is_source,
                        bool is_sink, void* context);
void NoaIpcNotifierDeinit(void* notifier_data);
void NoaIpcNotify(void* notifier_data);

typedef int32_t (*OnNotified)(void* context);
void NoaIpcRegisterNotificationHandler(void* notifier_data, OnNotified callback,
                                       void* context);

void NoaIpcSignalInit(void** signal);
void NoaIpcSignalDeinit(void* signal);
int32_t NoaIpcWaitFor(void* signal);
void NoaIpcComplete(void* signal);

// Thread
bool NoaIpcYield(void);

// Memory Allocation
///
/// @brief Allocate memory region.
///
/// @param size memory size in byte.
/// @return The pointer of allocated memory region.
///
void* NoaIpcAllocate(size_t size);

///
/// @brief Free allocated memory region.
///
/// @param ptr The point to be freed.
///
void NoaIpcFree(void* ptr);

///
/// @brief Remap NOA memory range to OS memory range.
///
/// @param noa_addr The NOA view address.
/// @param len The memory range size.
/// @param context The context for IO remapping.
/// @return The OS view address.
///
void* OsIoRemap(uint32_t noa_addr, uint32_t len, void* context);

///
/// @brief Unmap NOA memory range from OS memory range.
///
/// @param os_addr The OS view address.
/// @param context The context for IO unmapping.
///
void OsIoUnmap(void* os_addr, void* context);

#ifdef __cplusplus
}
#endif
#endif /* NOA_IPC_OS_H */
