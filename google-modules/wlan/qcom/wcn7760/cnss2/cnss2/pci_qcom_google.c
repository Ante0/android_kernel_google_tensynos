// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include "pci_platform.h"
#include "debug.h"
#include "linux/of_address.h"
#include <linux/platform_data/sscoredump.h>
#include <linux/pcie_google_if.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include "main.h"

#define PLT_PATH "/chosen/plat"
#define CDB_PATH "/chosen/config"
#define HW_SKU    "sku"
#define HW_STAGE  "stage"
#define HW_MAJOR  "major"
#define HW_MINOR  "minor"
#define MAX_HW_INFO_LEN   10u
#define MAX_HW_EXT_LEN    (MAX_HW_INFO_LEN * 2)
#define MAX_FILE_COUNT    4u
#define MAX_HW_STAGE      6u
#define DEFAULT_VAL "DEFAULT"
#define WIFI_TURNON_DELAY	200

enum {
	REV_SKU = 0,
	REV_ONLY = 1,
	SKU_ONLY = 2,
	NO_EXT_NAME = 3
};

typedef struct {
	char hw_id[MAX_HW_INFO_LEN];
	char sku[MAX_HW_INFO_LEN];
} sku_info_t;

char hw_stage_name[MAX_HW_STAGE][MAX_HW_INFO_LEN] = {
	"DEV",
	"PROTO",
	"EVT",
	"DVT",
	"PVT",
	"MP",
};

sku_info_t sku_table[] = {
	{ {"G4XQ2"}, {"NA"} },
	{ {"GP1BL"}, {"JPN"} },
	{ {"GQ05X"}, {"ROW"} }
};

typedef struct platform_hw_info {
	unsigned long avail_bmap;
	char ext_name[MAX_FILE_COUNT][MAX_HW_EXT_LEN];
} platform_hw_info_t;

platform_hw_info_t platform_hw_info;

static struct cnss_msi_config msi_config = {
   .total_vectors = 32,
   .total_users = MSI_USERS,
   .users = (struct cnss_msi_user[]) {
       { .name = "MHI", .num_vectors = 3, .base_vector = 0 },
       { .name = "CE", .num_vectors = 10, .base_vector = 3 },
       { .name = "WAKE", .num_vectors = 1, .base_vector = 13 },
       { .name = "DP", .num_vectors = 18, .base_vector = 14 },
   },
};

#ifdef CONFIG_GOOG_USE_PWRCTRL
#include <linux/pci-pwrctrl.h>
#include "../drivers/pci/pci.h"

#define GOOG_WAIT_FOR_PCI_DEV_MS       10000
static bool goog_pwrctrl_ready;
static struct pci_pwrctrl *goog_pwrctrl;

/*
 * Returns NULL if the device is not found. Callers must release a returned
 * device with pci_dev_put().
 */
struct pci_dev *cnss_wait_pcidev(int ch_num)
{
	const int poll_timeout_ms = GOOG_WAIT_FOR_PCI_DEV_MS;
	const int interval_ms = 100;

	for (int i = 0; i < poll_timeout_ms; i += interval_ms) {
		struct pci_bus *pci_bus;
		struct pci_dev *pci_dev;

		pci_bus = pci_find_bus(ch_num, 1);
		if (pci_bus) {
			pci_dev = pci_get_slot(pci_bus, PCI_DEVFN(0, 0));
			if (pci_dev) {
			/*
			 * pci_get_slot() may find a PCI device before
			 * it's actually ready to attach to a driver.
			 * Our callers intend to immediately attach to
			 * it. Wait until it's fully ready, as noted by
			 * the PCI-specific PCI_DEV_ADDED flag.
			 */
				if (pci_dev_is_added(pci_dev))
					return pci_dev;

				dev_dbg(&pci_dev->dev, "found, but not yet added\n");
				pci_dev_put(pci_dev);
			}
		}

		msleep(interval_ms);
	}

