#include "wdev_if_chip_wcn_7760.h"

int32_t WdevChipWcn7760Init(WdevIf *const wdev_if)
{
	WdevIrqInfo *irq_info = &wdev_if->irq_info;

	wdev_if->post_desc_val_method = kWdevPostDescCoherenceValidationMethodNone;
	wdev_if->cmpl_desc_val_method = kWdevCmplDescCoherenceValidationMethodNone;

	/* The IRQ number should be obtained by registering the ISR through the RPC */
	irq_info->num_irq = 0;
	return 0;
}

void WdevChipWcn7760Deinit(WdevIf *const wdev_if)
{
}

void WdevChipWcn7760AcknowledgeInterrupt(WdevIf *const wdev_if, int32_t irq_id)
{
	/* Qualcomm hardware typically doesn't require explicit ack for MSI-X */
}

void WdevChipWcn7760RingTxPostDoorbell(WdevIf *const wdev_if, void *priv)
{
	/* Qualcomm hardware typically doesn't require explicit Doorbell for TX */
}

bool WdevChipWcn7760CheckPcieCmplTimeOut(void)
{
	//TODO:
	return false;
}
