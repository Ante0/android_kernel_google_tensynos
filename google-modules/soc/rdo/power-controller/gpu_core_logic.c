// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2023 Google LLC */

#include "gpu_core_logic.h"

#include <linux/iopoll.h>

#define GPU_CONTROL_RES_NAME "gpu_control"
#define PVTC_REG_RES_NAME "pvtc"
#define PVTC_PWROFF_REG_RES_NAME "pvtc_pwroff"
#define DTS_RES_NAME "dts"

#define GPU_CONTROL_EMULATION_TIMEOUT_MULTIPLIER 30

#define GPU_CONTROL_POWER_OFF_ACK_BITMASK BIT(0)
#define GPU_CONTROL_POWER_GATE_ACK_BITMASK BIT(1)
#define GPU_CONTROL_ACK_BITMASK \
	(GPU_CONTROL_POWER_OFF_ACK_BITMASK | GPU_CONTROL_POWER_GATE_ACK_BITMASK)
#define GPU_CONTROL_ACK_TIMEOUT_US 10000
/*
 * The power operation is on the UI hotpath, so avoid yielding the CPU
 * just to immediately need scheduling again, esp as the hw power
 * operation has an upper bound shorter than the rescheduling overhead.
 */
#define GPU_CONTROL_UPDATE_DELAY_US 0
#define GPU_CONTROL_POWER_GATE_ON_VALUE 0x0
#define GPU_CONTROL_POWER_GATE_OFF_VALUE 0x8

#define PVTC_CLK_SYNTH_REG(pvtc_reg) ((pvtc_reg) + 0x10)
#define PVTC_CLK_SYNTH_EN BIT(24)
#define PVTC_CLK_SYNTH_STROBE GENMASK(19, 16)
#define PVTC_CLK_SYNTH_HI GENMASK(15, 8)
#define PVTC_CLK_SYNTH_LO GENMASK(7, 0)
/* Suggested value from TMSS hardware programming guide */
#define GPU_V1_PVTC_CLK_SYNTH_VAL \
	(FIELD_PREP(PVTC_CLK_SYNTH_EN, 1) | \
	 FIELD_PREP(PVTC_CLK_SYNTH_HI, 2) | \
	 FIELD_PREP(PVTC_CLK_SYNTH_LO, 2))

#define PVTC_SDIF_DISABLE_REG(pvtc_reg) ((pvtc_reg) + 0x20)
#define PVTC_SDIF_DISABLE_MASK GENMASK(6, 0)
/* GPU has a single sensor instance to enable */
#define GPU_PVTC_SDIF_DISABLE_VAL (~BIT(0) & PVTC_SDIF_DISABLE_MASK)

#define PVTC_SDIF_STATUS_REG(pvtc_reg) ((pvtc_reg) + 0x24)
#define PVTC_SDIF_STATUS_BUSY_MASK BIT(0)
#define PVTC_SDIF_IDLE_TIMEOUT_US 1000

#define PVTC_SDIF_REG(pvtc_reg) ((pvtc_reg) + 0x28)
#define PVTC_SDIF_DATA GENMASK(23,  0)
#define PVTC_SDIF_ADDR GENMASK(26, 24)
#define PVTC_SDIF_WRN BIT(27)
#define PVTC_SDIF_WRN_READ 0
#define PVTC_SDIF_WRN_WRITE 1
#define PVTC_SDIF_PROG BIT(31)
#define PVTC_SDIF_CMD(addr, data) \
	(FIELD_PREP(PVTC_SDIF_PROG, 1) | \
	 FIELD_PREP(PVTC_SDIF_WRN, PVTC_SDIF_WRN_WRITE) | \
	 FIELD_PREP(PVTC_SDIF_ADDR, (addr)) |\
	 FIELD_PREP(PVTC_SDIF_DATA, (data)))

#define PVTC_SET_DTS_RUN_MODE_ADDR 0x1
#define PVTC_DTS_RUN_MODE_CONVERSION 0xF0000

#define PVTC_SET_DTS_POLLING_SEQUENCE_ADDR 0x4
/* GPU has three remote temp probes to poll */
#define PVTC_GPU_DTS_POLLING_SEQUENCE 0x7

#define PVTC_SET_DTS_MODE_ADDR 0x0
#define PVTC_DTS_MODE_STOP 0x1
#define PVTC_DTS_MODE_CONTINUOUS_POLL 0x508