	pr_err("Endpoint device for channel %d not found\n", ch_num);
	return NULL;
}
void cnss_goog_pwrctrl_set_ready(bool ready)
{
	if (!goog_pwrctrl) {
		cnss_pr_err("%s: no pwrctrl\n", __func__);
		return;
	}

	if (ready == goog_pwrctrl_ready)
		return;
	goog_pwrctrl_ready = ready;

	if (ready) {
		/*
		 * Wait 200ms after WLAN_EN is pulled high
		 * to prevent CPL timeout issues before deasserting PERST#.
		 */
		cnss_pr_info("Delay %dms after power on\n", WIFI_TURNON_DELAY);
		msleep(WIFI_TURNON_DELAY);
		pci_pwrctrl_device_set_ready(goog_pwrctrl);
	}
	else
		pci_pwrctrl_device_unset_ready(goog_pwrctrl);
}
int cnss_google_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pci_pwrctrl *pwrctrl;

	pwrctrl = devm_kzalloc(dev, sizeof(*pwrctrl), GFP_KERNEL);
	if (!pwrctrl)
		return -ENOMEM;

	pci_pwrctrl_init(pwrctrl, dev);

	goog_pwrctrl = pwrctrl;
	return 0;
}
void cnss_google_plat_remove(void)
{
	/*
	 * Make sure to clear pwrctrl state, even if we didn't power off for
	 * some reason.
	 */
	if (goog_pwrctrl_ready)
		cnss_goog_pwrctrl_set_ready(false);

	goog_pwrctrl = NULL;
}
#endif

int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num)
{
#ifdef CONFIG_GOOG_USE_PWRCTRL
	struct pci_dev *pci_dev __free(pci_dev_put) = NULL;

	/*Set pwrctrl true before PCI enumeration*/
	cnss_goog_pwrctrl_set_ready(true);
	cnss_pr_info("RC enumerate pwrctrl set\n");

	pci_dev = cnss_wait_pcidev(rc_num);
	if (pci_dev == NULL) {
		cnss_goog_pwrctrl_set_ready(false);
		pr_err("%s failed to get pci_dev\n", __func__);
		return 1;
	}
	return 0;
#else
	return google_pcie_rc_poweron(rc_num);
#endif
}

int cnss_pci_get_msi_assignment(struct cnss_pci_data *pci_priv)
{
	pci_priv->msi_config = &msi_config;

	return 0;
}

bool cnss_pci_is_sync_probe(void)
{
	return false;
}

int cnss_pci_assert_perst(struct cnss_pci_data *pci_priv)
{
	return -EOPNOTSUPP;
}

int cnss_pci_disable_pc(struct cnss_pci_data *pci_priv, bool vote)
{
	return 0;
}

int cnss_pci_set_link_bandwidth(struct cnss_pci_data *pci_priv,
				u16 link_speed, u16 link_width)
{
	return 0;
}

int cnss_pci_set_max_link_speed(struct cnss_pci_data *pci_priv,
				u32 rc_num, u16 link_speed)
{
	return 0;
}

#if IS_ENABLED(CONFIG_SOC_MBU)
static void cnss_pci_event_cb(enum google_pcie_callback_type type, void *priv)
{
	struct pci_dev *pci_dev;
	struct cnss_pci_data *pci_priv = priv;

	if (!priv)
		return;

	switch (type) {
	case GPCIE_CB_CPL_TIMEOUT:
	case GPCIE_CB_LINK_DOWN:
		pci_dev = pci_priv->pci_dev;
		cnss_pr_err("%s: Received event 0x%x for pci_dev %p\n",
			__func__, type, pci_dev);
		cnss_pci_handle_linkdown(pci_priv);
		break;
	default:
		cnss_pr_err("Received invalid PCI event: 0x%x\n", type);
	}
}
#endif

