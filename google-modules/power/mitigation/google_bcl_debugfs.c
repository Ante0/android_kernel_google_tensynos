// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_bcl_debugfs.c Google bcl debug fs driver
 *
 * Copyright (c) 2023, Google LLC. All rights reserved.
 *
 */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
#include <soc/google/exynos-pm.h>
#include <soc/google/exynos-pmu-if.h>
#else
#include <mailbox/protocols/mba/cpm/common/bcl/bcl_service.h>
#endif
#include <linux/debugfs.h>
#include <gbms_storage.h>
#include "bcl.h"
#include "core_pmic/core_pmic_defs.h"
#include "ifpmic/max77759/max77759_irq.h"
#include "ifpmic/max77779/max77779_irq.h"
#include "soc/soc_defs.h"

#define GBMS_STORE_BCLS_BYTES 1

#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
static const unsigned int xclkout_source[] = {
	XCLKOUT_SOURCE_CPU0,
	XCLKOUT_SOURCE_CPU1,
	XCLKOUT_SOURCE_CPU2,
	XCLKOUT_SOURCE_TPU,
	XCLKOUT_SOURCE_GPU
};
#endif

static int get_xclk(void *data, u64 *val, int idx)
{
	struct bcl_device *bcl_dev = data;
	void __iomem *addr;
	unsigned int reg;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return 0;
	*val = 0;
	switch (idx) {
#if IS_ENABLED(CONFIG_SOC_MBU)
	case CPU1A:
	case CPU1B:
#elif IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
	case CPU0:
	case CPU1A:
	case CPU1B:
#else
	case CPU0:
	case CPU1:
#endif
	case CPU2:
		addr = bcl_dev->core_conf[idx].base_mem + CLKOUT;
		if (cpu_sfr_read(bcl_dev, idx, addr, &reg) == 0)
			*val = (u64) reg;
		break;
	case GPU:
	case TPU:
		*val = bcl_dev->core_conf[idx].clk_out;
		break;
	}

	return 0;
}

static int set_xclk(void *data, u64 val, int idx)
{
#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	struct bcl_device *bcl_dev = data;
	void __iomem *addr;

	switch (idx) {
	case CPU0:
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
	case CPU1A:
	case CPU1B:
#else
	case CPU1:
#endif
	case CPU2:
		addr = bcl_dev->core_conf[idx].base_mem + CLKOUT;
		cpu_sfr_write(bcl_dev, idx, addr, val);
		break;
	case GPU:
	case TPU:
	case AUR:
		bcl_dev->core_conf[idx].clk_out = val;
		break;
	}

	exynos_pmu_write(PMU_CLK_OUT, val ? xclkout_source[idx] : 0);
#endif
	return 0;
}

static int get_cpu0clk(void *data, u64 *val)
{
	return get_xclk(data, val, CPU0);
}

static int set_cpu0clk(void *data, u64 val)
{
	return set_xclk(data, val, CPU0);
}

#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
static int get_cpu1aclk(void *data, u64 *val)
{
	return get_xclk(data, val, CPU1A);
}

static int set_cpu1aclk(void *data, u64 val)
{
	return set_xclk(data, val, CPU1A);
}

static int get_cpu1bclk(void *data, u64 *val)
{
	return get_xclk(data, val, CPU1B);
}

static int set_cpu1bclk(void *data, u64 val)
{
	return set_xclk(data, val, CPU1B);
}

#else

static int get_cpu1clk(void *data, u64 *val)
{
	return get_xclk(data, val, CPU1);
}

static int set_cpu1clk(void *data, u64 val)
{
	return set_xclk(data, val, CPU1);
}
#endif

static int get_cpu2clk(void *data, u64 *val)
{
	return get_xclk(data, val, CPU2);
}

static int set_cpu2clk(void *data, u64 val)
{
	return set_xclk(data, val, CPU2);
}

static int get_gpuclk(void *data, u64 *val)
{
	return get_xclk(data, val, GPU);
}

static int set_gpuclk(void *data, u64 val)
{
	return set_xclk(data, val, GPU);
}

static int get_tpuclk(void *data, u64 *val)
{
	return get_xclk(data, val, TPU);
}

static int set_tpuclk(void *data, u64 val)
{
	return set_xclk(data, val, TPU);
}

static int set_vimon_pwrloop_en(void *data, u64 val)
{
	struct bcl_device *bcl_dev = data;

	bcl_dev->vimon_pwr_loop_en = val;

	return 0;
}

static int get_vimon_pwrloop_en(void *data, u64 *val)
{
	struct bcl_device *bcl_dev = data;

	*val = bcl_dev->vimon_pwr_loop_en;

	return 0;
}

static int get_add_perph(void *data, u64 *val)
{
	struct bcl_device *bcl_dev = data;

	*val = (u64)bcl_dev->add_perph;
	return 0;
}