#define PVTC_SET_SDA_TIMER_ADDR 0x5
/* GPU SDA timer configures 512 clock cycle delay */
#define PVTC_GPU_SDA_TIMER_DELAY 0x200

#define PVTC_PWR_STATUS_REG(pvtc_reg) ((pvtc_reg) + 0x23C)
#define PVTC_PWR_STATUS_DELAY_US 10
#define PVTC_PWR_STATUS_TIMEOUT_US 1000

#define DTS_PWRSWITCH_CONTROL_REG(dts_reg) (dts_reg)
#define DTS_ISO_EN_REG(dts_reg) ((dts_reg) + 0x04)
#define DTS_PWRSTATE_REG(dts_reg) ((dts_reg) + 0x08)

#define PVTC_PWROFF_REQ_REG(pvtc_reg) (pvtc_reg)
#define PVTC_PWROFF_ACK_REG(pvtc_reg) ((pvtc_reg) + 0x04)
#define PVTC_PWROFF_STATUS_TIMEOUT_US 1000

/* Suggested value from TMSS hardware programming guide */
#define GPU_V2_PVTC_CLK_SYNTH_VAL \
	(FIELD_PREP(PVTC_CLK_SYNTH_EN, 1) | \
	 FIELD_PREP(PVTC_CLK_SYNTH_HI, 1) | \
	 FIELD_PREP(PVTC_CLK_SYNTH_LO, 1) | \
	 FIELD_PREP(PVTC_CLK_SYNTH_STROBE, 1))

#define DTS_PWRSWITCH_CONTROL_ON_VAL 0x1
#define DTS_ISO_EN_DISABLE_VAL 0x0
#define DTS_PWRSTATE_ON_VAL 0x1

static int update_gpu_control_reg(struct gpu_core_logic_pd *pd, u32 write_val)
{
	int ret;

	writel(write_val, pd->gpu_control_reg);
	ret = readl_poll_timeout
		(pd->gpu_control_reg, pd->gpu_control_val,
		pd->gpu_control_val & GPU_CONTROL_ACK_BITMASK,
		GPU_CONTROL_UPDATE_DELAY_US, pd->gpu_control_ack_timeout_us);

	return ret;
}

static int pvtc_wait_sdif_idle(void __iomem *pvtc_reg)
{
	u32 sdif_status;

	return readl_poll_timeout(PVTC_SDIF_STATUS_REG(pvtc_reg),
		sdif_status, (sdif_status & PVTC_SDIF_STATUS_BUSY_MASK) == 0,
		 /*delay_us=*/0, PVTC_SDIF_IDLE_TIMEOUT_US);
}

static int pvtc_write_sdif_cmd(struct device *dev, void __iomem *pvtc_reg,
			       u8 addr, u32 data)
{
	u32 cmd = PVTC_SDIF_CMD(addr, data);
	int ret;

	writel(cmd, PVTC_SDIF_REG(pvtc_reg));
	ret = pvtc_wait_sdif_idle(pvtc_reg);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "timeout waiting for pvtc sdif idle: cmd=0x%08x", cmd);
	return ret;
}

static int set_pvtc_power_state(struct device *dev, void __iomem *pvtc_reg, bool state)
{
	int ret;
	u32 val;
	u32 data = (state ? 0x0 : 0x1);

	writel(data, PVTC_PWROFF_REQ_REG(pvtc_reg));
	ret = readl_poll_timeout(PVTC_PWROFF_ACK_REG(pvtc_reg), val, (val == data),
				0, PVTC_PWROFF_STATUS_TIMEOUT_US);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "timeout waiting for pvtc_pwroff_ack: data=0x%08x", data);

	return ret;
}

static int enable_pvtc_sdif(struct device *dev, struct gpu_core_logic_pd *pd)
{
	int ret;
	u32 pvtc_pwr_status;

	writel(GPU_PVTC_SDIF_DISABLE_VAL, PVTC_SDIF_DISABLE_REG(pd->pvtc_reg));

	/*
	 * Confirm SDA power status
	 */
	ret = readl_poll_timeout(PVTC_PWR_STATUS_REG(pd->pvtc_reg),
			pvtc_pwr_status, pvtc_pwr_status == 1,
			PVTC_PWR_STATUS_DELAY_US, PVTC_PWR_STATUS_TIMEOUT_US);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "timeout waiting for gpu pvtc power on");

	return ret;
}