int cnss_reg_pci_event(struct cnss_pci_data *pci_priv)
{
#if IS_ENABLED(CONFIG_SOC_MBU)
	return google_pcie_register_callback(pci_priv->pci_dev->bus->domain_nr,
			cnss_pci_event_cb, pci_priv);
#else
	return 0;
#endif
}

void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv)
{
#if IS_ENABLED(CONFIG_SOC_MBU)
	if (pci_priv && pci_priv->pci_dev && pci_priv->pci_dev->bus) {
		google_pcie_unregister_callback(pci_priv->pci_dev->bus->domain_nr);
	}
#endif
}


int cnss_wlan_adsp_pc_enable(struct cnss_pci_data *pci_priv, bool control)
{
	return 0;
}

int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up)
{
#if IS_ENABLED(CONFIG_WCN_GOOGLE)
	cnss_pr_info("RC PM is enabled, skipping link pm:%d\n", link_up);
	return 0;
#if 0
	int ret = 0;
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	u32 rc_num = plat_priv->rc_num;

	cnss_pr_info("%s PCI-%d link", link_up ? "Resuming" : "Suspending", rc_num);

	if (link_up) {
		ret = google_pcie_rc_poweron(rc_num);
	} else {
		ret = google_pcie_rc_poweroff(rc_num);
	}
	cnss_pr_info(": %s\n", ret ? "Fail" : "Success");
	return ret;
#endif
#else
	int ret = 0;
	struct device *dev, *host_bridge_dev;
	struct pci_dev *root_port;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is null\n");
		return -EINVAL;
	}

	root_port = pcie_find_root_port(pci_priv->pci_dev);
	if (!root_port) {
		cnss_pr_err("PCIe root port is null\n");
		return -EINVAL;
	}

	host_bridge_dev = root_port->dev.parent;
	if (!host_bridge_dev) {
		cnss_pr_err("host_bridge_dev is null\n");
		return -EINVAL;
	}

	dev = host_bridge_dev->parent;
	if (!dev) {
		cnss_pr_err("PCIe platform device is null\n");
		return -EINVAL;
	}

	cnss_pr_info("%s PCI link, \n", link_up ? "Resuming" : "Suspending");

	cnss_pr_info("PCIe PM: usage_count:%d, runtime_status:%d\n",
		     atomic_read(&dev->power.usage_count),
		     dev->power.runtime_status);

	if (link_up) {
		ret = pm_runtime_get_sync(dev);
		cnss_pr_info("PCIe resume: ret:%d, usage_count:%d, runtime_status:%d\n",
			     ret, atomic_read(&dev->power.usage_count),
			     dev->power.runtime_status);

		if (ret ||
		    dev->power.runtime_status != RPM_ACTIVE) {
			cnss_pr_info("Faile to resume PCIe link\n");
			return ret;
		}
	} else {
		ret = pm_runtime_put_sync(dev);
		cnss_pr_info("PCIe suspend: ret:%d, usage_count:%d, runtime_status:%d\n",
			     ret, atomic_read(&dev->power.usage_count),
			     dev->power.runtime_status);

		if (ret ||
		    dev->power.runtime_status != RPM_SUSPENDED) {
			cnss_pr_info("Faile to suspend PCIe link\n");
			return ret;
		}
	}

	return ret;
#endif
}

int cnss_set_pci_pwrctrl(struct cnss_pci_data *pci_priv, bool power_on)
{
	/*
	 * PCIe link power on/off during EP WLAN HW on/off being
	 * taken care by this common pwrctrl API called from pci.c
	 * This makes sure when PCIe link suspend/resume being
	 * called as part of WLAN HW ON/OFF. PCIe link is
	 * made ON/OFF.
	 */
	cnss_goog_pwrctrl_set_ready(power_on);
	cnss_pr_info("RC pwrctrl set:%u\n", power_on);

	return 0;
}

int cnss_pci_prevent_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int ret;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&pci_priv->bus_lock);
	ret = __cnss_pci_prevent_l1(dev);
	mutex_unlock(&pci_priv->bus_lock);

	return ret;
}
EXPORT_SYMBOL(cnss_pci_prevent_l1);