static int set_add_perph(void *data, u64 val)
{
	struct bcl_device *bcl_dev = data;

	if (val < 0 || val > SUBSYSTEM_SOURCE_MAX)
		return -EINVAL;

	bcl_dev->add_perph = (u8)val;
	return 0;
}

static int get_add_addr(void *data, u64 *val)
{
	struct bcl_device *bcl_dev = data;

	*val = bcl_dev->add_addr;
	return 0;
}

static int set_add_addr(void *data, u64 val)
{
	struct bcl_device *bcl_dev = data;

	if (val < 0 || val > SZ_128)
		return -EINVAL;

	bcl_dev->add_addr = val;
	return 0;
}

static int get_add_data(void *data, u64 *val)
{
	struct bcl_device *bcl_dev = data;
	void __iomem *read_addr;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return 0;
	if (bcl_dev->add_addr > SZ_128)
		return -EINVAL;

	if (bcl_dev->add_perph > SUBSYSTEM_SOURCE_MAX)
		return -EINVAL;

	mutex_lock(&bcl_dev->sysreg_lock);
	if ((bcl_dev->add_perph < TPU) && (bcl_dev->add_perph != CPU0)) {
		if (!bcl_disable_power(bcl_dev, bcl_dev->add_perph)) {
			mutex_unlock(&bcl_dev->sysreg_lock);
			return 0;
		}
	}
	read_addr = bcl_dev->base_add_mem[bcl_dev->add_perph] + bcl_dev->add_addr;
	*val = __raw_readl(read_addr);
	if ((bcl_dev->add_perph < TPU) && (bcl_dev->add_perph != CPU0))
		bcl_enable_power(bcl_dev, bcl_dev->add_perph);
	mutex_unlock(&bcl_dev->sysreg_lock);
	return 0;
}

static int set_add_data(void *data, u64 val)
{
	struct bcl_device *bcl_dev = data;
	void __iomem *write_addr;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return 0;
	if (bcl_dev->add_addr > SZ_128)
		return -EINVAL;

	if (bcl_dev->add_perph > SUBSYSTEM_SOURCE_MAX)
		return -EINVAL;

	if (!bcl_dev)
		return -ENOMEM;

	if (!bcl_dev->base_add_mem[bcl_dev->add_perph]) {
		pr_err("Error in ADD perph\n");
		return -ENOMEM;
	}

	mutex_lock(&bcl_dev->sysreg_lock);
	if ((bcl_dev->add_perph < TPU) && (bcl_dev->add_perph != CPU0)) {
		if (!bcl_disable_power(bcl_dev, bcl_dev->add_perph)) {
			mutex_unlock(&bcl_dev->sysreg_lock);
			return 0;
		}
	}
	write_addr = bcl_dev->base_add_mem[bcl_dev->add_perph] + bcl_dev->add_addr;
	__raw_writel(val, write_addr);
	if ((bcl_dev->add_perph < TPU) && (bcl_dev->add_perph != CPU0))
		bcl_enable_power(bcl_dev, bcl_dev->add_perph);
	mutex_unlock(&bcl_dev->sysreg_lock);
	return 0;
}


