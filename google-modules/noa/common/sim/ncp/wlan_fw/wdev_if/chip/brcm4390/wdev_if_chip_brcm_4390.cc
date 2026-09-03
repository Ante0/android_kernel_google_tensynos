#include "wdev_if_chip_brcm_4390.h"

#include "sys_if/types/types.h"
#include "sys_if/common.h"
#include "sys_if/interrupt/sys_if_interrupt.h"
#include "wlan_log/wlan_log.h"
#include "wdev_if/wdev_if.h"
#include "wlan_cast.h"
#include "modules/wlan_ring/wlan_ring.h"
#include "wlan_service_rpc_protocol.h"

#define WLAN_PCI_CTO_REG_OFFSET (0x90)
#define WLAN_PCI_CTO_INT_SHIFT 16
#define WLAN_PCI_CTO_INT_MASK (1 << WLAN_PCI_CTO_INT_SHIFT)
#define WLAN_PCI_RETRY_COUNT 10

int32_t WdevChipBrcm4390Init(WdevIf *const wdev_if)
{
	WdevIrqInfo *irq_info = &wdev_if->irq_info;

	wdev_if->post_desc_val_method = kWdevPostDescCoherenceValidationMethodBrcmSn;
	wdev_if->cmpl_desc_val_method = kWdevCmplDescCoherenceValidationMethodBrcmSnCsum;

	irq_info->num_irq = 1;
	// The IRQ number should be obtained by registering the ISR through the RPC
	// interface or the shared memory configuration mechanism.
	irq_info->info[0].irq_num = 0;
	irq_info->info[0].rx_data_ring_polling_mask = 0x1;
	irq_info->info[0].tx_cpl_ring_polling_mask = 0x1;

	return 0;
}

void WdevChipBrcm4390Deinit(WdevIf *const wdev_if)
{
}

void WdevChipBrcm4390AcknowledgeInterrupt(WdevIf *const wdev_if, int32_t irq_id)
{
	// Not supported
}

void WdevChipBrcm4390RingTxPostDoorbell(WdevIf *const wdev_if, void *priv)
{
	WlanRing *ring = WLAN_REINTERPRET_CAST(WlanRing *, priv);
	uint32_t value = (0xFF000000 | ring->hw_idx << 16 | ring->write);

	//TODO: b/420769169: Request PCIe link
	// Driver Mode case can be removed after Request/Release PCIe Link supported
	if (SysIfIsDriverMode()) {
		ExtSvcSendEventToApc(wdev_if->ext_svc, kWlanEventTypeRingDoorbell,
				     WLAN_REINTERPRET_CAST(void *, &value), sizeof(uint32_t));
	} else {
		SysIfIoWritel(value, WLAN_REINTERPRET_CAST(void *, wdev_if->doorbell_addr));
	}
	//TODO: b/420769169: Release PCIe Link
}

bool WdevChipBrcm4390CheckPcieCmplTimeOut(void)
{
	uint32_t cto_data;
	uint8_t retry = WLAN_PCI_RETRY_COUNT;

	// Follows the same logic as in the linux_osl.c
	do {
		cto_data = SysIfPcieConfigRead32(WLAN_PCI_CTO_REG_OFFSET);
		if (cto_data != 0xFFFFFFFF) {
			break;
		}
	} while (retry--);

	if (cto_data == 0xFFFFFFFF || (cto_data & WLAN_PCI_CTO_INT_MASK)) {
		WLAN_LOG_ERROR(Isr, "%s(): CTO data(0x%" PRIx32 ") is not zero.", __func__,
			       cto_data);
		return true;
	}

	return false;
}

bool WdevChipBrcm4390FwTrapCheck(uint64_t fw_trap_addr)
{
	uint32_t fw_trap_data;

	if (!fw_trap_addr) {
		WLAN_LOG_ERROR(Dp, "%s(): Invalid wlan_dp or fw_trap_addr.", __func__);
		return true;
	}

	SysIfInvalidDCache(WLAN_STATIC_CAST(const PhyAddr, fw_trap_addr),
			   sizeof(uint32_t));
	fw_trap_data = *(volatile uint32_t *)fw_trap_addr;
	if (fw_trap_data != 0) {
		WLAN_LOG_ERROR(Dp, "%s(): FW trap data(0x%" PRIx32 ") is not zero.", __func__,
			       fw_trap_data);
		return true;
	}
	return false;
}
