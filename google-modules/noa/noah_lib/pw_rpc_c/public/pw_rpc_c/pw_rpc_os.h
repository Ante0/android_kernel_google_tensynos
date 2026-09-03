#ifndef PW_RPC_OS_H
#define PW_RPC_OS_H

#if defined(__linux__) && defined(__KERNEL__)
#include <linux/types.h>
#else
#include <inttypes.h>
#include <stddef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
//
// OS-specific Functions
//
__attribute__((capability("PwRpcLock"))) typedef void* PwRpcLock;
__attribute__((
    capability("PwRpcNotification"))) typedef void* PwRpcNotification;

///
/// @brief Initialize the lock.
///
/// @param lock The lock reference.
///
void PwRpcLockInit(PwRpcLock* lock);

///
/// @brief Acquire the lock.
///
/// @param lock The lock reference.
///
int32_t PwRpcLockAcquire(PwRpcLock lock);

///
/// @brief Release the lock.
///
/// @param lock The lock reference.
///
void PwRpcLockRelease(PwRpcLock lock);

///
/// @brief Deinitialize the lock.
///
/// @param lock The lock reference.
///
void PwRpcLockDeinit(PwRpcLock lock);

///
/// @brief Allocate memory region.
///
/// @param size memory size in byte.
/// @return The pointer of allocated memory region.
///
void* PwRpcAllocate(size_t size);

///
/// @brief Free allocated memory region.
///
/// @param ptr The point to be freed.
///
void PwRpcFree(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* PW_RPC_OS_H */
