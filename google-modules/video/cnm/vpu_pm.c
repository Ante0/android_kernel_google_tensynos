// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Codec3P video accelerator
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/pm_domain.h>
#include <linux/delay.h>

#include "vpu_pm.h"
#include "wave6_regdefine.h"
#include "vpu_common.h"
#include "vpu_coredump.h"

#define VPU_BUSY_CHECK_TIMEOUT   500
#define VCORES_PSM_STATUS_CURR_STATE_MASK GENMASK(3, 0)
#define VCORES_PSM_STATUS_POWERED_OFF 0x6
#define VCORES_OFF_CHECK_TIMEOUT 50

static const char * const vpu_dbg_reg_names[] = {
	"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
	"R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
	"CR0", "CR1", "ML", "MH", "SP", "LR", "PC", "SR", "SSP"
};

static void dump_vcpu_status(struct vpu_core *core)
{
	unsigned int i;
	unsigned int regVal;

	dev_info(core->dev, "------VCPU CORE STATUS-----\n");

	dev_info(core->dev, "vpu_busy %d cmd_busy %d\n",
		READ_VPU_REGISTER(core, W6_VPU_BUSY_STATUS),
		READ_VPU_REGISTER(core, W6_VPU_CMD_BUSY_STATUS));

	regVal = READ_VPU_REGISTER(core, W6_CMD_QUEUE_FULL_IDC);
	dev_info(core->dev, "QUEUE_FULL_IDC=%#x\n", regVal);
	regVal = READ_VPU_REGISTER(core, W6_CMD_QUEUE_EMPTY_IDC);
	dev_info(core->dev, "RET_EMPTY_IDC=%#x\n", regVal);
	regVal = READ_VPU_REGISTER(core, W6_RET_QUEUE_STATUS);
	dev_info(core->dev, "instanceQueueCount=%d\n", (regVal >> 16) & 0xffff);
	dev_info(core->dev, "reportQueueCount=%d\n", (regVal >>  0) & 0xffff);

	for (i = 0; i < 10; i++) {
		dev_info(core->dev, "PC=%#010x, LR=%#010x\n",
				READ_VPU_REGISTER(core, W6_VCPU_CUR_PC),
				READ_VPU_REGISTER(core, W6_VCPU_CUR_LR));
	}
	/* --------- VCPU register Dump */
	dev_info(core->dev, "[+] VCPU REG Dump\n");
	for (i = 0; i < ARRAY_SIZE(vpu_dbg_reg_names); i++) {
		WRITE_VPU_REGISTER(core, W6_VPU_PDBG_IDX_REG, (1<<9) | (i & 0xff));
		regVal = READ_VPU_REGISTER(core, W6_VPU_PDBG_RDATA_REG);

		if (i < 16) {
			dev_info(core->dev, "%#010x%c", regVal, i % 4 == 3 ? '\n' : '\t');
		} else {
			dev_info(core->dev, "%s: %#010x\n", vpu_dbg_reg_names[i], regVal);
		}
	}
	regVal = READ_VPU_REGISTER(core, 0x010C);
	dev_info(core->dev,  "VCPU Backbone busy flag: %#010x\n", regVal);
	dev_info(core->dev, "[-] VCPU REG Dump\n");
}

static bool vpu_idle(struct vpu_core *core)
{
	return READ_VPU_REGISTER(core, W6_VPU_BUSY_STATUS) == 0 &&
		READ_VPU_REGISTER(core, W6_VPU_CMD_BUSY_STATUS) == 0;
}

static void vpu_wait_idle_fail(struct vpu_core *core)
{
	dev_warn(core->dev, "force skip vpu_idle, reload FW on next power on\n");
	core->need_reload_fw = true;
	dump_vcpu_status(core);

	writel(BIT(6), core->qch_base + core->qchctl_active_mask_0_offset);
}