static int power_on_seq_v1(struct device *dev, struct gpu_core_logic_pd *pd)
{
	int ret;

	/*
	 * Configure and enable DTS (thermal sensor) polling
	 */
	if (pd->pvtc_reg) {
		writel(GPU_V1_PVTC_CLK_SYNTH_VAL, PVTC_CLK_SYNTH_REG(pd->pvtc_reg));
		ret = enable_pvtc_sdif(dev, pd);
		if (ret)
			return ret;
		ret = pvtc_wait_sdif_idle(pd->pvtc_reg);
		if (ret == -ETIMEDOUT) {
			dev_err(dev, "timeout waiting for gpu pvtc sdif idle after power on");
			return ret;
		}
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
					  PVTC_SET_DTS_RUN_MODE_ADDR,
					  PVTC_DTS_RUN_MODE_CONVERSION);
		if (ret)
			return ret;
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
					  PVTC_SET_DTS_POLLING_SEQUENCE_ADDR,
					  PVTC_GPU_DTS_POLLING_SEQUENCE);
		if (ret)
			return ret;
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
					  PVTC_SET_DTS_MODE_ADDR,
					  PVTC_DTS_MODE_CONTINUOUS_POLL);
		if (ret)
			return ret;
	}
	return 0;
}

static int power_on_seq_v2(struct device *dev, struct gpu_core_logic_pd *pd)
{
	int ret;

	if (pd->dts_reg) {
		writel(DTS_PWRSWITCH_CONTROL_ON_VAL, DTS_PWRSWITCH_CONTROL_REG(pd->dts_reg));
		writel(DTS_ISO_EN_DISABLE_VAL, DTS_ISO_EN_REG(pd->dts_reg));
	}
	if ((pd->gpu_control_val & GPU_CONTROL_POWER_OFF_ACK_BITMASK) && pd->pvtc_reg) {
		/*
		 * This sequence initializes TMSS. It should be executed for the
		 * first power up of gpu core-logic pd after GPU SSWRP powers on.
		 */
		writel(GPU_V2_PVTC_CLK_SYNTH_VAL, PVTC_CLK_SYNTH_REG(pd->pvtc_reg));
		ret = enable_pvtc_sdif(dev, pd);
		if (ret)
			return ret;
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
					  PVTC_SET_DTS_RUN_MODE_ADDR,
					  PVTC_DTS_RUN_MODE_CONVERSION);
		if (ret)
			return ret;
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
					  PVTC_SET_DTS_POLLING_SEQUENCE_ADDR,
					  PVTC_GPU_DTS_POLLING_SEQUENCE);
		if (ret)
			return ret;
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
					  PVTC_SET_SDA_TIMER_ADDR,
					  PVTC_GPU_SDA_TIMER_DELAY);
		if (ret)
			return ret;
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
					  PVTC_SET_DTS_MODE_ADDR,
					  PVTC_DTS_MODE_CONTINUOUS_POLL);
		if (ret)
			return ret;
	} else if ((pd->gpu_control_val & GPU_CONTROL_POWER_GATE_ACK_BITMASK)
					&& pd->pvtc_pwroff_reg) {
		/*
		 * This sequence should be executed on subsequent gpu core-logic power-on.
		 */
		ret = set_pvtc_power_state(dev, pd->pvtc_pwroff_reg, true);
		if (ret)
			return ret;
	}
	return 0;
}