DEFINE_SIMPLE_ATTRIBUTE(cpu0_clkout_fops, get_cpu0clk, set_cpu0clk, "0x%llx\n");
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
DEFINE_SIMPLE_ATTRIBUTE(cpu1a_clkout_fops, get_cpu1aclk, set_cpu1aclk, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(cpu1b_clkout_fops, get_cpu1bclk, set_cpu1bclk, "0x%llx\n");
#else
DEFINE_SIMPLE_ATTRIBUTE(cpu1_clkout_fops, get_cpu1clk, set_cpu1clk, "0x%llx\n");
#endif
DEFINE_SIMPLE_ATTRIBUTE(cpu2_clkout_fops, get_cpu2clk, set_cpu2clk, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(gpu_clkout_fops, get_gpuclk, set_gpuclk, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(tpu_clkout_fops, get_tpuclk, set_tpuclk, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(vimon_pwrloop_enable_fops, get_vimon_pwrloop_en, set_vimon_pwrloop_en,
			"0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(add_perph_fops, get_add_perph, set_add_perph, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(add_addr_fops, get_add_addr, set_add_addr, "0x%llx\n");
DEFINE_SIMPLE_ATTRIBUTE(add_data_fops, get_add_data, set_add_data, "0x%llx\n");

static int get_sys_evt_addr(void *data, u64 *val)
{
	struct bcl_device *bcl_dev = data;
	*val = bcl_dev->debug_sys_evt.addr;
	return 0;
}

static int set_sys_evt_addr(void *data, u64 val)
{
	struct bcl_device *bcl_dev = data;

	bcl_dev->debug_sys_evt.addr = val;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_sys_event_addr, get_sys_evt_addr, set_sys_evt_addr, "0x%llx\n");

static int get_sys_evt_pmic(void *data, u64 *val)
{
	struct bcl_device *bcl_dev = data;
	*val = bcl_dev->debug_sys_evt.pmic;
	return 0;
}

static int set_sys_evt_pmic(void *data, u64 val)
{
	struct bcl_device *bcl_dev = data;

	bcl_dev->debug_sys_evt.pmic = val;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_sys_event_pmic, get_sys_evt_pmic, set_sys_evt_pmic, "0x%llx\n");

static int get_sys_evt_count(void *data, u64 *val)
{
	struct bcl_device *bcl_dev = data;
	*val = bcl_dev->debug_sys_evt.count;
	return 0;
}

static int set_sys_evt_count(void *data, u64 val)
{
	struct bcl_device *bcl_dev = data;

	bcl_dev->debug_sys_evt.count = val;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_sys_event_count, get_sys_evt_count, set_sys_evt_count, "0x%llx\n");

static int get_sys_evt_data(void *data, u64 *val)
{
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
	struct bcl_device *bcl_dev = data;
	int ret;
	uint32_t response[] = {0, 0};

	ret = google_bcl_cpm_send_cmd(bcl_dev, MB_BCL_CMD_GET_SYS_EVT,
				      bcl_dev->debug_sys_evt.count, bcl_dev->debug_sys_evt.pmic,
				      bcl_dev->debug_sys_evt.addr, response);

	if (ret < 0)
		return ret;

	if (response[MB_BCL_IDX_RSP_STATUS] > 0)
		return -EINVAL;

	*val = response[MB_BCL_IDX_RSP_DATA];

#endif
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_sys_event_data, get_sys_evt_data, NULL, "0x%llx\n");

static int get_bcls_val(void *data, u64 *val)
{
	uint32_t rd_val;
	int ret;

	ret = gbms_storage_read(GBMS_TAG_BCLS, &rd_val, GBMS_STORE_BCLS_BYTES);
	if (ret < 0)
		return ret;

	*val = rd_val;

	return 0;
}

static int set_bcls_val(void *data, u64 val)
{
	int ret;

	ret = gbms_storage_write(GBMS_TAG_BCLS, &val, GBMS_STORE_BCLS_BYTES);
	if (ret < 0)
		return ret;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_bcls_val, get_bcls_val, set_bcls_val, "%#llx\n");

void google_init_debugfs(struct bcl_device *bcl_dev)
{
	struct dentry *dentry_add;
	struct dentry *dentry_sys_evt;
	bcl_dev->debug_entry = debugfs_create_dir("google_bcl", 0);
	debugfs_create_file("cpu0_clk_out", 0644, bcl_dev->debug_entry, bcl_dev, &cpu0_clkout_fops);
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
	debugfs_create_file("cpu1a_clk_out", 0644, bcl_dev->debug_entry, bcl_dev,
			    &cpu1a_clkout_fops);
	debugfs_create_file("cpu1b_clk_out", 0644, bcl_dev->debug_entry, bcl_dev,
			    &cpu1b_clkout_fops);
#else
	debugfs_create_file("cpu1_clk_out", 0644, bcl_dev->debug_entry, bcl_dev, &cpu1_clkout_fops);
#endif
	debugfs_create_file("cpu2_clk_out", 0644, bcl_dev->debug_entry, bcl_dev, &cpu2_clkout_fops);
	debugfs_create_file("gpu_clk_out", 0644, bcl_dev->debug_entry, bcl_dev, &gpu_clkout_fops);
	debugfs_create_file("tpu_clk_out", 0644, bcl_dev->debug_entry, bcl_dev, &tpu_clkout_fops);
	debugfs_create_file("vimon_pwrloop_en", 0644, bcl_dev->debug_entry, bcl_dev,
			    &vimon_pwrloop_enable_fops);
	dentry_add = debugfs_create_dir("add", bcl_dev->debug_entry);
	debugfs_create_file("perph", 0600, dentry_add, bcl_dev, &add_perph_fops);
	debugfs_create_file("addr", 0600, dentry_add, bcl_dev, &add_addr_fops);
	debugfs_create_file("data", 0600, dentry_add, bcl_dev, &add_data_fops);

	dentry_sys_evt = debugfs_create_dir("sys_evt", bcl_dev->debug_entry);
	debugfs_create_file("addr", 0644, dentry_sys_evt, bcl_dev, &fops_sys_event_addr);
	debugfs_create_file("pmic", 0644, dentry_sys_evt, bcl_dev, &fops_sys_event_pmic);
	debugfs_create_file("count", 0644, dentry_sys_evt, bcl_dev, &fops_sys_event_count);
	debugfs_create_file("data", 0444, dentry_sys_evt, bcl_dev, &fops_sys_event_data);

	debugfs_create_file("bcls_val", 0644, bcl_dev->debug_entry, bcl_dev, &fops_bcls_val);
}