static int vpu_wait_busy_status(struct vpu_core *core, uint32_t reg)
{
	int i;

	for (i = 0; i < VPU_BUSY_CHECK_TIMEOUT; ++i) {
		if (!READ_VPU_REGISTER(core, reg))
			return 0;
		usleep_range(900, 1100);
	}
	dev_err(core->dev, "failed to wait for reg %#x in %d ms\n", reg, i);
	return -ETIMEDOUT;
}

static void vpu_wait_idle(struct vpu_core *core)
{
	/* According to b/273411152#comment106, vpu_idle needs both
	 * VPU_BUSY_STATUS and VCPU_CMD_BUSY_STATUS to be 0.
	 */
	if (vpu_wait_busy_status(core, W6_VPU_BUSY_STATUS) < 0 ||
	    vpu_wait_busy_status(core, W6_VPU_CMD_BUSY_STATUS) < 0) {
		vpu_wait_idle_fail(core);
	}
}

static void vpu_enable_interrupt(struct vpu_core *core)
{
	WRITE_VPU_REGISTER(core, W6_VPU_VINT_ENABLE, core->restore_regs.vint_enable);
}

static int vpu_sleep_wakeup(struct vpu_core *core, bool is_sleep)
{
	int rc, ret;
	struct vpu_fw_cmd fw_cmd = {0};

	if (is_sleep) {
		rc = vpu_wait_busy_status(core, W6_VPU_CMD_BUSY_STATUS);
		if (rc)
			return rc;

		fw_cmd.cmd = W6_SLEEP_VPU;
		vpu_send_fw_cmd_locked(core, &fw_cmd);

		rc = vpu_wait_busy_status(core, W6_VPU_CMD_BUSY_STATUS);
		if (rc)
			return rc;

		if (!READ_VPU_REGISTER(core, W6_RET_SUCCESS)) {
			ret = READ_VPU_REGISTER(core, W6_RET_FAIL_REASON);

			switch (ret) {
			case WAVE6_SYSERR_VPU_STILL_RUNNING:
				dev_dbg(core->dev, "sleep returns VPU_STILL_RUNNING\n");
				rc = -EBUSY;
				break;
			case WAVE6_SYSERR_INSTRUCTION_ACCESS_VIOLATION:
			case WAVE6_SYSERR_PRIVILEGE_VIOLATION:
			case WAVE6_SYSERR_DATA_ADDR_ALIGNMENT:
			case WAVE6_SYSERR_DATA_ACCESS_VIOLATION:
			case WAVE6_SYSERR_INSTRUCTION_ADDR_ALIGNMENT:
			case WAVE6_SYSERR_UNKNOWN:
				dev_warn(core->dev, "sleep returns exception 0x%x\n", ret);
				rc = -EFAULT;
				break;
			default:
				dev_warn(core->dev, "sleep returns error 0x%x\n", ret);
				rc = -EINVAL;
				break;
			}
		}
	} else {
		vpu_enable_interrupt(core);

		fw_cmd.cmd = W6_WAKEUP_VPU;
		vpu_send_fw_cmd_locked(core, &fw_cmd);

		rc = vpu_wait_busy_status(core, W6_VPU_CMD_BUSY_STATUS);
		if (rc)
			return rc;

		if (!READ_VPU_REGISTER(core, W6_RET_SUCCESS)) {
			ret = READ_VPU_REGISTER(core, W6_RET_FAIL_REASON);
			dev_warn(core->dev, "wakeup command returns error 0x%x\n", ret);
			rc = -EINVAL;
		}
	}

	return rc;
}

static int vpu_wait_interrupt(struct vpu_core *core, uint32_t *reason)
{
	int rc;
	struct vpu_intr_queue *intr_queue = &core->intr_queue;
	uint32_t num;

	/* b/446005383#comment5 this wait can't be interrupted as we need
	 * vpu to stop either by sleep command or waiting for a while here
	 */
	rc = wait_event_timeout(intr_queue->wq,
			kfifo_len(&intr_queue->intr_pending_q),
			msecs_to_jiffies(VPU_BUSY_CHECK_TIMEOUT));
	if (!rc) {
		dev_warn(core->dev, "timed out waiting for interrupt\n");
		return -ETIMEDOUT;
	}

	num = kfifo_out_spinlocked(&intr_queue->intr_pending_q, reason,
						sizeof(u32), &intr_queue->kfifo_lock);

	if (num <= 0) {
		dev_warn(core->dev, "no entry in fifo queue\n");
		*reason = 0;
		return -EINTR;
	}

	return 0;
}