int gpu_core_logic_on(struct device *dev, struct gpu_core_logic_pd *pd)
{
	int ret;

	/*
	 * Tell GPU LPCM to power on the GPU IP domain.
	 * All the registers configured below are in that domain, so this must
	 * be the first step.
	 */
	ret = update_gpu_control_reg(pd, GPU_CONTROL_POWER_GATE_ON_VALUE);
	if (ret) {
		dev_err(dev, "failed to power on gpu: %d\n", ret);
		return ret;
	}

	switch (pd->gpu_dts_version) {
	case GPU_DTS_VERSION_1:
		ret = power_on_seq_v1(dev, pd);
		break;
	case GPU_DTS_VERSION_2:
		ret = power_on_seq_v2(dev, pd);
		break;
	case GPU_DTS_VERSION_3:
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		gpu_core_logic_off(dev, pd);

	return ret;
}

static int power_off_seq_v1(struct device *dev, struct gpu_core_logic_pd *pd)
{
	int ret;

	if (pd->pvtc_reg) {
		/*
		 * Drop errors - we'll want to power-gate even if something
		 * is wrong with PVTC.
		 */
		ret = pvtc_write_sdif_cmd(dev, pd->pvtc_reg,
				PVTC_SET_DTS_MODE_ADDR, PVTC_DTS_MODE_STOP);
		if (ret)
			return ret;
		writel(PVTC_SDIF_DISABLE_MASK, PVTC_SDIF_DISABLE_REG(pd->pvtc_reg));
	}
	return 0;
}

static int power_off_seq_v2(struct device *dev, struct gpu_core_logic_pd *pd)
{
	int ret;

	if (pd->pvtc_pwroff_reg) {
		ret = set_pvtc_power_state(dev, pd->pvtc_pwroff_reg, false);
		if (ret)
			return ret;
	}
	if (pd->dts_reg)
		writel(DTS_PWRSTATE_ON_VAL, DTS_PWRSTATE_REG(pd->dts_reg));

	return 0;
}

int gpu_core_logic_off(struct device *dev, struct gpu_core_logic_pd *pd)
{
	int ret = 0;

	switch (pd->gpu_dts_version) {
	case GPU_DTS_VERSION_1:
		ret = power_off_seq_v1(dev, pd);
		break;
	case GPU_DTS_VERSION_2:
		ret = power_off_seq_v2(dev, pd);
		break;
	case GPU_DTS_VERSION_3:
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret)
		return ret;

	ret = update_gpu_control_reg(pd, GPU_CONTROL_POWER_GATE_OFF_VALUE);
	if (ret)
		dev_err(dev, "failed to power off gpu: %d\n", ret);

	return ret;
}

static void __iomem *ioremap_byname(struct device *dev, struct device_node *np, const char *name)
{
	int reg_idx;

	reg_idx = of_property_match_string(np, "reg-names", name);
	if (reg_idx < 0)
		return ERR_PTR(reg_idx);
	return devm_of_iomap(dev, np, reg_idx, NULL);
}

int gpu_core_logic_init(struct device *dev, struct device_node *np, struct gpu_core_logic_pd *pd)
{
	pd->gpu_control_reg = ioremap_byname(dev, np, GPU_CONTROL_RES_NAME);
	if (IS_ERR(pd->gpu_control_reg)) {
		dev_err(dev, "ioremap failed for %s\n", GPU_CONTROL_RES_NAME);
		return PTR_ERR(pd->gpu_control_reg);
	}

	/*
	 * Only the gpu_control reg above is mandatory.
	 *
	 * If a platform doesn't have one of the remaining regs, it should omit it from the
	 * devicetree. If present in the devicetree, though, we'll treat a failure to map it
	 * as fatal.
	 */

	pd->pvtc_reg = ioremap_byname(dev, np, PVTC_REG_RES_NAME);
	if (IS_ERR(pd->pvtc_reg)) {
		if (PTR_ERR(pd->pvtc_reg) == -ENODATA) {
			pd->pvtc_reg = NULL;
		} else {
			dev_err(dev, "ioremap failed for %s\n", PVTC_REG_RES_NAME);
			return PTR_ERR(pd->pvtc_reg);
		}
	}

	pd->pvtc_pwroff_reg = ioremap_byname(dev, np, PVTC_PWROFF_REG_RES_NAME);
	if (IS_ERR(pd->pvtc_pwroff_reg)) {
		if (PTR_ERR(pd->pvtc_pwroff_reg) == -ENODATA) {
			pd->pvtc_pwroff_reg = NULL;
		} else {
			dev_err(dev, "ioremap failed for %s\n", PVTC_PWROFF_REG_RES_NAME);
			return PTR_ERR(pd->pvtc_pwroff_reg);
		}
	}

	pd->dts_reg = ioremap_byname(dev, np, DTS_RES_NAME);
	if (IS_ERR(pd->dts_reg)) {
		if (PTR_ERR(pd->dts_reg) == -ENODATA) {
			pd->dts_reg = NULL;
		} else {
			dev_err(dev, "ioremap failed for %s\n", DTS_RES_NAME);
			return PTR_ERR(pd->dts_reg);
		}
	}

	pd->gpu_control_ack_timeout_us = GPU_CONTROL_ACK_TIMEOUT_US;
	if (of_property_present(dev->of_node, "in_emulation"))
		pd->gpu_control_ack_timeout_us *= GPU_CONTROL_EMULATION_TIMEOUT_MULTIPLIER;

	return 0;
}
