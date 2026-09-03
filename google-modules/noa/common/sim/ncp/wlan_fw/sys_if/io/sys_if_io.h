#ifndef SYS_IF_IO_SYS_IF_IO_H
#define SYS_IF_IO_SYS_IF_IO_H

#include "sys_if/types/types.h"

#if defined(__KERNEL__)
#include <linux/compiler_types.h>
#endif

#if !defined(__KERNEL__) && !defined(__iomem)
#define __iomem
#endif

/// @brief Remap a physical address to a virtual address in kernel
/// space.
///
/// @param phys_addr The physical address to be remapped.
/// @param size The size of the memory region to be remapped.
///
/// @return A virtual address pointing to the remapped memory region,
/// or NULL on error.
extern void __iomem *SysIfIoRemap(uint64_t phys_addr, uint64_t size);

/// @brief Unmap a previously remapped memory region.
///
/// This function unmaps a memory region that was previously remapped
/// using `SysIfIoRemap()`.
///
/// @param[in] addr The virtual address returned by `SysIfIoRemap()`.
extern void SysIfIoUnmap(void __iomem *addr);

/// @brief Write a 16-bit value to an I/O memory address.
///
/// @param[in] data The 16-bit value to write.
/// @param[in] addr The I/O memory address to write to.
extern void SysIfIoWritew(uint16_t data, void __iomem *addr);

/// @brief Read a 16-bit value from an I/O memory address.
///
/// @param[in] addr The I/O memory address to read from.
/// @return The 16-bit value read from the I/O memory address.
extern uint16_t SysIfIoReadw(const volatile void __iomem *addr);

/// @brief Write a 32-bit value to an I/O memory address.
///
/// @param[in] data The 32-bit value to write.
/// @param[in] addr The I/O memory address to write to.
extern void SysIfIoWritel(uint32_t data, void __iomem *addr);

/// @brief Read a 32-bit value from an I/O memory address.
///
/// @param[in] addr The I/O memory address to read from.
/// @return The 32-bit value read from the I/O memory address.
extern uint32_t SysIfIoReadl(const volatile void __iomem *addr);

#endif /* SYS_IF_IO_SYS_IF_IO_H */