int __cnss_pci_prevent_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int aspm_state = 0;
	int ret;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_err("PCIe link is in suspend state\n");
		return -EIO;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return -EIO;
	}

	ret = pci_enable_link_state(pci_dev, aspm_state);

	if (ret)
		cnss_pr_err("Failed to prevent PCIe L1, considered as link down\n");

	return ret;
}

void cnss_pci_allow_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return;
	}

	mutex_lock(&pci_priv->bus_lock);
	__cnss_pci_allow_l1(dev);
	mutex_unlock(&pci_priv->bus_lock);
}
EXPORT_SYMBOL(cnss_pci_allow_l1);

void __cnss_pci_allow_l1(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct cnss_pci_data *pci_priv = cnss_get_pci_priv(pci_dev);
	int aspm_state = 0;
	int ret;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return;
	}

	if (pci_priv->pci_link_state == PCI_LINK_DOWN) {
		cnss_pr_err("PCIe link is in suspend state\n");
		return;
	}

	if (pci_priv->pci_link_down_ind) {
		cnss_pr_err("PCIe link is down\n");
		return;
	}

	aspm_state = PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_CLKPM |
			PCIE_LINK_STATE_L1_1 | PCIE_LINK_STATE_L1_2;

	ret = pci_enable_link_state(pci_dev, aspm_state);
}

int cnss_pci_fmd_enable(struct cnss_pci_data *pci_priv)
{
	return -EOPNOTSUPP;
}

int _cnss_pci_get_reg_dump(struct cnss_pci_data *pci_priv,
			   u8 *buf, u32 len)
{
	return 0;
}

void cnss_pci_update_drv_supported(struct cnss_pci_data *pci_priv)
{
	pci_priv->drv_supported = false;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)) && \
    (LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0))
static int
cnss_pci_smmu_dev_fault_handler(struct iommu_fault *fault,  void *data)
{
	struct cnss_pci_data *pci_priv = data;

	cnss_fatal_err("SMMU fault happened with IOVA 0x%llx\n",
		       fault->event.addr);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	pci_priv->is_smmu_fault = true;
	cnss_pci_update_status(pci_priv, CNSS_FW_DOWN);
	cnss_force_fw_assert(&pci_priv->pci_dev->dev);

	/* IOMMU driver requires -ENOSYS to print debug info. */
	return -ENOSYS;
}

static
void cnss_register_iommu_fault_handler(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;

	iommu_register_device_fault_handler(&pci_dev->dev,
					    cnss_pci_smmu_dev_fault_handler,
					    pci_priv);
}
#else
static int cnss_pci_smmu_fault_handler(struct iommu_domain *domain,
				       struct device *dev, unsigned long iova,
				       int flags, void *handler_token)
{
	struct cnss_pci_data *pci_priv = handler_token;

	cnss_fatal_err("SMMU fault happened with IOVA 0x%lx\n", iova);

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	pci_priv->is_smmu_fault = true;
	cnss_pci_update_status(pci_priv, CNSS_FW_DOWN);
	cnss_force_fw_assert(&pci_priv->pci_dev->dev);

	/* IOMMU driver requires -ENOSYS to print debug info. */
	return -ENOSYS;
}