static void vpu_save_restore_cmd_regs(struct vpu_core *core, bool is_save)
{
	u32 i;

	if (is_save) {
		for (i = 0; i < ARRAY_SIZE(core->restore_regs.cmd_regs); ++i) {
			u32 offset = W6_CMD_REG_BASE + (i * 4);

			core->restore_regs.cmd_regs[i] = READ_VPU_REGISTER(core, offset);
		}
		/* restore VINT_ENABLE in vpu_enable_interrupt */
		core->restore_regs.vint_enable = READ_VPU_REGISTER(core, W6_VPU_VINT_ENABLE);
	} else {
		for (i = 0; i < ARRAY_SIZE(core->restore_regs.cmd_regs); ++i) {
			u32 offset = W6_CMD_REG_BASE + (i * 4);

			WRITE_VPU_REGISTER(core, offset, core->restore_regs.cmd_regs[i]);
		}
	}
}

static int vpu_sleep(struct vpu_core *core)
{
	int rc;
	uint32_t reason;

	/* Expect VPU_STILL_RUNNING (mapped to -EBUSY), for the first sleep cmd */
	rc = vpu_sleep_wakeup(core, true);
	if (rc != -EBUSY)
		goto out;

	/* Expect SLEEP_VPU_IDLE interrupt */
	rc = vpu_wait_interrupt(core, &reason);
	if (rc)
		goto out;

	if (reason != BIT(INT_WAVE6_SLEEP_VPU_IDLE)) {
		dev_warn(core->dev, "1st sleep command failed, reason %d\n", reason);
		rc = -EAGAIN;
		goto out;
	}

	/* Expect success for the second sleep cmd */
	rc = vpu_sleep_wakeup(core, true);
	if (rc != 0)
		goto out;

	/* Expect SLEEP_VPU_SUCCESS interrupt */
	rc = vpu_wait_interrupt(core, &reason);
	if (rc)
		goto out;

	if (reason != BIT(INT_WAVE6_SLEEP_VPU_SUCCESS)) {
		dev_warn(core->dev, "2nd sleep command failed, reason %d\n", reason);
		rc = -EAGAIN;
		goto out;
	}

	vpu_wait_idle(core);
	vpu_save_restore_cmd_regs(core, true);
out:
	return rc;
}

static int vpu_wakeup(struct vpu_core *core)
{
	vpu_save_restore_cmd_regs(core, false);
	return vpu_sleep_wakeup(core, false);
}

int vpu_runtime_suspend(struct device *dev)
{
	return 0;
}

int vpu_runtime_resume(struct device *dev)
{
	return 0;
}

static void vpu_hw_reset(struct vpu_core *core)
{
	reset_control_assert(core->vpu_reset);
	reset_control_deassert(core->vpu_reset);
}

int vpu_pm_power_on(struct vpu_core *core)
{
	int rc = 0;

	mutex_lock(&core->lock);
	if (core->power_status != POWER_OFF_RELEASED) {
		dev_warn(core->dev, "not powered off\n");
		rc = -EBUSY;
		goto out;
	}

	if (core->pd_dev) {
		rc = pm_runtime_resume_and_get(core->pd_dev);
		if (rc) {
			dev_err(core->dev, "failed to power on %d", rc);
			goto out;
		}
	}

	if (IS_ENABLED(CONFIG_CODEC3P_VPU_RESET))
		vpu_hw_reset(core);

	core->power_status = POWER_ON;
	dev_info(core->dev, "powered on\n");

out:
	vpu_update_system_state(core);
	mutex_unlock(&core->lock);
	return rc;
}

