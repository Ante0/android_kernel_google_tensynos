// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include "pci_platform.h"
#include "debug.h"
#include <linux/pm.h>
#include <linux/of_address.h>

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

const struct cnss_sw_reset_reg_params reset_reg_params = {
	.pcie_txvecdb = 0x360,
	.pcie_txvecstatus = 0x368,
	.pcie_rxvecdb = 0x394,
	.pcie_rxvecstatus = 0x39c,
	.pcie_parf_ltssm = 0x1e081b0,
	.ltssm_value = 0x111,
	.gcc_pcie_hot_rst = 0x1e402bc,
	.gcc_pcie_hot_rst_val = 0x10,
	.pcie_int_all_clear = 0x1e08228,
	.pcie_int_clear_all = 0xffffffff,
	.wlaon_qfprom_pwr_ctrl_reg = 0x01f8031c,
	.qfprom_pwr_ctrl_vdd4blow_mask = 0x4,
	.wlaon_warm_sw_entry = 0x1f80504,
	.wlaon_soc_reset_cause_reg = 0x01f8060c,
	.pcie_q6_cookie_addr = 0x01f80500,
	.pcie_soc_global_reset = 0x3008,
	.pcie_soc_global_reset_v = 0x1,
	.mhistatus = 0x48,
	.mhictrl = 0x38,
	.mhictrl_reset_mask = 0x2,
};

