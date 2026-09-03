// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/sysfs.h>

#include "feature-control.h"
#include "md2ap-wakemon.h"
#include "mtk_pci.h"
#include "mtk_pm.h"
#include "mtk_port.h"
#include "radio-utils.h"

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
#include "metrics_collection.h"
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)

/* See IRQ table: mtk_dev_cfg_0900.irq_tbl in tmi3/pcie/m9xx/mtk_pci_drv_m9xx.c */
static const u32 irq_src_to_lv2_map[MTK_IRQ_SRC_MAX] = {
	[MTK_IRQ_SRC_ADO] = LV2_REASON_ADO,	    [MTK_IRQ_SRC_DPMAIF] = LV2_REASON_DPMAIF,
	[MTK_IRQ_SRC_CLDMA0] = LV2_REASON_CLDMA0,   [MTK_IRQ_SRC_CLDMA1] = LV2_REASON_CLDMA1,
	[MTK_IRQ_SRC_CLDMA2] = LV2_REASON_CLDMA2,   [MTK_IRQ_SRC_CLDMA3] = LV2_REASON_CLDMA3,
	[MTK_IRQ_SRC_CLDMA4] = LV2_REASON_CLDMA4,   [MTK_IRQ_SRC_MHCCIF] = LV2_REASON_MHCCIF,
	[MTK_IRQ_SRC_DPMAIF2] = LV2_REASON_DPMAIF2, [MTK_IRQ_SRC_DPMAIF3] = LV2_REASON_DPMAIF3,
	[MTK_IRQ_SRC_DPMAIF6] = LV2_REASON_DPMAIF6, [MTK_IRQ_SRC_SAP_RGU] = LV2_REASON_SAP_RGU,
	[MTK_IRQ_SRC_PM_LOCK] = LV2_REASON_PM_LOCK, [MTK_IRQ_SRC_TRAS_SYNC] = LV2_REASON_TRAS_SYNC,
};

static const u32 cldma_to_lv2_map[] = {
	[0] = LV2_REASON_CLDMA0, [1] = LV2_REASON_CLDMA1, [2] = LV2_REASON_CLDMA2,
	[3] = LV2_REASON_CLDMA3, [4] = LV2_REASON_CLDMA4,
};

static const char *const lv2_reason_desc[] = {
#define TABLE_GEN(name, bit) [bit] = #name,
	LV2_REASONS_LIST(TABLE_GEN)
#undef TABLE_GEN
};

static void init_irq_map(struct md2ap_wakemon *wakemon)
{
	struct mtk_pci_priv *priv = wakemon->goog->mdev->hw_priv;
	const int *irq_tbl = priv->cfg->irq_tbl;
	int i, irq_id;
	u32 reason;

	for (i = MTK_IRQ_SRC_MIN + 1; i < MTK_IRQ_SRC_MAX; i++) {
		irq_id = irq_tbl[i];
		reason = irq_src_to_lv2_map[i];

		if (irq_id >= 0 && irq_id < ARRAY_SIZE(wakemon->irq_id_to_lv2_map))
			wakemon->irq_id_to_lv2_map[irq_id] = reason;
		else
			LOG_WARN("Unexpected IRQ ID: %d\n", irq_id);
	}
}

static u32 irq_id_to_lv2_reason(struct md2ap_wakemon *wakemon, u32 irq_id)
{
	if (unlikely(irq_id >= ARRAY_SIZE(wakemon->irq_id_to_lv2_map))) {
		LOG_ERR("Out-of-bound IRQ ID: %u\n", irq_id);
		return 0;
	}

	if (unlikely(wakemon->irq_id_to_lv2_map[irq_id] == 0)) {
		LOG_ERR("Unsupported IRQ ID: %u\n", irq_id);
		return 0;
	}

	return wakemon->irq_id_to_lv2_map[irq_id];
}