static bool vpu_check_vcores_off(struct vpu_core *core)
{
	uint32_t val;
	uint32_t curr_state;

	/* Reference step 1 of VCPU FW hang scenario in MBU SSWRP_CODEC3P HPG */
	val = readl(core->lpcm_base + NS_PSM_C3P_CORE_FSM_STATUS_OFFSET);
	curr_state = FIELD_GET(VCORES_PSM_STATUS_CURR_STATE_MASK, val);
	return (curr_state == VCORES_PSM_STATUS_POWERED_OFF);
}

static bool vpu_wait_vcores_off(struct vpu_core *core)
{
	int i;

	for (i = 0; i < VCORES_OFF_CHECK_TIMEOUT; ++i) {
		if (vpu_check_vcores_off(core))
			return true;
		usleep_range(900, 1100);
	}
	return false;
}

static bool vpu_force_vcores_off(struct vpu_core *core)
{
	bool ret;

	/* Reference steps 2-5 of VCPU FW hang scenario in MBU SSWRP_CODEC3P HPG */
	writel(BIT(0), core->lpcm_base + C3P_CONTROL_INPUT_SW_EN_OFFSET);
	writel(BIT(0) | BIT(1), core->lpcm_base + C3P_CONTROL_INPUT_OFFSET);
	ret = vpu_wait_vcores_off(core);
	return ret;
}

int vpu_pm_power_off(struct vpu_core *core)
{
	int rc = 0;

	mutex_lock(&core->lock);
	if (core->power_status == POWER_OFF_RELEASED) {
		dev_warn(core->dev, "already powered off %d\n", core->power_status);
		goto out;
	} else if (core->power_status == POWER_OFF_SLEEP) {
		core->power_status = POWER_OFF_RELEASED;
		dev_info(core->dev, "suspended -> power off\n");
		goto out;
	}

	/* It should be fine to send SLEEP CMD to vpu regardless but we see SMMU fault (b/411597624)
	 * if sending SLEEP CMD in normal power-off case.
	 * Normal means vpu is only powered off after all vpu instances are closed
	 */
	if (!vpu_idle(core)) {
		dev_info(core->dev, "power off without vpu idle\n");
		rc = vpu_sleep(core);
		if (rc) {
			if (rc == -EFAULT) {
				/* b/439991064#comment7. In case of vCPU exception,
				 * sleep cmd is ignored and we need to explicitly wait for idle
				 * to make sure vCore is not active anymore
				 */
				vpu_wait_idle(core);
			}
			vpu_wait_idle_fail(core);
		}
	}

	if (IS_ENABLED(CONFIG_CODEC3P_VCORE_APG)) {
		if (!vpu_wait_vcores_off(core)) {
			dev_warn(core->dev, "power off while vcores on. forcing them off\n");
			vpu_internal_coredump(core, "force power off VCORE");
			if (!vpu_force_vcores_off(core))
				dev_err(core->dev, "could not force vcores off\n");
		}
	}

	if (core->pd_dev) {
		rc = pm_runtime_put_sync(core->pd_dev);
		if (rc) {
			dev_err(core->dev, "failed to power off %d\n", rc);
			goto out;
		}
	}

	core->power_status = POWER_OFF_RELEASED;
	dev_info(core->dev, "powered off\n");

out:
	vpu_update_system_state(core);
	mutex_unlock(&core->lock);
	return rc;
}

