// SPDX-License-Identifier: GPL-2.0
/*
 * gs_bcl_soc.c Google bcl driver - Utility
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 *
 */

#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <soc/google/cal-if.h>
#include <soc/google/exynos-cpupm.h>
#include <soc/google/exynos-pm.h>
#include <soc/google/exynos-pmu-if.h>

#include "../soc_defs.h"

const unsigned int clk_stats_offset[] = {
	CPUCL0_CLKDIVSTEP_STAT,
	CLKDIVSTEP_STAT,
	CLKDIVSTEP_STAT,
	TPU_CLKDIVSTEP_STAT,
	G3D_CLKDIVSTEP_STAT,
	AUR_CLKDIVSTEP_STAT,
};

const unsigned int subsystem_pmu[] = {
	PMU_ALIVE_CPU0_STATES,
	PMU_ALIVE_CPU1_STATES,
	PMU_ALIVE_CPU2_STATES,
	PMU_ALIVE_TPU_STATES,
	PMU_ALIVE_GPU_STATES,
	PMU_ALIVE_AUR_STATES
};

static unsigned int subsys_to_pmu(enum SUBSYSTEM_SOURCE subsys_src)
{
	switch (subsys_src) {
	case CPU0:
		return PMU_ALIVE_CPU0_STATES;
	case CPU1:
		return PMU_ALIVE_CPU1_STATES;
	case CPU2:
		return PMU_ALIVE_CPU2_STATES;
	case TPU:
		return PMU_ALIVE_TPU_STATES;
	case GPU:
		return PMU_ALIVE_GPU_STATES;
	default:
		return -EINVAL;
	}
}

static enum CPU_CLUSTER get_cluster_by_source_idx(enum SUBSYSTEM_SOURCE subsys_src)
{
	switch (subsys_src) {
	case CPU0:
		return LITTLE_CLUSTER;
	case CPU1:
		return MID_CLUSTER;
	case CPU2:
		return BIG_CLUSTER;
	default:
		return -EINVAL;
	}
}

static enum SUBSYSTEM_SOURCE get_source_by_cluster(enum CPU_CLUSTER cluster_src)
{
	switch (cluster_src) {
	case LITTLE_CLUSTER:
		return CPU0;
	case MID_CLUSTER:
		return CPU1;
	case BIG_CLUSTER:
		return CPU2;
	default:
		return -EINVAL;
	}
}

ssize_t safe_emit_bcl_cnt(char *buf, struct bcl_zone *zone)
{
	return sysfs_emit(buf, "%d\n", zone ? atomic_read(&zone->bcl_cnt) : 0);
}

ssize_t safe_emit_pre_evt_cnt(char *buf, struct bcl_zone *zone)
{
	return sysfs_emit(buf, "0\n");
}

static int google_bcl_init_clk_div(struct bcl_device *bcl_dev, int idx,
				   unsigned int value)
{
	switch (idx) {
	case TPU:
	case GPU:
	case AUR:
		return -EIO;
	case CPU0:
	case CPU1:
	case CPU2:
		return cpu_buff_write(bcl_dev, idx, CPU_BUFF_CLKDIVSTEP, value);
	}
	return -EINVAL;
}

void google_bcl_clk_div(struct bcl_device *bcl_dev)
{
	if (google_bcl_init_clk_div(bcl_dev, CPU2,
				    bcl_dev->core_conf[CPU2].clkdivstep) != 0)
		dev_err(bcl_dev->device, "CPU2 Address is NULL\n");
	if (google_bcl_init_clk_div(bcl_dev, CPU1,
				    bcl_dev->core_conf[CPU1].clkdivstep) != 0)
		dev_err(bcl_dev->device, "CPU1 Address is NULL\n");
	if (google_bcl_init_clk_div(bcl_dev, CPU0,
				    bcl_dev->core_conf[CPU0].clkdivstep) != 0)
		dev_err(bcl_dev->device, "CPU0 Address is NULL\n");
}