static
void cnss_register_iommu_fault_handler(struct cnss_pci_data *pci_priv)
{
	iommu_set_fault_handler(pci_priv->iommu_domain,
				cnss_pci_smmu_fault_handler, pci_priv);
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0))
int cnss_pci_get_iommu_addr(struct cnss_pci_data *pci_priv,
			    struct device_node *iommu_group_node)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
	struct device_node *of_node;
	const u32 *maps;
	const u32 *end;
	int size;

	of_node = of_find_node_by_name(pci_dev->dev.of_node,
				       "cnss_pci0_iommu_region_partition");
	if (!of_node)
		return -EINVAL;

	maps = of_get_property(of_node, "iommu-addresses", &size);
	if (!maps) {
		of_node_put(of_node);
		return -EINVAL;
	}

	end = maps + size / sizeof(u32);

	pci_priv->smmu_iova_start = 0;

	while (maps < end) {
		phys_addr_t iova;
		size_t length;

		/*
		 * Skip the device phandle and if required later, we can
		 * check if the device phandle matches with pci_dev->dev.of_node
		 */
		maps++;

		maps = of_translate_dma_region(pci_dev->dev.of_node, maps,
					       &iova, &length);

		/*
		 * Assuming a single contiguous DMA address range
		 */
		if (!pci_priv->smmu_iova_start)
			pci_priv->smmu_iova_start = length;
		else
			pci_priv->smmu_iova_len =
					iova - pci_priv->smmu_iova_start;
	}

	of_node_put(of_node);

	return (pci_priv->smmu_iova_start && pci_priv->smmu_iova_len) ?
		0 : -EINVAL;
}
#else
int cnss_pci_get_iommu_addr(struct cnss_pci_data *pci_priv,
			    struct device_node *iommu_group_node)
{
	u32 addr_win[2];
	int ret;

	ret = of_property_read_u32_array(iommu_group_node,
					 "qcom,iommu-dma-addr-pool",
					 addr_win, ARRAY_SIZE(addr_win));

	pci_priv->smmu_iova_start = addr_win[0];
	pci_priv->smmu_iova_len = addr_win[1];

	return ret;
}
#endif

int cnss_pci_init_smmu(struct cnss_pci_data *pci_priv)
{
	struct pci_dev *pci_dev = pci_priv->pci_dev;
#if !IS_ENABLED(CONFIG_WCN_GOOGLE)
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
#endif
	struct device_node *of_node;
#if !IS_ENABLED(CONFIG_WCN_GOOGLE)
	struct resource *res;
#else
	struct device_node *ipa_node;
#endif
	const char *iommu_dma_type;
	int ret = 0;

	of_node = of_parse_phandle(pci_dev->dev.of_node, "qcom,iommu-group", 0);
	if (!of_node)
		return ret;

	cnss_pr_dbg("Initializing SMMU\n");

	pci_priv->iommu_domain = iommu_get_domain_for_dev(&pci_dev->dev);
	ret = of_property_read_string(of_node, "qcom,iommu-dma",
				      &iommu_dma_type);
	if (!ret && !strcmp("fastmap", iommu_dma_type)) {
		cnss_pr_dbg("Enabling SMMU S1 stage\n");
		pci_priv->smmu_s1_enable = true;
		cnss_register_iommu_fault_handler(pci_priv);
		cnss_register_iommu_fault_handler_irq(pci_priv);
	}

	ret = cnss_pci_get_iommu_addr(pci_priv, of_node);
	if (ret) {
		cnss_pr_err("Invalid SMMU size window, err = %d\n", ret);
		of_node_put(of_node);
		return ret;
	}

	cnss_pr_dbg("smmu_iova_start: %pa, smmu_iova_len: 0x%zx\n",
		    &pci_priv->smmu_iova_start,
		    pci_priv->smmu_iova_len);

#if !IS_ENABLED(CONFIG_WCN_GOOGLE)
	res = platform_get_resource_byname(plat_priv->plat_dev, IORESOURCE_MEM,
					   "smmu_iova_ipa");
	if (res) {
		pci_priv->smmu_iova_ipa_start = res->start;
		pci_priv->smmu_iova_ipa_current = res->start;
		pci_priv->smmu_iova_ipa_len = resource_size(res);
		cnss_pr_dbg("smmu_iova_ipa_start: %pa, smmu_iova_ipa_len: 0x%zx\n",
			    &pci_priv->smmu_iova_ipa_start,
			    pci_priv->smmu_iova_ipa_len);
	}

#else
	ipa_node = of_get_child_by_name(pci_dev->dev.of_node, "smmu-iova-ipa");
	if (ipa_node) {
		u32 reg_vals[2];

		if (of_property_read_u32_array(ipa_node, "reg", reg_vals, 2) == 0) {
			pci_priv->smmu_iova_ipa_start = reg_vals[0];
			pci_priv->smmu_iova_ipa_current = reg_vals[0];
			pci_priv->smmu_iova_ipa_len = reg_vals[1];
			cnss_pr_dbg("smmu_iova_ipa_start: %pa, smmu_iova_ipa_len: 0x%zx\n",
				&pci_priv->smmu_iova_ipa_start,
				pci_priv->smmu_iova_ipa_len);
		}
		of_node_put(ipa_node);
	}
#endif
	pci_priv->iommu_geometry = of_property_read_bool(of_node,
							 "qcom,iommu-geometry");
	cnss_pr_dbg("iommu_geometry: %d\n", pci_priv->iommu_geometry);

	of_node_put(of_node);

	return 0;
}

