#include "sys_if_interrupt.h"
#include "sys_if/common.h"
#include "wlan_log/wlan_log.h"

void SysIfSetPcieStoredState(void *stored_state)
{
	return;
}

void SysIfSetupPcieMsiDescs(const void *msi_descs, int32_t num_msi_desc)
{
	return;
}

uint32_t SysIfPcieConfigRead32(uint32_t offset)
{
	return 0;
}

int32_t SysIfCheckPcieLinkState(void)
{
	return WLAN_PCIE_LINK_UP;
}

int32_t SysIfRcPowerChange(uint8_t power_state)
{
	return 0;
}

int32_t SysIfSyncPcieConfig(PhyAddr saved_state, uint32_t saved_state_size, bool save_to_shm)
{
	return 0;
}

int32_t SysIfRequestIrq(const uint32_t irq, IrqHandler irq_handler, const uint32_t flags, void *ctx)
{
	struct WlanIntrContext *intr_ctx = (struct WlanIntrContext *)ctx;
	if (request_irq(irq, irq_handler, IRQF_SHARED, "dpa_wlan", intr_ctx)) {
		WLAN_LOG_ERROR(Cfg, "%s(): request IRQ(%u) failed.", __func__, irq);
		return -EINVAL;
	}
	return 0;
}

void SysIfEnableIrq(const uint32_t irq)
{
	struct irq_desc *irq_desc;
	u32 irq_disable_count;

	irq_desc = irq_to_desc(irq);
	if (irq_desc) {
		irq_disable_count = irq_desc->depth;
		while (irq_disable_count--) {
			enable_irq(irq);
		}
	}
}

void SysIfDisableIrq(const uint32_t irq)
{
	disable_irq(irq);
}

void SysIfDisableIrqNoSync(const uint32_t irq)
{
	disable_irq_nosync(irq);
}

void SysIfClearIrq(const uint32_t irq)
{
	// Clear pending bits
}

const void *SysIfFreeIrq(const uint32_t irq, void *ctx)
{
	irq_set_affinity_hint(irq, NULL);
	return free_irq(irq, ctx);
}
