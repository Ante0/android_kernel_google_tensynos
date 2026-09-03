// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include "mtk_dev.h"
#include "mtk_fsm.h"
#include "mtk_google.h"
#include "mtk_pci.h"
#include "mtk_pci_reg.h"
#include "mtk_port.h"
#include "mtk_pwrctl.h"

static struct tmi_ops tmi_ops = { .fsm = {
					  .notifier_register = mtk_fsm_notifier_register,
					  .notifier_unregister = mtk_fsm_notifier_unregister,
				  },
				  .pwrctl = {
					    .force_md_assert = mtk_pwrctl_force_md_assert,
				  } };

struct tmi_ops *mtk_google_get_tmi_ops(void)
{
	return &tmi_ops;
}

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
u32 mtk_google_get_active_irqs(struct mtk_md_dev *mdev)
{
	u32 irq_state = mtk_pci_mac_read32(mdev->hw_priv, REG_MSIX_ISTATUS_HOST_GRP0_0);
	u32 irq_enable = mtk_pci_mac_read32(mdev->hw_priv, REG_IMASK_HOST_MSIX_GRP0_0);
	return irq_state & irq_enable;
}

struct mtk_port *mtk_google_resolve_skb_port(struct sk_buff *skb, void *priv)
{
	struct mtk_ccci_header *ccci_h;
	struct mtk_port *port = priv;
	u16 channel;

	if (!(port->info.flags & PORT_F_RAW_DATA)) {
		ccci_h = mtk_port_strip_header(skb);
		if (likely(ccci_h)) {
			channel = FIELD_GET(MTK_HDR_FLD_CHN, le32_to_cpu(ccci_h->status));
			port = mtk_port_search_by_id(port->port_mngr, channel);
		}
	}

	return port;
}
#endif