int vpu_pm_suspend(struct device *dev)
{
	int rc = 0;
	struct vpu_core *core = platform_get_drvdata(to_platform_device(dev));

	mutex_lock(&core->lock);
	if (core->power_status != POWER_ON)
		goto out;

	if (vpu_sleep(core)) {
		rc = -EAGAIN;
		goto out;
	}
	core->vpu_power_cycled = false;

	if (IS_ENABLED(CONFIG_CODEC3P_VCORE_APG)) {
		if (!vpu_wait_vcores_off(core)) {
			dev_warn(core->dev, "suspend while vcores on. forcing them off\n");
			vpu_internal_coredump(core, "force power off VCORE");
			if (!vpu_force_vcores_off(core))
				dev_err(core->dev, "could not force vcores off\n");
		}
	}

	if (core->pd_dev) {
		rc = pm_runtime_put_sync(core->pd_dev);
		if (rc) {
			dev_err(core->dev, "failed to power off %d\n", rc);
			goto out;
		}
	}

	core->power_status = POWER_OFF_SLEEP;
out:
	vpu_update_system_state(core);
	mutex_unlock(&core->lock);
	if (!rc)
		dev_info_ratelimited(core->dev, "suspended\n");
	return rc;
}

int vpu_pm_resume(struct device *dev)
{
	int rc = 0;
	struct vpu_core *core = platform_get_drvdata(to_platform_device(dev));

	mutex_lock(&core->lock);
	if (core->user_idle)
		goto out;

	if (core->power_status != POWER_OFF_SLEEP) {
		if (core->power_status == POWER_ON)
			dev_warn(core->dev, "already powered on");
		goto out;
	}

	if (core->pd_dev) {
		rc = pm_runtime_resume_and_get(core->pd_dev);
		if (rc) {
			dev_err(core->dev, "failed to power on %d", rc);
			goto out;
		}
	}

	/* TODO: recover the clock if needed */
	if (core->vpu_power_cycled) {
		if (vpu_wakeup(core)) {
			dump_vcpu_status(core);
			vpu_internal_coredump(core, "wakeup VPU fails");
			/* wakeup fails, but we don't power off here as it might fail
			 * the host should trigger SSR to reset VPU HW in this case
			 */
			dev_warn(core->dev,
				"Ignore vpu wakeup error, expect the following command timeout to trigger SSR\n");
		}
	} else if (IS_ENABLED(CONFIG_CODEC3P_VCORE_APG)) {
		writel(0, core->lpcm_base + C3P_CONTROL_INPUT_SW_EN_OFFSET);
	}

	core->power_status = POWER_ON;

out:
	vpu_update_system_state(core);
	mutex_unlock(&core->lock);
	if (!rc)
		dev_info_ratelimited(core->dev, "resumed\n");
	return rc;
}

static int vpu_pd_notifier(struct notifier_block *nb,
			   unsigned long action, void *data)
{
	struct vpu_core *core = container_of(nb, struct vpu_core, genpd_nb);

	if (action == GENPD_NOTIFY_OFF) {
		core->vpu_power_cycled = true;
		vpu_update_system_state(core);
	}

	return NOTIFY_OK;
}

int vpu_pm_init(struct vpu_core *core)
{
	struct device *pd_dev;
	int rc;

	if (!IS_ENABLED(CONFIG_VPU_POWER_DOMAIN)) {
		dev_info(core->dev, "power domain disabled via Kconfig\n");
		core->pd_dev = NULL;
		return 0;
	}

	core->genpd_nb.notifier_call = vpu_pd_notifier;

	/* There will be multiple pd-domains in dtsi even if we only use one (c3p_core),
	 * so pd-domain is powered off by default
	 */
	pd_dev = dev_pm_domain_attach_by_id(core->dev, 0);
	if (pd_dev == NULL) {
		pr_err("pm domain not specified\n");
		return -EINVAL;
	} else if (IS_ERR(pd_dev)) {
		pr_err("failed to attach power domain\n");
		return PTR_ERR(pd_dev);
	}

	rc = dev_pm_genpd_add_notifier(pd_dev, &core->genpd_nb);
	if (rc) {
		dev_pm_domain_detach(pd_dev, true);
		return rc;
	}
	core->pd_dev = pd_dev;

	return 0;
}

void vpu_pm_deinit(struct vpu_core *core)
{
	if (!core->pd_dev)
		return;

	dev_pm_genpd_remove_notifier(core->pd_dev);
	dev_pm_domain_detach(core->pd_dev, true);
}