/*
 * The following functions are for ssrdump.
 */

#define DEVICE_NAME "wlan"

static struct sscd_platform_data sscd_pdata;

static struct platform_device sscd_dev = {
	.name            = DEVICE_NAME,
	.driver_override = SSCD_NAME,
	.id              = -1,
	.dev             = {
		.platform_data = &sscd_pdata,
		.release       = sscd_release,
    },
};

void cnss_register_sscd(void)
{
	platform_device_register(&sscd_dev);
}

void cnss_unregister_sscd(void)
{
	platform_device_unregister(&sscd_dev);
}

void sscd_release(struct device *dev)
{
	cnss_pr_info("%s: enter\n", __FUNCTION__);
}

u8 *crash_info;
void sscd_set_coredump(void *buf, int buf_len)
{
	struct sscd_platform_data *pdata = dev_get_platdata(&sscd_dev.dev);
	struct sscd_segment seg;

	if (pdata->sscd_report) {
		memset(&seg, 0, sizeof(seg));
		seg.addr = buf;
		seg.size = buf_len;
		if (crash_info) {
			pdata->sscd_report(&sscd_dev, &seg, 1, 0, crash_info);
			kfree(crash_info);
			crash_info = 0;
		} else {
			pdata->sscd_report(&sscd_dev, &seg, 1, 0, "Unknown");
		}
	}
}

void crash_info_handler(u8 *info)
{
	u32 string_len = 0;

	if (crash_info) {
		kfree(crash_info);
		crash_info = 0;
	}

	string_len = strlen(info);
	crash_info = kzalloc(string_len + 1, GFP_KERNEL);
	if (!crash_info)
		return;
	strscpy(crash_info, info, string_len + 1);
	crash_info[string_len] = '\0';
}

static void
cnss_set_platform_ext_name(char *hw_rev, char *val_sku)
{
	memset(&platform_hw_info, 0, sizeof(platform_hw_info_t));

	if (strncmp(hw_rev, DEFAULT_VAL, MAX_HW_INFO_LEN) != 0) {
		if (strncmp(val_sku, DEFAULT_VAL, MAX_HW_INFO_LEN) != 0) {
			snprintf(platform_hw_info.ext_name[REV_SKU], MAX_HW_EXT_LEN, "_%s_%s",
				hw_rev, val_sku);
			set_bit(REV_SKU, &platform_hw_info.avail_bmap);
		}
		snprintf(platform_hw_info.ext_name[REV_ONLY], MAX_HW_EXT_LEN, "_%s", hw_rev);
		set_bit(REV_ONLY, &platform_hw_info.avail_bmap);
	}

	if (strncmp(val_sku, DEFAULT_VAL, MAX_HW_INFO_LEN) != 0) {
		snprintf(platform_hw_info.ext_name[SKU_ONLY], MAX_HW_EXT_LEN, "_%s", val_sku);
		set_bit(SKU_ONLY, &platform_hw_info.avail_bmap);
	}

	memset(platform_hw_info.ext_name[NO_EXT_NAME], 0, MAX_HW_EXT_LEN);
	set_bit(NO_EXT_NAME, &platform_hw_info.avail_bmap);
}