static u32 cldma_hw_id_to_lv2_reason(u32 hw_id)
{
	if (unlikely(hw_id >= ARRAY_SIZE(cldma_to_lv2_map))) {
		LOG_ERR("Out-of-bound HW ID: %u\n", hw_id);
		return 0;
	}

	if (unlikely(cldma_to_lv2_map[hw_id] == 0)) {
		LOG_ERR("Unsupported HW ID: %u\n", hw_id);
		return 0;
	}

	return cldma_to_lv2_map[hw_id];
}

static const char *get_lv2_reason_desc(u32 reason)
{
	u32 bit;

	if (unlikely(!reason || hweight32(reason) != 1))
		return "UNKNOWN";

	bit = __ffs(reason);

	if (unlikely(bit >= ARRAY_SIZE(lv2_reason_desc) || !lv2_reason_desc[bit]))
		return "UNDEFINED";

	return lv2_reason_desc[bit];
}

static struct md2ap_wakemon *dev_to_wakemon(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct mtk_md_dev *mdev = pci_get_drvdata(pdev);
	struct radio_google *goog;

	if (!mdev || !mdev->google)
		return NULL;

	goog = mdev->google;

	return goog->md2ap_wakemon;
}

static ssize_t wakeup_reason_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct md2ap_wakemon *wakemon = dev_to_wakemon(dev);

	if (!wakemon)
		return -ENODEV;

	/* Ensures that CPU doesn't use the stale data from before the notification. */
	smp_rmb();

	/* Format: 8,1,0x2,CLDMA1,0x7202a,MDLog */
	return sysfs_emit(buf, "%u,%u,0x%x,%s,0x%x,%s\n", atomic_read(&wakemon->wakeup_cnt),
			  atomic_read(&wakemon->wakeup_reason_lv1),
			  atomic_read(&wakemon->wakeup_reason_lv2), wakemon->wakeup_reason_lv2_desc,
			  atomic_read(&wakemon->wakeup_reason_lv3),
			  wakemon->wakeup_reason_lv3_desc);
}

static ssize_t md2ap_wakeup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct md2ap_wakemon *wakemon = dev_to_wakemon(dev);

	if (!wakemon)
		return -ENODEV;

	/* Ensures that the CPU doesn't use the stale data from before the notification. */
	smp_rmb();

	return sysfs_emit(buf, "%u\n", atomic_read(&wakemon->wakeup_cnt));
}

static DEVICE_ATTR_RO(wakeup_reason);
static DEVICE_ATTR_RO(md2ap_wakeup);

static struct attribute *md2ap_wakemon_attrs[] = {
	&dev_attr_wakeup_reason.attr,
	&dev_attr_md2ap_wakeup.attr,
	NULL,
};

static const struct attribute_group md2ap_wakemon_attr_group = {
	.name = "md2ap_wakemon",
	.attrs = md2ap_wakemon_attrs,
};

static void notify_wakeup_event(struct md2ap_wakemon *wakemon)
{
	if (!wakemon->wakeup_notify_node) {
		LOG_ERR("wakeup_notify_node is NULL\n");
		return;
	}

	LOG_INFO("Wakeup event: cnt=%u, lv1=%u, lv2=0x%x, lv2_desc=%s, lv3=0x%x, lv3_desc=%s\n",
		 atomic_read(&wakemon->wakeup_cnt), atomic_read(&wakemon->wakeup_reason_lv1),
		 atomic_read(&wakemon->wakeup_reason_lv2), wakemon->wakeup_reason_lv2_desc,
		 atomic_read(&wakemon->wakeup_reason_lv3), wakemon->wakeup_reason_lv3_desc);

	/* Ensures that wakeup reasons are committed to memory before notifying the reader. */
	smp_wmb();

	atomic_inc(&wakemon->wakeup_cnt);
	sysfs_notify_dirent(wakemon->wakeup_notify_node);
}

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
static void update_mcf_irq_wakeup_cnt(struct md2ap_wakemon *wakemon)
{
	u32 reason = atomic_read(&wakemon->wakeup_reason_lv2);

	if (reason & MCF_LV2_REASON_NETWORK_GROUP)
		atomic_inc(&wakemon->mcf_network_wakeup_cnt);
	else if (reason & MCF_LV2_REASON_GNSS_GROUP)
		atomic_inc(&wakemon->mcf_gnss_wakeup_cnt);
	else if (reason & MCF_LV2_REASON_CONTROL_GROUP)
		atomic_inc(&wakemon->mcf_control_wakeup_cnt);
	else
		atomic_inc(&wakemon->mcf_misc_wakeup_cnt);
}

