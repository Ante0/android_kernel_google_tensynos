// These implementations for the interrupt interfaces use a system-wide
// interrupt controller. To avoid impacting other tests, the SysIf*Irq APIs are
// not directly tested. However, the underlying helper functions, which provide
// the core interrupt management logic, have comprehensive test coverage.
//
// This approach allows us to validate the interrupt handling logic without
// interfering with other tests that might rely on the system-wide interrupt
// controller.

#include <cstdint>

#include "sys_if_interrupt.h"
#include "sys_if_interrupt_pw.h"

namespace noa::service::wlan_service
{
extern "C" {

void SysIfSetPcieStoredState(void *stored_state)
{
	return SysIfInterruptPwHelper::GetInstance().SetPcieStoredState(stored_state);
}

void SysIfSetupPcieMsiDescs(const void *msi_descs, int32_t num_msi_descs)
{
	return SysIfInterruptPwHelper::SetupPcieMsiDescs(msi_descs, num_msi_descs);
}

uint32_t SysIfPcieConfigRead32(uint32_t offset)
{
	return SysIfInterruptPwHelper::GetInstance().PcieConfigRead32(offset);
}

int32_t SysIfCheckPcieLinkState()
{
	return SysIfInterruptPwHelper::GetInstance().CheckPcieLinkState();
}

int32_t SysIfRcPowerChange(uint8_t power_state)
{
	return SysIfInterruptPwHelper::RcPowerChange(power_state);
}

int32_t SysIfSyncPcieConfig(PhyAddr saved_state, uint32_t saved_state_size, bool save_to_shm)
{
	return SysIfInterruptPwHelper::SyncPcieConfig(saved_state, saved_state_size, save_to_shm);
}

int32_t SysIfRequestIrq(uint32_t irq, IrqHandler irq_handler, uint32_t /* flags */, void *ctx)
{
	return SysIfInterruptPwHelper::GetInstance().RequestIrq(irq, irq_handler, ctx);
}

void SysIfEnableIrq(uint32_t irq)
{
	SysIfInterruptPwHelper::GetInstance().EnableIrq(irq);
}

void SysIfDisableIrq(uint32_t irq)
{
	SysIfInterruptPwHelper::GetInstance().DisableIrq(irq);
}

void SysIfDisableIrqNoSync(uint32_t irq)
{
	SysIfInterruptPwHelper::GetInstance().DisableIrqNoSync(irq);
}

void SysIfClearIrq(uint32_t irq)
{
	SysIfInterruptPwHelper::GetInstance().ClearIrq(irq);
}

const void *SysIfFreeIrq(uint32_t irq, void *ctx)
{
	return SysIfInterruptPwHelper::GetInstance().FreeIrq(irq, ctx);
}

} // extern "C"
} // namespace noa::service::wlan_service