static void google_bcl_enable_vdroop_irq(struct bcl_device *bcl_dev)
{
	void __iomem *gpio_alive;
	unsigned int reg;

	if (!IS_ENABLED(CONFIG_SOC_ZUMA) || !IS_ENABLED(CONFIG_SOC_GS201))
		return;
	gpio_alive = ioremap(GPIO_ALIVE_BASE, SZ_4K);
	reg = __raw_readl(gpio_alive + GPA_CON);
	reg |= 0xFF0000;
	__raw_writel(0xFFFFF22, gpio_alive + GPA_CON);
}

int google_bcl_init_instruction(struct bcl_device *bcl_dev)
{
	bcl_dev->core_conf[CPU0].base_mem = devm_ioremap(bcl_dev->device, CPUCL0_BASE, SZ_8K);
	if (!bcl_dev->core_conf[CPU0].base_mem) {
		dev_err(bcl_dev->device, "cpu0_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[CPU1].base_mem = devm_ioremap(bcl_dev->device, CPUCL1_BASE, SZ_8K);
	if (!bcl_dev->core_conf[CPU1].base_mem) {
		dev_err(bcl_dev->device, "cpu1_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[CPU2].base_mem = devm_ioremap(bcl_dev->device, CPUCL2_BASE, SZ_8K);
	if (!bcl_dev->core_conf[CPU2].base_mem) {
		dev_err(bcl_dev->device, "cpu2_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[TPU].base_mem = devm_ioremap(bcl_dev->device, TPU_BASE, SZ_8K);
	if (!bcl_dev->core_conf[TPU].base_mem) {
		dev_err(bcl_dev->device, "tpu_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[GPU].base_mem = devm_ioremap(bcl_dev->device,
								  G3D_BASE, SZ_8K);
	if (!bcl_dev->core_conf[GPU].base_mem) {
		dev_err(bcl_dev->device, "gpu_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->core_conf[AUR].base_mem = devm_ioremap(bcl_dev->device,
								  AUR_BASE, SZ_8K);
	if (!bcl_dev->core_conf[AUR].base_mem) {
		dev_err(bcl_dev->device, "aur_mem ioremap failed\n");
		return -EIO;
	}
	bcl_dev->sysreg_cpucl0 = devm_ioremap(bcl_dev->device, SYSREG_CPUCL0_BASE, SZ_8K);
	if (!bcl_dev->sysreg_cpucl0) {
		dev_err(bcl_dev->device, "sysreg_cpucl0 ioremap failed\n");
		return -EIO;
	}

	mutex_init(&bcl_dev->sysreg_lock);
	mutex_init(&bcl_dev->cpu_ratio_lock);
	google_bcl_enable_vdroop_irq(bcl_dev);

	bcl_dev->base_add_mem[CPU0] = devm_ioremap(bcl_dev->device, ADD_CPUCL0, SZ_128);
	if (!bcl_dev->base_add_mem[CPU0]) {
		dev_err(bcl_dev->device, "cpu0_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[CPU1] = devm_ioremap(bcl_dev->device, ADD_CPUCL1, SZ_128);
	if (!bcl_dev->base_add_mem[CPU1]) {
		dev_err(bcl_dev->device, "cpu1_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[CPU2] = devm_ioremap(bcl_dev->device, ADD_CPUCL2, SZ_128);
	if (!bcl_dev->base_add_mem[CPU2]) {
		dev_err(bcl_dev->device, "cpu2_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[TPU] = devm_ioremap(bcl_dev->device, ADD_TPU, SZ_128);
	if (!bcl_dev->base_add_mem[TPU]) {
		dev_err(bcl_dev->device, "tpu_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[GPU] = devm_ioremap(bcl_dev->device, ADD_G3D, SZ_128);
	if (!bcl_dev->base_add_mem[GPU]) {
		dev_err(bcl_dev->device, "gpu_add_mem ioremap failed\n");
		return -EIO;
	}

	bcl_dev->base_add_mem[AUR] = devm_ioremap(bcl_dev->device, ADD_AUR, SZ_128);
	if (!bcl_dev->base_add_mem[AUR]) {
		dev_err(bcl_dev->device, "aur_add_mem ioremap failed\n");
		return -EIO;
	}
	return 0;
}

void google_bcl_parse_clk_div_dtree(struct bcl_device *bcl_dev)
{
	int ret;
	struct device_node *np = bcl_dev->device->of_node;
	u32 val;

	ret = of_property_read_u32(np, "tpu_con_heavy", &val);
	bcl_dev->core_conf[TPU].con_heavy = ret ? 0 : val;
	ret = of_property_read_u32(np, "tpu_con_light", &val);
	bcl_dev->core_conf[TPU].con_light = ret ? 0 : val;
	ret = of_property_read_u32(np, "gpu_con_heavy", &val);
	bcl_dev->core_conf[GPU].con_heavy = ret ? 0 : val;
	ret = of_property_read_u32(np, "gpu_con_light", &val);
	bcl_dev->core_conf[GPU].con_light = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu2_con_heavy", &val);
	bcl_dev->core_conf[CPU2].con_heavy = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu2_con_light", &val);
	bcl_dev->core_conf[CPU2].con_light = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu1_con_heavy", &val);
	bcl_dev->core_conf[CPU1].con_heavy = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu1_con_light", &val);
	bcl_dev->core_conf[CPU1].con_light = ret ? 0 : val;
	ret = of_property_read_u32(np, "gpu_clkdivstep", &val);
	bcl_dev->core_conf[GPU].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "tpu_clkdivstep", &val);
	bcl_dev->core_conf[TPU].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "aur_clkdivstep", &val);
	bcl_dev->core_conf[AUR].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu2_clkdivstep", &val);
	bcl_dev->core_conf[CPU2].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu1_clkdivstep", &val);
	bcl_dev->core_conf[CPU1].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "cpu0_clkdivstep", &val);
	bcl_dev->core_conf[CPU0].clkdivstep = ret ? 0 : val;
	ret = of_property_read_u32(np, "rffe_channel", &val);
	bcl_dev->rffe_channel = ret ? 11 : val;
	ret = of_property_read_u32(np, "cpu0_cluster", &val);
	bcl_dev->cpu_cluster[QOS_CPU0] = ret ? CPU0_CLUSTER_MIN : val;
	ret = of_property_read_u32(np, "cpu1_cluster", &val);
	bcl_dev->cpu_cluster[QOS_CPU1] = ret ? CPU1_CLUSTER_MIN : val;
	ret = of_property_read_u32(np, "cpu2_cluster", &val);
	bcl_dev->cpu_cluster[QOS_CPU2] = ret ? CPU2_CLUSTER_MIN : val;
	ret = of_property_read_u32(np, "smpl_ctrl", &val);
	bcl_dev->smpl_ctrl = ret ? DEFAULT_SMPL : val;

	bcl_dev->qos_update_wq = create_singlethread_workqueue("bcl_qos_update");
}

static int bcl_dev_cpu_notifier(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int cpu = raw_smp_processor_id();
	struct bcl_device *bcl_dev;
	unsigned int i, bit_mask;
	int cpu_ind, system_source_idx;

	if (action != CPU_PM_EXIT)
		return NOTIFY_OK;

	bcl_dev = container_of(nfb, struct bcl_device, cpu_nb);
	if (!bcl_dev)
		return -ENODEV;

	/* Ensure BCL driver is initialized */
	if (!bcl_dev->initialized)
		return -ENODEV;

	if (cpu < bcl_dev->cpu_cluster[QOS_CPU1])
		return NOTIFY_OK;

	if (cpu >= bcl_dev->cpu_cluster[QOS_CPU1] && cpu < bcl_dev->cpu_cluster[QOS_CPU2])
		cpu_ind = MID_CLUSTER;
	else
		cpu_ind = BIG_CLUSTER;

	system_source_idx = get_source_by_cluster(cpu_ind);
	if (system_source_idx < 0) {
		dev_err(bcl_dev->device, "invalid system source from cpu cluster");
		return NOTIFY_OK;
	}

	if (bcl_dev->cpu_buff_conf[cpu_ind].wr_update_rqd == 0 &&
	    bcl_dev->cpu_buff_conf[cpu_ind].rd_update_rqd == 0)
		return NOTIFY_OK;

	for (i = 0; i < CPU_BUFF_VALS_MAX; i++) {
		bit_mask = BIT(i);
		if (bcl_dev->cpu_buff_conf[cpu_ind].wr_update_rqd & bit_mask) {
			__raw_writel(bcl_dev->cpu_buff_conf[cpu_ind].buff[i],
				     bcl_dev->core_conf[system_source_idx].base_mem +
				     bcl_dev->cpu_buff_conf[cpu_ind].addr[i]);
			bcl_dev->cpu_buff_conf[cpu_ind].wr_update_rqd &= ~bit_mask;
		}
		if (bcl_dev->cpu_buff_conf[cpu_ind].rd_update_rqd & bit_mask) {
			bcl_dev->cpu_buff_conf[cpu_ind].buff[i] =
				__raw_readl(bcl_dev->core_conf[system_source_idx].base_mem +
					    bcl_dev->cpu_buff_conf[cpu_ind].addr[i]);
			bcl_dev->cpu_buff_conf[cpu_ind].rd_update_rqd &= ~bit_mask;
		}
	}

	return NOTIFY_OK;
}

int cpu_buff_read(struct bcl_device *bcl_dev, int idx, unsigned int type, unsigned int *reg)
{
	const enum CPU_CLUSTER cluster = get_cluster_by_source_idx(idx);

	if (cluster < LITTLE_CLUSTER || cluster >= CPU_CLUSTER_MAX)
		return -EINVAL;

	if (type < CPU_BUFF_CON_HEAVY || type >= CPU_BUFF_VALS_MAX)
		return -EINVAL;

	if (cluster == LITTLE_CLUSTER) {
		*reg = __raw_readl(bcl_dev->core_conf[idx].base_mem +
				   bcl_dev->cpu_buff_conf[cluster].addr[type]);
		return 0;
	}

	*reg = bcl_dev->cpu_buff_conf[cluster].buff[type];
	bcl_dev->cpu_buff_conf[cluster].rd_update_rqd |= BIT(type);
	return 0;
}

int cpu_buff_write(struct bcl_device *bcl_dev, int idx, unsigned int type, unsigned int val)
{
	const enum CPU_CLUSTER cluster = get_cluster_by_source_idx(idx);

	if (cluster < LITTLE_CLUSTER || cluster >= CPU_CLUSTER_MAX)
		return -EINVAL;

	if (type < CPU_BUFF_CON_HEAVY || type >= CPU_BUFF_VALS_MAX)
		return -EINVAL;

	if (cluster == LITTLE_CLUSTER) {
		__raw_writel(val, bcl_dev->core_conf[idx].base_mem +
			     bcl_dev->cpu_buff_conf[cluster].addr[type]);
		return 0;
	}

	bcl_dev->cpu_buff_conf[cluster].buff[type] = val;
	bcl_dev->cpu_buff_conf[cluster].wr_update_rqd |= BIT(type);
	return 0;
}

int cpu_sfr_write(struct bcl_device *bcl_dev, int idx, void __iomem *addr, unsigned int value)
{
	mutex_lock(&bcl_dev->cpu_ratio_lock);
	if (!bcl_disable_power(bcl_dev, idx)) {
		mutex_unlock(&bcl_dev->cpu_ratio_lock);
		return -EIO;
	}
	__raw_writel(value, addr);
	bcl_enable_power(bcl_dev, idx);
	mutex_unlock(&bcl_dev->cpu_ratio_lock);
	return 0;
}

int cpu_sfr_read(struct bcl_device *bcl_dev, int idx, void __iomem *addr, unsigned int *reg)
{
	mutex_lock(&bcl_dev->cpu_ratio_lock);
	if (!bcl_disable_power(bcl_dev, idx)) {
		mutex_unlock(&bcl_dev->cpu_ratio_lock);
		return -EIO;
	}
	*reg = __raw_readl(addr);
	bcl_enable_power(bcl_dev, idx);
	mutex_unlock(&bcl_dev->cpu_ratio_lock);

	return 0;
}

bool bcl_is_cluster_on(struct bcl_device *bcl_dev, int cluster)
{
	unsigned int addr, value = 0;

	if (!IS_ENABLED(CONFIG_REGULATOR_S2MPG14))
		return false;
	if (cluster < bcl_dev->cpu_cluster[QOS_CPU2]) {
		addr = CLUSTER1_NONCPU_STATES;
		exynos_pmu_read(addr, &value);
		return value & BIT(4);
	}
	if (cluster == bcl_dev->cpu_cluster[QOS_CPU2]) {
		addr = CLUSTER2_NONCPU_STATES;
		exynos_pmu_read(addr, &value);
		return value & BIT(4);
	}
	return false;
}

bool bcl_is_subsystem_on(struct bcl_device *bcl_dev, enum SUBSYSTEM_SOURCE subsys_src)
{
	unsigned int value;
	unsigned int addr = subsys_to_pmu(subsys_src);

	switch (addr) {
	case PMU_ALIVE_TPU_STATES:
	case PMU_ALIVE_GPU_STATES:
	case PMU_ALIVE_AUR_STATES:
		exynos_pmu_read(addr, &value);
		return !(value & BIT(7));
	case PMU_ALIVE_CPU0_STATES:
		return true;
	case PMU_ALIVE_CPU1_STATES:
		return bcl_is_cluster_on(bcl_dev, bcl_dev->cpu_cluster[QOS_CPU1]);
	case PMU_ALIVE_CPU2_STATES:
		return bcl_is_cluster_on(bcl_dev, bcl_dev->cpu_cluster[QOS_CPU2]);
	}
	return false;
}

bool bcl_disable_power(struct bcl_device *bcl_dev, int system_src)
{
	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG14) || IS_ENABLED(CONFIG_REGULATOR_S2MPG12)) {
		int i;

		if (system_src == CPU1)
			for (i = bcl_dev->cpu_cluster[QOS_CPU1];
			     i < bcl_dev->cpu_cluster[QOS_CPU2]; i++)
				disable_power_mode(i, POWERMODE_TYPE_CLUSTER);
		else if (system_src == CPU2)
			disable_power_mode(bcl_dev->cpu_cluster[QOS_CPU2], POWERMODE_TYPE_CLUSTER);
	}
	return true;
}

bool bcl_enable_power(struct bcl_device *bcl_dev, int system_src)
{
	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG14) || IS_ENABLED(CONFIG_REGULATOR_S2MPG12)) {
		int i;

		if (system_src == CPU1)
			for (i = bcl_dev->cpu_cluster[QOS_CPU1];
			     i < bcl_dev->cpu_cluster[QOS_CPU2]; i++)
				enable_power_mode(i, POWERMODE_TYPE_CLUSTER);
		else if (system_src == CPU2)
			enable_power_mode(bcl_dev->cpu_cluster[QOS_CPU2], POWERMODE_TYPE_CLUSTER);
	}
	return true;
}

int google_bcl_init_notifier(struct bcl_device *bcl_dev)
{
	int ret, i;

	for (i = LITTLE_CLUSTER; i < CPU_CLUSTER_MAX; i++) {
		bcl_dev->cpu_buff_conf[i].addr[CPU_BUFF_CON_HEAVY] =
			(i == LITTLE_CLUSTER) ? CPUCL0_CLKDIVSTEP_CON : CLKDIVSTEP_CON_HEAVY;
		bcl_dev->cpu_buff_conf[i].addr[CPU_BUFF_CON_LIGHT] =
			(i == LITTLE_CLUSTER) ? CPUCL0_CLKDIVSTEP_CON : CLKDIVSTEP_CON_LIGHT;
		bcl_dev->cpu_buff_conf[i].addr[CPU_BUFF_CLKDIVSTEP] = CLKDIVSTEP;
		bcl_dev->cpu_buff_conf[i].addr[CPU_BUFF_VDROOP_FLT] = VDROOP_FLT;
		bcl_dev->cpu_buff_conf[i].addr[CPU_BUFF_CLK_STATS] =
			(i == LITTLE_CLUSTER) ? CPUCL0_CLKDIVSTEP_STAT : CLKDIVSTEP_STAT;
		bcl_dev->cpu_buff_conf[i].rd_update_rqd = BIT(CPU_BUFF_VALS_MAX) - 1;
	}
	bcl_dev->cpu_nb.notifier_call = bcl_dev_cpu_notifier;
	ret = cpu_pm_register_notifier(&bcl_dev->cpu_nb);

	return ret;
}

int google_init_ratio(struct bcl_device *data, enum SUBSYSTEM_SOURCE idx)
{
	void __iomem *addr;

	if (idx >= ARRAY_SIZE(clk_stats_offset) ||
	    idx >= ARRAY_SIZE(subsystem_pmu))
		return -EINVAL;

	/* Ensure BCL driver is initialized */
	if (!smp_load_acquire(&data->initialized))
		return -EINVAL;

	if (idx == CPU1 || idx == CPU2) {
		if (data->core_conf[idx].con_heavy > 0)
			cpu_buff_write(data, idx, CPU_BUFF_CON_HEAVY,
						   data->core_conf[idx].con_heavy);
		if (data->core_conf[idx].con_light > 0)
			cpu_buff_write(data, idx, CPU_BUFF_CON_LIGHT,
						   data->core_conf[idx].con_light);

		return 0;
	}

	if (!bcl_is_subsystem_on(data, subsystem_pmu[idx]))
		return -EIO;

	if (idx < TPU)
		return -EIO;

	if (idx != AUR) {
		addr = data->core_conf[idx].base_mem + CLKDIVSTEP_CON_HEAVY;
		__raw_writel(data->core_conf[idx].con_heavy, addr);
		addr = data->core_conf[idx].base_mem + CLKDIVSTEP_CON_LIGHT;
		__raw_writel(data->core_conf[idx].con_light, addr);
		addr = data->core_conf[idx].base_mem + VDROOP_FLT;
		__raw_writel(data->core_conf[idx].vdroop_flt, addr);
	}

	addr = data->core_conf[idx].base_mem + CLKDIVSTEP;
	__raw_writel(data->core_conf[idx].clkdivstep, addr);
	addr = data->core_conf[idx].base_mem + CLKOUT;
	__raw_writel(data->core_conf[idx].clk_out, addr);
	data->core_conf[idx].clk_stats = __raw_readl(data->core_conf[idx].base_mem +
						     clk_stats_offset[idx]);

	return 0;
}

unsigned int google_get_db(struct bcl_device *data, enum MPMM_SOURCE index)
{
	void __iomem *addr;
	unsigned int reg;

	if (!IS_ENABLED(CONFIG_REGULATOR_S2MPG14))
		return 0;

	/* Ensure BCL driver is initialized */
	if (!smp_load_acquire(&data->initialized))
		return -EINVAL;
	if (!data->sysreg_cpucl0) {
		dev_err(data->device, "Error in sysreg_cpucl0\n");
		return -ENOMEM;
	}

	if (index == MID)
		addr = data->sysreg_cpucl0 + CLUSTER0_MID_DISPBLOCK;
	else if (index == BIG)
		addr = data->sysreg_cpucl0 + CLUSTER0_BIG_DISPBLOCK;
	else
		return -EINVAL;

	mutex_lock(&data->sysreg_lock);
	reg = __raw_readl(addr);
	mutex_unlock(&data->sysreg_lock);

	return reg;
}

int google_set_db(struct bcl_device *data, unsigned int value, enum MPMM_SOURCE index)
{
	void __iomem *addr;

	if (!IS_ENABLED(CONFIG_REGULATOR_S2MPG14))
		return 0;
	/* Ensure BCL driver is initialized */
	if (!smp_load_acquire(&data->initialized))
		return -EINVAL;
	if (!data->sysreg_cpucl0) {
		dev_err(data->device, "Error in sysreg_cpucl0\n");
		return -ENOMEM;
	}

	if (index == MID)
		addr = data->sysreg_cpucl0 + CLUSTER0_MID_DISPBLOCK;
	else if (index == BIG)
		addr = data->sysreg_cpucl0 + CLUSTER0_BIG_DISPBLOCK;
	else
		return -EINVAL;

	mutex_lock(&data->sysreg_lock);
	__raw_writel(value, addr);
	mutex_unlock(&data->sysreg_lock);
	return 0;
}

ssize_t get_clk_div(struct bcl_device *bcl_dev, int idx, char *buf)
{
	unsigned int reg = 0;
	int ret;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case TPU:
	case GPU:
	case AUR:
		return sysfs_emit(buf, "%#x\n", bcl_dev->core_conf[idx].clkdivstep);
	case CPU1:
	case CPU2:
		ret = cpu_buff_read(bcl_dev, idx, CPU_BUFF_CLKDIVSTEP, &reg);
		if (ret < 0)
			return ret;
		break;
	}
	return sysfs_emit(buf, "%#x\n", reg);
}

ssize_t set_clk_div(struct bcl_device *bcl_dev, int idx, const char *buf, size_t size)
{
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 16, &value);
	if (ret)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case TPU:
	case GPU:
	case AUR:
		return size;
	case CPU0:
	case CPU1:
	case CPU2:
		ret = cpu_buff_write(bcl_dev, idx, CPU_BUFF_CLKDIVSTEP, value);
		if (ret < 0)
			return ret;
		break;
	}
	return size;
}

ssize_t get_clk_stats(struct bcl_device *bcl_dev, int idx, char *buf)
{
	unsigned int reg = 0;
	int ret;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case TPU:
	case GPU:
	case AUR:
		return sysfs_emit(buf, "%#x\n", bcl_dev->core_conf[idx].clk_stats);
	case CPU0:
	case CPU1:
	case CPU2:
		ret = cpu_buff_read(bcl_dev, idx, CPU_BUFF_CLK_STATS, &reg);
		if (ret < 0)
			return ret;
		break;
	}
	return sysfs_emit(buf, "%#x\n", reg);
}

ssize_t get_clk_ratio(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx, char *buf, int sub_idx)
{
	unsigned int reg = 0;
	int ret;

	if (!bcl_dev)
		return -EIO;

	switch (idx) {
	case heavy:
		if (sub_idx < GPU) {
			ret = cpu_buff_read(bcl_dev, sub_idx, CPU_BUFF_CON_HEAVY, &reg);
			if (ret < 0)
				return ret;
			break;
		}
		return sysfs_emit(buf, "%#x\n", bcl_dev->core_conf[sub_idx].con_heavy);
	case light:
		if (sub_idx < GPU) {
			ret = cpu_buff_read(bcl_dev, sub_idx, CPU_BUFF_CON_LIGHT, &reg);
			if (ret < 0)
				return ret;
			break;
		}
		return sysfs_emit(buf, "%#x\n", bcl_dev->core_conf[sub_idx].con_light);
	}
	return sysfs_emit(buf, "%#x\n", reg);
}

ssize_t set_clk_ratio(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
		      const char *buf, size_t size, int sub_idx)
{
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 16, &value);
	if (ret)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;

	switch (idx) {
	case heavy:
		if (sub_idx < GPU) {
			ret = cpu_buff_write(bcl_dev, sub_idx, CPU_BUFF_CON_HEAVY, value);
			if (ret < 0)
				return ret;
			break;
		}
		bcl_dev->core_conf[sub_idx].con_heavy = value;
		return size;
	case light:
		if (sub_idx < GPU) {
			ret = cpu_buff_write(bcl_dev, sub_idx, CPU_BUFF_CON_LIGHT, value);
			if (ret < 0)
				return ret;
			break;
		}
		bcl_dev->core_conf[sub_idx].con_light = value;
		return size;
	}
	return size;
}

ssize_t set_hw_mitigation(struct bcl_device *bcl_dev, const char *buf,
			  size_t size)
{
	bool value;
	int ret, i;
	unsigned int reg;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	/* Ensure hw mitigation enabled is read correctly */
	if (smp_load_acquire(&bcl_dev->hw_mitigation_enabled) == value)
		return size;

	/* Ensure hw mitigation is set correctly */
	smp_store_release(&bcl_dev->hw_mitigation_enabled, value);
	if (value) {
		for (i = CPU0; i <= AUR; i++) {
			if (i <= CPU2) {
				ret = cpu_buff_write(
					bcl_dev, i, CPU_BUFF_CLKDIVSTEP,
					bcl_dev->core_conf[i].clkdivstep_last |
						0x1);
				if (ret < 0)
					return ret;
			} else {
				bcl_dev->core_conf[i].clkdivstep =
					bcl_dev->core_conf[i].clkdivstep_last;
			}
		}
	} else {
		for (i = CPU0; i <= AUR; i++) {
			if (i <= CPU2) {
				ret = cpu_buff_read(bcl_dev, i,
						    CPU_BUFF_CLKDIVSTEP, &reg);
				if (ret < 0)
					return ret;
				bcl_dev->core_conf[i].clkdivstep_last = reg;
				ret = cpu_buff_write(bcl_dev, i,
						     CPU_BUFF_CLKDIVSTEP,
						     reg & ~(1 << 0));
				if (ret < 0)
					return ret;
			} else {
				bcl_dev->core_conf[i].clkdivstep_last =
					bcl_dev->core_conf[i].clkdivstep;
				bcl_dev->core_conf[i].clkdivstep &= ~(1 << 0);
			}
		}
	}
	return size;
}

ssize_t set_sw_mitigation(struct bcl_device *bcl_dev, const char *buf, size_t size)
{
	bool value;
	int ret, i;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	/* Ensure sw mitigation enabled is different */
	if (smp_load_acquire(&bcl_dev->sw_mitigation_enabled) == value)
		return size;

	/* Ensure bcl driver is initialized to avoid receiving external calls */
	smp_store_release(&bcl_dev->sw_mitigation_enabled, value);
	if (value) {
		for (i = 0; i < TRIGGERED_SOURCE_MAX; i++)
			if (bcl_dev->zone[i] && i != BATOILO)
				enable_irq(bcl_dev->zone[i]->bcl_irq);
	} else {
		for (i = 0; i < TRIGGERED_SOURCE_MAX; i++)
			if (bcl_dev->zone[i] && i != BATOILO)
				disable_irq(bcl_dev->zone[i]->bcl_irq);
	}
	return size;
}

int google_init_vd_soc(struct bcl_device *bcl_dev)
{
	return 0;
}