static void update_mcf_cldma_wakeup_cnt(struct md2ap_wakemon *wakemon)
{
	char *port = wakemon->wakeup_reason_lv3_desc;

	if (unlikely(!port))
		return;

	if (strncmp(port, "MDLog", WAKEUP_REASON_DESC_LEN) == 0 ||
	    strncmp(port, "SAPLog", WAKEUP_REASON_DESC_LEN) == 0 ||
	    strncmp(port, "MDLogNtfy", WAKEUP_REASON_DESC_LEN) == 0 ||
	    strncmp(port, "MIPC0", WAKEUP_REASON_DESC_LEN) == 0) {
		atomic_inc(&wakemon->mcf_log_wakeup_cnt);
	} else if (strncmp(port, "MIPC8", WAKEUP_REASON_DESC_LEN) == 0) {
		atomic_inc(&wakemon->mcf_gnss_wakeup_cnt);
	} else if (strncmp(port, "MIPC2", WAKEUP_REASON_DESC_LEN) == 0) {
		atomic_inc(&wakemon->mcf_control_wakeup_cnt);
	} else {
		atomic_inc(&wakemon->mcf_misc_wakeup_cnt);
	}
}

static int mcf_pull_modem_wakeup_ap_statistics(struct mcf_modem_wakeup_ap_stats *data, void *priv)
{
	struct md2ap_wakemon *wakemon = priv;

	data->counts[WAKEUP_SRC_ID_NETWORK] = atomic_read(&wakemon->mcf_network_wakeup_cnt);
	data->counts[WAKEUP_SRC_ID_GNSS] = atomic_read(&wakemon->mcf_gnss_wakeup_cnt);
	data->counts[WAKEUP_SRC_ID_LOG] = atomic_read(&wakemon->mcf_log_wakeup_cnt);
	data->counts[WAKEUP_SRC_ID_CONTROL] = atomic_read(&wakemon->mcf_control_wakeup_cnt);
	data->counts[WAKEUP_SRC_ID_MISC] = atomic_read(&wakemon->mcf_misc_wakeup_cnt);

	return 0;
}
#endif /* CONFIG_METRICS_COLLECTION_FRAMEWORK */

int md2ap_wakemon_init(struct radio_google *goog)
{
	struct md2ap_wakemon *md2ap_wakemon;
	struct kernfs_node *kn_dir;
	int ret = 0;

	md2ap_wakemon = devm_kzalloc(goog->mdev->dev, sizeof(*md2ap_wakemon), GFP_KERNEL);
	if (!md2ap_wakemon)
		return -ENOMEM;

	md2ap_wakemon->goog = goog;

	init_irq_map(md2ap_wakemon);

	ret = devm_device_add_group(goog->mdev->dev, &md2ap_wakemon_attr_group);
	if (ret) {
		LOG_ERR("Failed to create sysfs group: %d\n", ret);
		return ret;
	}

	kn_dir = sysfs_get_dirent(goog->mdev->dev->kobj.sd, "md2ap_wakemon");
	if (kn_dir) {
		md2ap_wakemon->wakeup_notify_node = sysfs_get_dirent(kn_dir, "md2ap_wakeup");
		sysfs_put(kn_dir);
	}

	if (!md2ap_wakemon->wakeup_notify_node) {
		LOG_ERR("Failed to get sysfs node: md2ap_wakeup\n");
		return -ENODEV;
	}

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	ret = mcf_register_modem_wakeup_ap(mcf_pull_modem_wakeup_ap_statistics, md2ap_wakemon);
	if (ret)
		LOG_WARN("Failed to register mcf callback: %d\n", ret);
#endif

	goog->md2ap_wakemon = md2ap_wakemon;

	return 0;
}