int _cnss_pci_enumerate(struct cnss_plat_data *plat_priv, u32 rc_num)
{
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

int cnss_reg_pci_event(struct cnss_pci_data *pci_priv)
{
	return 0;
}

void cnss_dereg_pci_event(struct cnss_pci_data *pci_priv) {}

int cnss_wlan_adsp_pc_enable(struct cnss_pci_data *pci_priv, bool control)
{
	return 0;
}

#define CNSS_PCI_SUSPEND_RETRY_MAX 20
/* cnss_rc_rtpm_mgmt_wrapper() -
 *	Handle the PCIe RC device's RTPM state machine for
 *	PCIe link suspend/resume.
 *
 * Typically, this should be managed by the RC driver itself.
 * However, since the upstream RC driver is in use and it is difficult
 * to upstream this logic immediately, this function is maintained
 * in the CNSS driver as a short-term solution.
 *
 * The RC RTPM implementation provided here was reviewed by the
 * RC driver team and will be maintained by them.
 */
static inline
int cnss_rc_rtpm_mgmt_wrapper(struct pci_dev *pdev, bool link_up,
			      bool rpm_usage_count_operate)
{
	int ret = -EINVAL, retry = 0;
	struct device *dev, *host_bridge_dev;
	struct pci_dev *root_port;
	bool current_ignore_children;

	root_port = pcie_find_root_port(pdev);
	if (!root_port) {
		cnss_pr_err("PCIe root port is null\n");
		return ret;
	}

	host_bridge_dev = root_port->dev.parent;
	if (!host_bridge_dev) {
		cnss_pr_err("host_bridge_dev is null\n");
		return ret;
	}

	dev = host_bridge_dev->parent;
	if (!dev) {
		cnss_pr_err("PCIe platform device is null\n");
		return ret;
	}
	current_ignore_children = dev->power.ignore_children;

	cnss_pr_info("PCIe PM Enter: usage_count:%d, runtime_status:%d\n",
		     atomic_read(&dev->power.usage_count),
		     dev->power.runtime_status);

	if (link_up) {
		pm_suspend_ignore_children(dev, false);
		if (rpm_usage_count_operate) {
			cnss_pr_dbg("Resume PCIe link with usage_count increase\n");
			ret = pm_runtime_get_sync(dev);
		} else {
			cnss_pr_dbg("Resume PCIe link without usage_count increase\n");
			ret = pm_runtime_resume(dev);
			pm_runtime_barrier(dev);
		}
		cnss_pr_info("PCIe resume: ret:%d, usage_count:%d, runtime_status:%d\n",
			     ret, atomic_read(&dev->power.usage_count),
			     dev->power.runtime_status);

		if (ret) {
			/* the return value 1 from the pm runtime resume means that
			 * the PCIe link has already been resumed before set resume.
			 */
			if (ret == 1) {
				cnss_pr_info("PCIe link has already been resumed\n");
			} else {
				/* restore the usage_count after the runtime resume fail
				 * if the rpm_usage_count_operate is set to true for the
				 * function call pm_runtime_get_sync().
				 */
				if (rpm_usage_count_operate)
					pm_runtime_put_noidle(dev);
				cnss_pr_info("Failed to resume PCIe link\n");
			}
		} else {
			cnss_pr_info("Resume PCIe link successfully\n");
		}
	} else {
		pm_suspend_ignore_children(dev, true);
		if (rpm_usage_count_operate) {
			cnss_pr_dbg("Suspend PCIe link with usage_count decrease\n");
			ret = pm_runtime_put_sync(dev);
		} else {
			cnss_pr_dbg("Suspend PCIe link without usage_count decrease\n");
			ret = pm_runtime_suspend(dev);
			pm_runtime_barrier(dev);
		}
		cnss_pr_info("PCIe suspend: ret:%d, usage_count:%d, runtime_status:%d\n",
			     ret, atomic_read(&dev->power.usage_count),
			     dev->power.runtime_status);

		if (ret) {
			/* restore the usage_count after the runtime suspend fail
			 * if the rpm_usage_count_operate is set to true for the
			 * function call pm_runtime_put_sync().
			 */
			if (rpm_usage_count_operate)
				pm_runtime_get_noresume(dev);
			cnss_pr_info("Failed to suspend PCIe link\n");
		} else {
			while (retry < CNSS_PCI_SUSPEND_RETRY_MAX &&
				dev->power.runtime_status != RPM_SUSPENDED) {
				retry++;
				msleep(100);
			}
			if (retry == CNSS_PCI_SUSPEND_RETRY_MAX) {
				/* restore the usage_count after the runtime suspend fail
				 * if the rpm_usage_count_operate is set to true for the
				 * function call pm_runtime_put_sync().
				 */
				if (rpm_usage_count_operate)
					pm_runtime_get_noresume(dev);
				cnss_pr_info("Failed to suspend PCI link after max retry\n");
				ret = -EINVAL;
			} else {
				cnss_pr_info("Suspend PCIe link successfully\n");
			}
		}
	}

	/* restore the ignore_children flag */
	pm_suspend_ignore_children(dev, current_ignore_children);

	cnss_pr_info("PCIe PM Exit: usage_count:%d, runtime_status:%d\n",
		     atomic_read(&dev->power.usage_count),
		     dev->power.runtime_status);

	return ret;
}

int cnss_set_pci_link(struct cnss_pci_data *pci_priv, bool link_up)
{
	int ret = 0;
	struct cnss_plat_data *plat_priv;
	bool rpm_usage_count_operate = false;

	if (!pci_priv) {
		cnss_pr_err("pci_priv is NULL\n");
		return -ENODEV;
	}

	/* the PCIe link suspend/resume will be set for all the power ctrl
	 * mode when processing the PCIe link down recovery, including the
	 * CNSS_POWER_CTRL_ALWAYS_ON mode.
	 */
	if (pci_priv->pci_link_down_ind) {
		/* the rpm usage count was not dropped when set the PCIe link
		 * suspend during the handle of PCIe link down recovery, and
		 * need to decrease/increase the rpm usage count when set the
		 * PCIe link suspend/resume for the PCIe link down recovery.
		 */
		rpm_usage_count_operate = true;
	} else {
		plat_priv = pci_priv->plat_priv;
		/* PCIe device never enters D3Cold in ALWAYS_ON mode */
		if (plat_priv &&
		    plat_priv->pwr_ctrl_mode == CNSS_POWER_CTRL_ALWAYS_ON)
			return 0;
	}

	cnss_pr_info("%s PCI link, \n", link_up ? "Resuming" : "Suspending");
	ret = cnss_rc_rtpm_mgmt_wrapper(pci_priv->pci_dev, link_up,
					rpm_usage_count_operate);

	cnss_pr_info("cnss_set_pci_link, ret = %d\n", ret);

	return ret;
}

int cnss_set_pci_pwrctrl(struct cnss_pci_data *pci_priv, bool power_on)
{
	return 0;
}

int cnss_pci_prevent_l1(struct device *dev)
{
	return 0;
}
EXPORT_SYMBOL(cnss_pci_prevent_l1);

int __cnss_pci_prevent_l1(struct device *dev)
{
	return 0;
}

void cnss_pci_allow_l1(struct device *dev)
{
}
EXPORT_SYMBOL(cnss_pci_allow_l1);

void __cnss_pci_allow_l1(struct device *dev)
{
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

int cnss_pci_get_msi_assignment(struct cnss_pci_data *pci_priv)
{
	pci_priv->msi_config = &msi_config;

	return 0;
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
	if (cnss_force_fw_assert(&pci_priv->pci_dev->dev) == -EOPNOTSUPP)
		CNSS_ASSERT(0);

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
	if (cnss_force_fw_assert(&pci_priv->pci_dev->dev) == -EOPNOTSUPP)
		CNSS_ASSERT(0);

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
	int size, ret;
	char iommu_name[CNSS_IOMMU_NODE_NAME_MAX_LEN];

	if (!pci_priv->plat_priv) {
		cnss_pr_err("plat_priv is NULL\n");
		return -EINVAL;
	}

	ret = snprintf(iommu_name, CNSS_IOMMU_NODE_NAME_MAX_LEN,
			"cnss_pci%d_iommu_region_partition",
			pci_priv->plat_priv->rc_num);
	if (ret < 0 || ret >= CNSS_IOMMU_NODE_NAME_MAX_LEN) {
		cnss_pr_err("Failed to get iommu node rc:%d ret:%d\n",
			    pci_priv->plat_priv->rc_num, ret);
		return -EINVAL;
	}

	of_node = of_find_node_by_name(pci_dev->dev.of_node,
				       iommu_name);
	if (!of_node)
		/*
		 * fallback to check default name
		 * cnss_pci0_iommu_region_partition
		 * again,in case DTS name is not
		 * matching to actual rc number
		 */
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
	struct cnss_plat_data *plat_priv = pci_priv->plat_priv;
	struct device_node *of_node;
	struct resource *res;
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

	pci_priv->iommu_geometry = of_property_read_bool(of_node,
							 "qcom,iommu-geometry");
	cnss_pr_dbg("iommu_geometry: %d\n", pci_priv->iommu_geometry);

	of_node_put(of_node);

	return 0;
}

void cnss_pci_init_warm_reset_params(struct cnss_pci_data *pci_priv)
{
	if (pci_priv->plat_priv->pwr_ctrl_mode != CNSS_POWER_CTRL_ALWAYS_ON)
		return;

	switch (pci_priv->pci_dev->device) {
	case QCA6490_DEVICE_ID:
	case KIWI_DEVICE_ID:
	case PEACH_DEVICE_ID:
	case FIG_DEVICE_ID:
		pci_priv->reset_regs = &reset_reg_params;
		break;
	default:
		cnss_pr_err("Not support get device 0x%x reset reg params",
			    pci_priv->pci_dev->device);
		pci_priv->reset_regs = NULL;
		return;
	}

	cnss_pr_info("init reset regs for device 0x%x\n",
		     pci_priv->pci_dev->device);

	return;
}

static void cnss_mhi_reset_txvecdb(struct cnss_pci_data *pci_priv)
{
	int ret;
	unsigned int offset;

	offset = pci_priv->reset_regs->pcie_txvecdb;
	ret = cnss_pci_reg_write(pci_priv, offset, 0);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
			    0, offset, ret);
		return;
	}
}

