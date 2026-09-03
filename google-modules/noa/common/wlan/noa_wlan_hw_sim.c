// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2025 Google LLC.
 *
 */
#include <nep/nep.h>
#include <common/noa_ring_id.h>
#include <common/wlan_ring_id.h>
#include "noa_wlan_client.h"
#include "noa_wlan_hw.h"
#include "noa_wlan_dynamic_switch.h"
#include "wlan_rpc_service/noa_wlan_cmd_dispatch.h"
#include "wlan_rpc_service/noa_wlan_rpc.h"

/* noa interrupt handler */
static irqreturn_t noa_wlan_isr(int id, void *data)
{
	struct noa_wlan_client *client = (struct noa_wlan_client *)data;

	client->nep_apc_intr_cnt += 1;
	/* clear interrupt status first */
	writel(0, (u32 *)client->hw.ints_addr);
	if (client->ops && client->ops->rx_isr)
		client->ops->rx_isr(id);
	return IRQ_HANDLED;
}

int noa_wlan_hw_set(struct noa_wlan_client *client)
{
	struct noa_wlan_hw *hw = &client->hw;
	/* read address from hw port is driver mode simulator mode only */
	struct noa_port *hw_port = noa_sim_get_port(NOA_PORT_WLAN_SW);
	void *func = (void *)noa_wlan_fw_event_recv;
	void *dev = (void *)client->dev;

	/* dpa_dev same as pcie_dev in driver mode. */
	client->dpa_dev = client->dev;
	/* update info from hw_port */
	hw->irq = hw_port->irq;
	hw->intm_addr = (unsigned long)&hw_port->intm;
	hw->ints_addr = (unsigned long)&hw_port->ints;
	hw->doorbell_addr = (unsigned long)&hw_port->doorbell;
	/* regisger interrupt callback function when a interrupt is triggered */
	noa_interrupt_register(hw_port->irq, noa_wlan_isr, client);
	noa_wlan_fw_request_send_async(NOA_WLAN_CMD_REG_RECEIVER, (void *)&func, sizeof(void *));
	noa_wlan_fw_request_send_async(NOA_WLAN_CMD_CLIENT_DEV, (void *)&dev, sizeof(struct device *));
	return 0;
}

void noa_wlan_hw_reset(struct noa_wlan_client *client)
{
	/* read address from hw port is driver mode simulator mode only */
	struct noa_port *hw_port = noa_sim_get_port(NOA_PORT_WLAN_SW);
	/* update info from hw_port */
	/* free interrupt callback function when a interrupt is triggered */
	noa_interrupt_unregister(hw_port->irq, client);
}

int noa_wlan_hw_ringbell_nep(struct noa_wlan_client *client)
{
	struct noa_wlan_hw *hw = &client->hw;

	writel(1, (u32 *)hw->doorbell_addr);
	noa_sim_trig_rx();
	return 0;
}

int noa_wlan_hw_ringbell_ncp(struct noa_wlan_client *client, enum APC2NCP_DOORBELL_NUM doorbell_num)
{
	struct wlan_event_cmd_completion completion = {
		.cmd = doorbell_num,
		.result = 0,
	};

	switch (doorbell_num) {
	case DOORBELL_BM_UPDATE:
		__noa_wlan_fw_request_send_sim(NOA_WLAN_CMD_MOCK_BM_SYNC_DOORBELL, NULL);
		break;
	default:
		return -EINVAL;
	}
	noa_wlan_fw_event_cmd_completion(&completion);
	return 0;
}

int noa_wlan_hw_nep_ring_reg_get(uint8_t interface, uint8_t flow, uint8_t category,
				 uint8_t direction, struct noa_ring_regs *regs);
{
	int ret = NoaRingSharedRegsGet(regs, interface, flow, category, direction);

	if (ret) {
		pr_err("%s(): NoaRingSharedRegsGet fail\n", __func__);
		return -EINVAL;
	}

	return 0;
}

int noa_wlan_hw_nep_ring_reg_query(struct noa_wlan_client *client, int type,
				   struct noa_ring_regs *regs)
{
	switch (type) {
	case NOA_RING_TYPE_CONSUMER:
		noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlan, kNoaNetworkFlowDeviceToHost,
					     kNoaWlanRingRxData, kNoaRingNepOutput, regs);
		break;
	case NOA_RING_TYPE_PRODUCER:
		noa_wlan_hw_nep_ring_reg_get(kNoaNetworkInterfaceWlan, kNoaNetworkFlowHostToDevice,
					     kNoaWlanRingTxData, kNoaRingNepInput, regs);
		break;
	default:
		dev_err(client->dev, "%s(): Unsupported ring type: %d\n", __func__, type);
		return -EINVAL;
	}
	return 0;
}

int noa_wlan_hw_dynamic_switch_init(void *priv)
{
	return 0;
}

void noa_wlan_hw_dynamic_switch_deinit(void *priv)
{
	return;
}

int noa_wlan_hw_crash_dump_register(struct noa_wlan_client *client)
{
	(void)client;
	return 0;
}

void noa_wlan_hw_crash_dump_unregister(void)
{
}
