#ifndef SYS_IF_INTERRUPT_INTERRUPT_H
#define SYS_IF_INTERRUPT_INTERRUPT_H

#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "sys_if/memory/sys_if_memory.h"

#if defined(__KERNEL__)
#include <linux/interrupt.h>
#include <linux/irq.h>
#else
#include "interrupt/interrupt.h"
#endif // defined(__KERNEL__)

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

#if defined(__KERNEL__) || IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
// Linting is disabled for compatibility code.
// NOLINTBEGIN(readability-identifier-naming)
/// @brief An alias for the IRQ_HANDLED constant.
///
/// This alias provides a consistent way to represent a handled interrupt
/// across both Linux and Pigweed environments.
#define kIrqHandled IRQ_HANDLED
/// @brief An alias for the irq_handler_t type.
///
/// This alias ensures that interrupt handler functions have a consistent
/// type definition in both Linux and Pigweed environments.
typedef irq_handler_t IrqHandler;

typedef irqreturn_t NoaIrqReturn;
// NOLINTEND(readability-identifier-naming)
#else
/// @brief A constant representing a successfully handled interrupt.
constexpr noa::module::interrupt::NoaIrqReturn kIrqHandled =
	noa::module::interrupt::NoaIrqReturn::kNoaIrqHandled;
/// @brief An alias for the NoaIrqHandler type.
using IrqHandler = noa::module::interrupt::NoaIrqHandler;

using NoaIrqReturn = noa::module::interrupt::NoaIrqReturn;
#endif // defined(__KERNEL__) || IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)

#define WLAN_PCIE_LINK_UP 1

/// @brief Set up PCIe configuration space.
///
/// This function sets up the PCIe configuration space for the WLAN firmware.
/// @param[in] stored_state The stored PCIe state address need to be set up.
extern void SysIfSetPcieStoredState(void *stored_state);

/// @brief Set up PCIe MSI descriptors.
///
/// This function setup the MSI descriptors table which maps the per device msi_index to the msi vector data.
/// @param[in] msi_descs A pointer to the MSI descriptors table.
/// @param[in] num_msi_desc The number of MSI descriptors.
extern void SysIfSetupPcieMsiDescs(const void *msi_descs, int32_t num_msi_desc);

/// @brief Read 32bit value from the configuration space of the PCIe device.
///
/// This function reads a 32-bit value from the PCIe device's configuration space.
/// @param[in] offset The offset in the configuration space to read from.
/// @return The 32-bit value read from the configuration space.
extern uint32_t SysIfPcieConfigRead32(uint32_t offset);

/// @brief Check PCIe link status
///
/// @return 1 on link up, 0 on link down
extern int32_t SysIfCheckPcieLinkState(void);

/// @brief Set the Root Controller power state corresponding to the WLAN EP.
///
/// @param[in] helper Pointer to the memory map helper structure.
/// @param[in] power_state The power state to set, 1 for up and 0 for down.
/// @return 0 on success, negative error code on failure.
extern int32_t SysIfRcPowerChange(uint8_t power_state);

/// @brief Synchronize the PCIe configuration state.
///
/// @return 0 on success, negative error code on failure.
extern int32_t SysIfSyncPcieConfig(PhyAddr saved_state, uint32_t saved_state_size,
				   bool save_to_shm);

/// @brief Requests an interrupt.
///
/// This function registers an interrupt handler for the specified IRQ number.
///
/// @param[in] irq The IRQ number.
/// @param[in] irq_handler The interrupt handler function.
/// @param[in] flags Flags for configuring the interrupt behavior.
/// @param[in] ctx A context pointer to be passed to the interrupt handler.
/// This pointer is optional and can be null.
/// @return 0 on success, a negative error code otherwise.
/// @retval 0 Success.
/// @retval -EINVAL Requested IRQ number is unsupported.
/// @retval -ENXIO The interrupt controller is disabled.
/// @retval -EBUSY The requested IRQ is already occupied by another handler.
///
/// @post The interrupt is **disabled** after calling this function.
extern int32_t SysIfRequestIrq(uint32_t irq, IrqHandler irq_handler, uint32_t flags, void *ctx);

/// @brief Enables an interrupt.
///
/// This function enables the interrupt with the specified IRQ number.
///
/// @param[in] irq The IRQ number.
///
/// @post The interrupt is **enabled** after calling this function.
extern void SysIfEnableIrq(uint32_t irq);

/// @brief Disables an interrupt.
///
/// This function disables the interrupt with the specified IRQ number
/// ensuring all associated interrupt handlers have completed execution.
///
/// @param[in] irq The IRQ number.
///
/// @post The interrupt is **disabled** after calling this function.
extern void SysIfDisableIrq(uint32_t irq);

/// @brief Disables an interrupt.
///
/// This function disables the interrupt with the specified IRQ number,
/// without synchronising the running interrupt handlers, if any.
///
/// @param[in] irq The IRQ number.
///
/// @post The interrupt is **disabled** after calling this function.
extern void SysIfDisableIrqNoSync(uint32_t irq);

/// @brief Clears an interrupt.
///
/// This function clears any pending interrupt on the specified IRQ number.
///
/// @param irq The IRQ number.
extern void SysIfClearIrq(uint32_t irq);

/// @brief Frees an interrupt.
///
/// This function unregisters the interrupt handler for the specified IRQ
/// number.
///
/// @param[in] irq The IRQ number.
/// @param[in] ctx The context pointer that was passed to `SysIfRequestIrq`.
/// @return A pointer to the previously registered interrupt context, or
/// nullptr if no handler was found for the given IRQ and context, or if the
/// previously registered context was null.
///
/// @post The interrupt is **disabled** after calling this function.
extern const void *SysIfFreeIrq(uint32_t irq, void *ctx);

#if defined(__cplusplus)
} // extern "C"
#endif // defined(__cplusplus)

#endif // SYS_IF_INTERRUPT_INTERRUPT_H