static void cnss_mhi_reset_txvecstatus(struct cnss_pci_data *pci_priv)
{
	int ret;
	unsigned int offset;

	offset = pci_priv->reset_regs->pcie_txvecstatus;
	ret = cnss_pci_reg_write(pci_priv, offset, 0);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
			    0, offset, ret);
		return;
	}
}

static void cnss_mhi_reset_rxvecdb(struct cnss_pci_data *pci_priv)
{
	int ret;
	unsigned int offset;

	offset = pci_priv->reset_regs->pcie_rxvecdb;
	ret = cnss_pci_reg_write(pci_priv, offset, 0);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
			    0, offset, ret);
		return;
	}
}

static void cnss_mhi_reset_rxvecstatus(struct cnss_pci_data *pci_priv)
{
	int ret;
	unsigned int offset;

	offset = pci_priv->reset_regs->pcie_rxvecstatus;
	ret = cnss_pci_reg_write(pci_priv, offset, 0);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
			    0, offset, ret);
		return;
	}
}

static void cnss_mhi_clear_vector(struct cnss_pci_data *pci_priv)
{
	cnss_mhi_reset_txvecdb(pci_priv);
	cnss_mhi_reset_txvecstatus(pci_priv);
	cnss_mhi_reset_rxvecdb(pci_priv);
	cnss_mhi_reset_rxvecstatus(pci_priv);
}