void md2ap_wakemon_exit(struct radio_google *goog)
{
	if (goog->md2ap_wakemon && goog->md2ap_wakemon->wakeup_notify_node) {
		sysfs_put(goog->md2ap_wakemon->wakeup_notify_node);
		goog->md2ap_wakemon->wakeup_notify_node = NULL;
	}

#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	int ret = 0;

	ret = mcf_unregister_modem_wakeup_ap(mcf_pull_modem_wakeup_ap_statistics,
					     goog->md2ap_wakemon);
	if (ret)
		LOG_WARN("Failed to unregister mcf callback: %d\n", ret);
#endif
}

void md2ap_wakemon_pewake(struct radio_google *goog)
{
	struct md2ap_wakemon *wakemon;

	if (!get_wakemon_enable_status())
		return;

	if (unlikely(!goog))
		return;

	wakemon = goog->md2ap_wakemon;
	if (unlikely(!wakemon))
		return;

	atomic_set(&wakemon->pewake_flag, 1);
}
EXPORT_SYMBOL_GPL(md2ap_wakemon_pewake);

void md2ap_wakemon_resume(struct radio_google *goog)
{
	struct md2ap_wakemon *wakemon;

	if (!get_wakemon_enable_status())
		return;

	if (unlikely(!goog))
		return;

	wakemon = goog->md2ap_wakemon;
	if (!wakemon)
		return;

	if (atomic_xchg(&wakemon->pewake_flag, 0) == 1) {
		atomic_set(&wakemon->wakeup_reason_lv1, LV1_REASON_PEWAKE);
		atomic_set(&wakemon->wakeup_reason_lv2, 0);
		atomic_set(&wakemon->wakeup_reason_lv3, 0);
		strscpy(wakemon->wakeup_reason_lv2_desc, "", WAKEUP_REASON_DESC_LEN);
		strscpy(wakemon->wakeup_reason_lv3_desc, "", WAKEUP_REASON_DESC_LEN);
		atomic_set(&wakemon->wakeup_flag_lv1, 1);
	}
}
EXPORT_SYMBOL_GPL(md2ap_wakemon_resume);

void md2ap_wakemon_resume_link_on(struct radio_google *goog, u32 irq_state)
{
	struct md2ap_wakemon *wakemon;

	if (!get_wakemon_enable_status())
		return;

	if (unlikely(!goog))
		return;

	wakemon = goog->md2ap_wakemon;
	if (!wakemon)
		return;

	if (irq_state) {
		atomic_set(&wakemon->wakeup_reason_lv1, LV1_REASON_MSIX);
		atomic_set(&wakemon->wakeup_reason_lv2, 0);
		atomic_set(&wakemon->wakeup_reason_lv3, 0);
		strscpy(wakemon->wakeup_reason_lv2_desc, "", WAKEUP_REASON_DESC_LEN);
		strscpy(wakemon->wakeup_reason_lv3_desc, "", WAKEUP_REASON_DESC_LEN);
		atomic_set(&wakemon->wakeup_flag_lv1, 1);
	}
}
EXPORT_SYMBOL_GPL(md2ap_wakemon_resume_link_on);