int cnss_wlan_init_hardware_info(void)
{
	struct device_node *node = NULL;
	const char *hw_sku = NULL;
	int hw_stage = -1;
	int hw_major = -1;
	int hw_minor = -1;
	int i;
	char val_revision[MAX_HW_INFO_LEN];
	char val_sku[MAX_HW_INFO_LEN];

	strscpy(val_revision, DEFAULT_VAL, MAX_HW_INFO_LEN);
	strscpy(val_sku, DEFAULT_VAL, MAX_HW_INFO_LEN);

	node = of_find_node_by_path(PLT_PATH);
	if (!node) {
		cnss_pr_err("Node not created under %s\n", PLT_PATH);
		goto exit;
	} else {

		if (of_property_read_u32(node, HW_STAGE, &hw_stage)) {
			cnss_pr_err("%s: Failed to get hw stage\n", __FUNCTION__);
			goto exit;
		}

		if (of_property_read_u32(node, HW_MAJOR, &hw_major)) {
			cnss_pr_err("%s: Failed to get hw major\n", __FUNCTION__);
			goto exit;
		}

		if (of_property_read_u32(node, HW_MINOR, &hw_minor)) {
			cnss_pr_err("%s: Failed to get hw minor\n", __FUNCTION__);
			goto exit;
		}

		if (hw_stage > 0 && hw_stage <= MAX_HW_STAGE) {
			snprintf(val_revision, MAX_HW_INFO_LEN, "%s%d.%d",
					hw_stage_name[hw_stage-1], hw_major, hw_minor);

		}
	}

	node = of_find_node_by_path(CDB_PATH);
	if (!node) {
		cnss_pr_err("Node not created under %s\n", CDB_PATH);
		goto exit;
	} else {
		if (of_property_read_string(node, HW_SKU, &hw_sku)) {
			cnss_pr_err("%s: Failed to get hw sku\n", __FUNCTION__);
			goto exit;
		}

		for (i = 0; i < ARRAY_SIZE(sku_table); i++) {
			if (strcmp(hw_sku, sku_table[i].hw_id) == 0) {
				strscpy(val_sku, sku_table[i].sku, MAX_HW_INFO_LEN);
				break;
			}
		}
	}

	cnss_pr_info("%s: val_revision is %s, hw_sku is %s, val_sku is %s\n",
		__FUNCTION__, val_revision, hw_sku, val_sku);

exit:
	cnss_set_platform_ext_name(val_revision, val_sku);
	return 0;
}

int cnss_request_multiple_bdf_files(const struct firmware **fw,
				const char *name, struct device *device)
{
	int i, ret;
	char tmp_name[MAX_FIRMWARE_NAME_LEN];

	for (i = 0; i <= NO_EXT_NAME; i++) {
		if (!test_bit(i, &platform_hw_info.avail_bmap))
			continue;
		memset(tmp_name, 0, MAX_FIRMWARE_NAME_LEN);
		snprintf(tmp_name, MAX_FIRMWARE_NAME_LEN, "%s%s", name,
				platform_hw_info.ext_name[i]);
		ret = firmware_request_nowarn(fw, tmp_name, device);

		if (ret) {
			cnss_pr_info("Failed to load BDF: %s, ret: %d\n", tmp_name, ret);
			continue;
		} else {
			cnss_pr_info("Load BDF successfully: %s, size: %u\n",
							tmp_name, (*fw)->size);
			break;
		}
	}
	return ret;
}

void cnss_pci_init_warm_reset_params(struct cnss_pci_data *pci_priv)
{
}

int cnss_pci_dev_warm_reset(struct cnss_pci_data *pci_priv, bool power_on)
{
        return -EOPNOTSUPP;
}