static void cnss_pci_enable_ltssm(struct cnss_pci_data *pci_priv)
{
	unsigned int val;
	int i, ret;
	unsigned int ltssm_offset, ltssm_val;
	unsigned int hot_rst_offset, hot_rst_val;

	ltssm_offset = pci_priv->reset_regs->pcie_parf_ltssm;
	ltssm_val = pci_priv->reset_regs->ltssm_value;

	cnss_pci_reg_read(pci_priv, ltssm_offset, &val);

	/* PCIE link seems very unstable after the Hot Reset*/
	for (i = 0; val != ltssm_val && i < 5; i++) {
		if (val == 0xffffffff)
			mdelay(5);

		ret = cnss_pci_reg_write(pci_priv, ltssm_offset, ltssm_val);
		if (ret) {
			cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
				    ltssm_val,
				    ltssm_offset, ret);
			return;
		}
		cnss_pci_reg_read(pci_priv, ltssm_offset, &val);
	}
	cnss_pr_dbg("ltssm val 0x%x\n", val);

	hot_rst_offset = pci_priv->reset_regs->gcc_pcie_hot_rst;
	hot_rst_val = pci_priv->reset_regs->gcc_pcie_hot_rst_val;

	cnss_pci_reg_read(pci_priv, hot_rst_offset, &val);

	val |= hot_rst_val;
	ret = cnss_pci_reg_write(pci_priv, hot_rst_offset, val);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
			    val, hot_rst_offset, ret);
		return;
	}

	cnss_pci_reg_read(pci_priv, hot_rst_offset, &val);
	cnss_pr_dbg("pcie_hot_rst val 0x%x\n", val);
}

static void cnss_pci_clear_all_intrs(struct cnss_pci_data *pci_priv)
{
	int ret;
	unsigned int offset, val;

	offset = pci_priv->reset_regs->pcie_int_all_clear;
	val = pci_priv->reset_regs->pcie_int_clear_all;

	ret = cnss_pci_reg_write(pci_priv, offset, val);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
			    val, offset, ret);
		return;
	}
}

static void cnss_pci_reset_wlaon_pwr_ctrl(struct cnss_pci_data *pci_priv)
{
	unsigned int val;
	int ret;
	unsigned int offset, vdd4blow_mask;

	offset = pci_priv->reset_regs->wlaon_qfprom_pwr_ctrl_reg;
	vdd4blow_mask = pci_priv->reset_regs->qfprom_pwr_ctrl_vdd4blow_mask;

	cnss_pci_reg_read(pci_priv, offset, &val);
	cnss_pr_dbg("wlaon_qfprom_pwr_ctrl_reg val 0x%x\n", val);

	val &= ~vdd4blow_mask;
	ret = cnss_pci_reg_write(pci_priv, offset, val);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register 0x%x, err %d\n",
			    val, offset, ret);
		return;
	}
}

