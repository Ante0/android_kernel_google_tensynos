#ifndef SYS_IF_MEMORY_MEMORY_H
#define SYS_IF_MEMORY_MEMORY_H

#if defined(__KERNEL__)
#include <linux/dma-mapping.h>
#include <linux/stddef.h>
#else
#include <cstdint>
#include <cstddef>
#endif
#include "noa_desc.h"

#if defined(__KERNEL__)
typedef dma_addr_t PhyAddr;
#else
typedef uintptr_t PhyAddr;
#endif

/// @brief Invalidate the data cache for a given memory region.
///
/// @param[in] addr The starting address of the memory region to
/// invalidate.
/// @param[in] size The size of the memory region to invalidate.
void SysIfInvalidDCache(PhyAddr addr, size_t size);

/// @brief Flush the data cache for a given memory region.
///
/// @param[in] addr The starting address of the memory region to
/// flush.
/// @param[in] size The size of the memory region to flush.
void SysIfFlushDCache(PhyAddr addr, size_t size);

///@brief Synchronize the DMA buffer for CPU access.
///
/// This function synchronizes the given DMA buffer for CPU access. It
/// ensures that the CPU can see the latest data written to the buffer
/// by the device.
///
/// @param[in] addr Physical address of the DMA buffer.
/// @param[in] size Size of the DMA buffer.
void SysIfDmaSyncForCpu(PhyAddr addr, size_t size);

/// @brief Synchronize the DMA buffer for device access.
///
/// This function synchronizes the given DMA buffer for device access.
/// It ensures that the device can see the latest data written to the buffer
/// by the CPU.
///
/// @param[in] addr Physical address of the DMA buffer.
/// @param[in] size Size of the DMA buffer.
void SysIfDmaSyncForDevice(PhyAddr addr, size_t size);

/// @brief Allocate a block of DRAM memory.
///
/// @param[in] size The size of the memory block to allocate, in bytes.
/// @return A pointer to the allocated memory block, or NULL if the
/// allocation failed.
void *SysIfAllocateDram(size_t size);

/// @brief Free a block of DRAM memory previously allocated with
/// `SysIfAllocateDram()`.
///
/// @param[in] ptr A pointer to the memory block to free.
void SysIfFree(void *ptr);

#endif /* SYS_IF_MEMORY_MEMORY_H */