void md2ap_wakemon_pci_irq(struct radio_google *goog, u32 irq_id)
{
	struct mtk_pci_priv *priv;
	struct mtk_pci_pm *pm;
	struct md2ap_wakemon *wakemon;
	const char *desc;
	u32 reason;

	if (!get_wakemon_enable_status())
		return;

	if (unlikely(!goog))
		return;

	priv = goog->mdev->hw_priv;
	pm = priv->pm;

	wakemon = goog->md2ap_wakemon;
	if (unlikely(!wakemon))
		return;

	reason = irq_id_to_lv2_reason(wakemon, irq_id);

	/* Ignore resume handshake interrupts */
	if (test_bit(PM_IN_SUSPENDED, &pm->state) && reason == LV2_REASON_MHCCIF)
		return;

	if (atomic_xchg(&wakemon->wakeup_flag_lv1, 0) == 1) {
		atomic_set(&wakemon->wakeup_flag_lv2, reason);
		atomic_set(&wakemon->wakeup_reason_lv2, reason);
		desc = get_lv2_reason_desc(reason);
		strscpy(wakemon->wakeup_reason_lv2_desc, desc, WAKEUP_REASON_DESC_LEN);

		if (!(reason & LV2_REASON_CLDMA_GROUP)) {
			notify_wakeup_event(wakemon);
#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
			update_mcf_irq_wakeup_cnt(wakemon);
#endif
		}
	}
}
EXPORT_SYMBOL_GPL(md2ap_wakemon_pci_irq);

bool md2ap_wakemon_cldma_rx_check(struct radio_google *goog, u32 hw_id)
{
	struct md2ap_wakemon *wakemon;
	u32 reason_lv2;

	if (!get_wakemon_enable_status())
		return false;

	if (unlikely(!goog))
		return false;

	wakemon = goog->md2ap_wakemon;
	if (unlikely(!wakemon))
		return false;

	reason_lv2 = cldma_hw_id_to_lv2_reason(hw_id);
	if (unlikely(!reason_lv2))
		return false;

	if (atomic_cmpxchg(&wakemon->wakeup_flag_lv2, reason_lv2, 0) == reason_lv2)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(md2ap_wakemon_cldma_rx_check);

void md2ap_wakemon_cldma_rx(struct radio_google *goog, u32 rxqno, struct mtk_port *port)
{
	struct md2ap_wakemon *wakemon;
	const char *name = "unknown";
	u32 rx_ch = 0;

	if (!get_wakemon_enable_status())
		return;

	if (unlikely(!goog))
		return;

	wakemon = goog->md2ap_wakemon;
	if (unlikely(!wakemon))
		return;

	/* md2ap_wakemon_cldma_rx_check should be called first. */
	if (atomic_read(&wakemon->wakeup_flag_lv2) != 0) {
		LOG_ERR("LV2 wakeup flag not cleared\n");
		return;
	}

	if (likely(port)) {
		name = port->info.name;
		rx_ch = port->info.rx_ch;
	}

	atomic_set(&wakemon->wakeup_reason_lv3, FIELD_PREP(LV3_REASON_RXQNO_MASK, rxqno) |
							FIELD_PREP(LV3_REASON_RXCH_MASK, rx_ch));
	strscpy(wakemon->wakeup_reason_lv3_desc, name, WAKEUP_REASON_DESC_LEN);

	notify_wakeup_event(wakemon);
#if IS_ENABLED(CONFIG_METRICS_COLLECTION_FRAMEWORK)
	update_mcf_cldma_wakeup_cnt(wakemon);
#endif
}
EXPORT_SYMBOL_GPL(md2ap_wakemon_cldma_rx);

void md2ap_wakemon_suspend(struct radio_google *goog)
{
	struct md2ap_wakemon *wakemon;

	if (!get_wakemon_enable_status())
		return;

	if (unlikely(!goog))
		return;

	wakemon = goog->md2ap_wakemon;
	if (!wakemon)
		return;

	atomic_set(&wakemon->pewake_flag, 0);
	atomic_set(&wakemon->wakeup_flag_lv1, 0);
	atomic_set(&wakemon->wakeup_flag_lv2, 0);
	atomic_set(&wakemon->wakeup_flag_lv3, 0);
}
EXPORT_SYMBOL_GPL(md2ap_wakemon_suspend);

#endif /* CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR */
