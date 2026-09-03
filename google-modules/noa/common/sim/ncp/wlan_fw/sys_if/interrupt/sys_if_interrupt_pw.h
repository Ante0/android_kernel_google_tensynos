#pragma once

#include <cstdint>

#include "interrupt/interrupt.h"
#include "pw_assert/assert.h"
#include "pw_toolchain/no_destructor.h"
#include "sys_if_interrupt.h"
#include "common/compiler.h"
#include "wlan/ncp/wlan_pcie_endpoint.h"

namespace noa::service::wlan_service
{

/// @brief Helper class for managing interrupts in Pigweed.
///
/// This class provides a simplified interface for interacting with the
/// InterruptController in PW. It allows requesting, enabling, disabling,
/// clearing, and freeing interrupts.
class SysIfInterruptPwHelper {
    public:
	/// @brief Constructs a helper using a specified interrupt controller.
	///
	/// This constructor allows for dependency injection, enabling the use of a
	/// specific InterruptController instance. This is particularly useful for
	/// testing.
	///
	/// @param[in] interrupt_controller A pointer to the InterruptController
	/// instance. In typical usage, this would be a pointer to the single
	/// system-wide InterruptController. However, for testing purposes, a
	/// different InterruptController can be provided, such as a mock object,
	/// allowing for isolation of the code under test.
	///
	/// @note The interrupt_controller parameter must remain valid for the entire
	/// lifetime of SysIfInterruptPwHelper and cannot be null.
	SysIfInterruptPwHelper(noa::module::interrupt::InterruptController *interrupt_controller)
#if !IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		: interrupt_controller_(*interrupt_controller)
	{
		PW_ASSERT(interrupt_controller != nullptr);
	}
#else
	{
	}
#endif

	/// @brief Gets the singleton instance of SysIfInterruptPwHelper.
	///
	/// This static method returns a reference to the single instance of the
	/// SysIfInterruptPwHelper class. This instance uses the system-wide
	/// interrupt controller.
	///
	/// @return A reference to the SysIfInterruptPwHelper singleton instance.
	static SysIfInterruptPwHelper &GetInstance()
	{
		SEC_FAST_DATA static pw::NoDestructor<SysIfInterruptPwHelper> interrupt_helper(
			noa::module::interrupt::InterruptController::Instance());
		return *interrupt_helper;
	}

	/// @brief Sets up PCIe MSI descriptors.
	///
	/// This function sets up the MSI descriptors table which maps the per device
	/// msi_index to the msi vector data.
	/// @param[in] msi_descs A pointer to the MSI descriptors table.
	/// @param[in] num_msi_descs The number of MSI descriptors.
	static void SetupPcieMsiDescs(const void *msi_descs, int32_t num_msi_descs)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		return WlanPCIeEndpoint::SetupPcieMsiDescs(msi_descs, num_msi_descs);
#else
		return;
#endif
	}

	/// @brief Sets up the PCIe stored state.
	///
	/// This function sets the stored PCIe state for the WLAN firmware.
	/// @param[in] stored_state The stored PCIe state address to be set up.
	static void SetPcieStoredState(void *stored_state)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		return WlanPCIeEndpoint::SetPcieStoredState(stored_state);
#else
		return;
#endif
	}

	/// @brief Read 32-bit value from the configuration space of the PCIe device.
	///
	/// This function reads a 32-bit value from the PCIe device's configuration space.
	/// @param[in] offset The offset in the configuration space to read from.
	/// @return The 32-bit value read from the configuration space.
	static uint32_t PcieConfigRead32(uint32_t offset)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		return WlanPCIeEndpoint::GetInstance().ConfigRead32(offset);
#else
		return 0;
#endif
	}

	/// @brief Checks the PCIe link status.
	///
	/// This function checks the current PCIe link state.
	/// @return 0 if the link is down, 1 if the link is up, or -
	static int32_t CheckPcieLinkState()
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		return WlanPCIeEndpoint::CheckPcieLinkState();
#else
		return WLAN_PCIE_LINK_UP;
#endif
	}

	/// @brief Changes the power state of the PCIe RC
	///
	/// @param[in] power_state The desired power state, 1 for on and 0 for off.
	/// @return 0 on success, negative error code on failure.
	static int32_t RcPowerChange(uint8_t power_state)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		return WlanPCIeEndpoint::RcPowerChange(power_state);
#else
		return 0;
#endif
	}

	/// @brief Saves the PCIe state of the WLAN endpoint.
	///
	/// This function read the PCIe state from the hardware configuration space
	/// and saves it to the EP's shared memory.
	///
	/// @return 0 on success, negative error code on failure.
	static int32_t SyncPcieConfig(PhyAddr saved_state, uint32_t saved_state_size,
				      bool save_to_shm)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		if (save_to_shm) {
			// If the link is down, we should believe that the state is already saved
			// in the shared memory, so we don't need to save it again.
			SysIfFlushDCache(saved_state, saved_state_size);
		} else {
			SysIfInvalidDCache(saved_state, saved_state_size);
		}
#endif
		return 0;
	}

	/// @brief Requests an interrupt.
	///
	/// @param[in] irq The IRQ number.
	/// @param[in] irq_handler The interrupt handler function.
	/// @param[in] ctx A context pointer to be passed to the interrupt handler.
	/// This pointer is optional and can be null.
	/// @return 0 on success, a negative error code otherwise.
	/// @retval 0 Success.
	/// @retval -EINVAL Requested IRQ number is unsupported.
	/// @retval -ENXIO The interrupt controller is disabled.
	/// @retval -EBUSY The requested IRQ is already occupied by another handler.
	///
	/// @post The interrupt is **disabled** after calling this function.
	int32_t RequestIrq(uint32_t irq, IrqHandler irq_handler, void *ctx)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		return WlanPCIeEndpoint::GetInstance().PCIeRequestIRQ(irq, irq_handler, ctx);
#else
		return interrupt_controller_.RegisterInterrupt(irq, "noa_wlan_fw", irq_handler,
							       NOA_IRQF_NAKED_HANDLER, ctx);
#endif
	}

	/// @brief Enables an interrupt.
	///
	/// @param[in] irq The IRQ number.
	///
	/// @post The interrupt is **enabled** after calling this function.
	void EnableIrq(uint32_t irq)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		WlanPCIeEndpoint::GetInstance().EnableIRQ(irq);
#else
		interrupt_controller_.EnableInterrupt(irq);
#endif
	}

	/// @brief Disables an interrupt.
	///
	/// @param[in] irq The IRQ number.
	///
	/// @post The interrupt is **disabled** after calling this function.
	void DisableIrq(uint32_t irq)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		WlanPCIeEndpoint::GetInstance().DisableIRQ(irq);
#else
		interrupt_controller_.DisableInterrupt(irq);
#endif
	}

	/// @brief Disables an interrupt without synchronisation.
	///
	/// @param[in] irq The IRQ number.
	///
	/// @post The interrupt is **disabled** after calling this function.
	void DisableIrqNoSync(uint32_t irq)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		WlanPCIeEndpoint::GetInstance().DisableIRQNoSync(irq);
#else
		interrupt_controller_.DisableInterrupt(irq);
#endif
	}

	/// @brief Clears an interrupt.
	///
	/// @param[in] irq The IRQ number.
	void ClearIrq(uint32_t irq)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
#else
		interrupt_controller_.ClearInterrupt(irq);
#endif
	}

	/// @brief Frees an interrupt.
	///
	/// @param[in] irq The IRQ number.
	/// @param[in] ctx The context pointer that was passed to `RequestIrq`.
	/// @return A pointer to the previously registered interrupt context, or
	/// nullptr if no handler was found for the given IRQ and context, or if the
	/// previously registered context was null.
	///
	/// @post The interrupt is **disabled** after calling this function.
	void *FreeIrq(uint32_t irq, void *ctx)
	{
#if IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
		return nullptr;
#else
		return interrupt_controller_.UnregisterInterrupt(irq, ctx);
#endif
	}

	~SysIfInterruptPwHelper() = default;
	SysIfInterruptPwHelper(const SysIfInterruptPwHelper &) = delete;
	SysIfInterruptPwHelper(SysIfInterruptPwHelper &&) = delete;
	SysIfInterruptPwHelper &operator=(const SysIfInterruptPwHelper &) = delete;
	SysIfInterruptPwHelper &operator=(SysIfInterruptPwHelper &&) = delete;

    private:
// noa::module::interrupt::InterruptController::Instance() always returns a
// valid InterruptController instance that persists for the entire duration
// of the program.
#if !IS_ENABLED(CONFIG_NOA_WLAN_PCIE_SUPPORT)
	noa::module::interrupt::InterruptController &interrupt_controller_ =
		*noa::module::interrupt::InterruptController::Instance();
#endif
};

} // namespace noa::service::wlan_service