static void cnss_pci_clear_dbg_registers(struct cnss_pci_data *pci_priv)
{
	unsigned int val;
	int ret = 0;
	unsigned int warm_sw_entry, soc_reset_cause_reg, q6_cookie;

	warm_sw_entry = pci_priv->reset_regs->wlaon_warm_sw_entry;
	soc_reset_cause_reg = pci_priv->reset_regs->wlaon_soc_reset_cause_reg;
	q6_cookie = pci_priv->reset_regs->pcie_q6_cookie_addr;

	cnss_pci_reg_read(pci_priv, q6_cookie, &val);
	cnss_pr_dbg("pcie_q6_cookie val 0x%x\n", val);

	cnss_pci_reg_read(pci_priv, warm_sw_entry, &val);
	cnss_pr_dbg("wlaon_warm_sw_entry val 0x%x\n", val);

	/* write 0 to WLAON_WARM_SW_ENTRY to prevent Q6 from
	 * continuing warm path and entering dead loop.
	 */
	ret = cnss_pci_reg_write(pci_priv, warm_sw_entry, 0);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register offset 0x%x, err %d\n",
			    0, warm_sw_entry, ret);
		return;
	}
	mdelay(10);

	cnss_pci_reg_read(pci_priv, warm_sw_entry, &val);
	cnss_pr_dbg("wlaon_warm_sw_entry val 0x%x\n", val);

	/* A read clear register. clear the register to prevent
	 * Q6 from entering wrong code path.
	 */
	cnss_pci_reg_read(pci_priv, soc_reset_cause_reg, &val);
	cnss_pr_dbg("soc_reset_cause_reg val %d\n", val);
}

static void cnss_pci_soc_global_reset(struct cnss_pci_data *pci_priv)
{
	unsigned int val;
	int ret = 0;
	unsigned int soc_global_reset, soc_global_reset_v;

	soc_global_reset = pci_priv->reset_regs->pcie_soc_global_reset;
	soc_global_reset_v = pci_priv->reset_regs->pcie_soc_global_reset_v;

	cnss_pci_reg_read(pci_priv, soc_global_reset, &val);
	cnss_pr_dbg("soc_global_reset val 0x%x\n", val);
	val |= soc_global_reset_v;

	ret = cnss_pci_reg_write(pci_priv, soc_global_reset, val);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register offset 0x%x, err %d\n",
			    val, soc_global_reset, ret);
		return;
	}

	mdelay(10);

	val &= ~soc_global_reset_v;

	ret = cnss_pci_reg_write(pci_priv, soc_global_reset, val);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register offset 0x%x, err %d\n",
			    val, soc_global_reset, ret);
		return;
	}
	mdelay(10);

	cnss_pci_reg_read(pci_priv, soc_global_reset, &val);
	if (val == 0xffffffff)
		cnss_pr_err("link down error during global reset\n");

	cnss_pr_dbg("soc_global_reset final val 0x%x\n", val);
}

static void cnss_mhi_set_mhictrl_reset(struct cnss_pci_data *pci_priv)
{
	unsigned int val;
	int ret = 0;
	unsigned int mhistatus, mhictrl, reset_mask;

	mhistatus = pci_priv->reset_regs->mhistatus;
	mhictrl = pci_priv->reset_regs->mhictrl;
	reset_mask = pci_priv->reset_regs->mhictrl_reset_mask;

	cnss_pci_reg_read(pci_priv, mhistatus, &val);
	cnss_pr_dbg("mhistatus val 0x%x\n", val);

	ret = cnss_pci_reg_write(pci_priv, mhictrl, reset_mask);
	if (ret) {
		cnss_pr_err("Failed to write 0x%x to register offset 0x%x, err = %d\n",
			    reset_mask, mhictrl, ret);
		return;
	}
	mdelay(10);
}

int cnss_pci_dev_warm_reset(struct cnss_pci_data *pci_priv, bool power_on)
{
	if (pci_priv->reset_regs == NULL) {
		cnss_pr_err("Device 0x%x reset_regs NULL\n",
			    pci_priv->pci_dev->device);
		return -EINVAL;
	}

	if (power_on) {
		cnss_pci_enable_ltssm(pci_priv);
		cnss_pci_clear_all_intrs(pci_priv);
		cnss_pci_reset_wlaon_pwr_ctrl(pci_priv);
	}

	cnss_mhi_clear_vector(pci_priv);
	cnss_pci_clear_dbg_registers(pci_priv);
	cnss_pci_soc_global_reset(pci_priv);
	cnss_mhi_set_mhictrl_reset(pci_priv);
	return 0;
}
