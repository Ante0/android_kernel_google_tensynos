// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/platform_device.h>
#include <drm/drm_modes.h>
#include <drm/display/drm_hdcp_helper.h>

#include "dbg.h"
#include "dptx.h"
#include "regmaps/ctrl_fields.h"
#include "teeif.h"

#define MAX_LINK_FAILURE_RETRY_CNT (5)

/* TODO(jisshin): implement Widevine(DRM) & NS(Secure Layer) side trigger to
 *                reduce hdcp_max_ver for dynamic trigger impl.
 */
static unsigned long hdcp_max_ver = 3;
module_param(hdcp_max_ver, ulong, 0664);
MODULE_PARM_DESC(hdcp_max_ver,
	"support up to specific hdcp version by setting hdcp_max_ver=x");

unsigned long hdcp_delay_ms = 1000;
module_param(hdcp_delay_ms, ulong, 0664);
MODULE_PARM_DESC(hdcp_delay_ms,
	"set number of milliseconds to delay HDCP negotiation after HPD_PLUG");

static ssize_t hdcp2_success_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct dptx *dptx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", dptx->hdcp_dev.hdcp2_success_count);
}
static DEVICE_ATTR_RO(hdcp2_success_count);

static ssize_t hdcp2_fallback_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct dptx *dptx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", dptx->hdcp_dev.hdcp2_fallback_count);
}
static DEVICE_ATTR_RO(hdcp2_fallback_count);

static ssize_t hdcp2_fail_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct dptx *dptx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", dptx->hdcp_dev.hdcp2_fail_count);
}
static DEVICE_ATTR_RO(hdcp2_fail_count);

static ssize_t hdcp1_success_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct dptx *dptx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", dptx->hdcp_dev.hdcp1_success_count);
}
static DEVICE_ATTR_RO(hdcp1_success_count);

static ssize_t hdcp1_fail_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct dptx *dptx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", dptx->hdcp_dev.hdcp1_fail_count);
}
static DEVICE_ATTR_RO(hdcp1_fail_count);

static ssize_t hdcp0_count_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct dptx *dptx = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", dptx->hdcp_dev.hdcp0_count);
}
static DEVICE_ATTR_RO(hdcp0_count);

static ssize_t hdcp_enc_lvl_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct dptx *dptx = dev_get_drvdata(dev);
	uint32_t version = 0;

	int rc = hdcp_tee_check_protection(dptx, &version);

	if (rc) {
		dptx_info(dptx, "failed to check protection (%d)\n", rc);
		return -EIO;
	}

	return sysfs_emit(buf, "%u\n", version);
}
static DEVICE_ATTR_RO(hdcp_enc_lvl);

static struct attribute *hdcp_attrs[] = {
	&dev_attr_hdcp2_success_count.attr,
	&dev_attr_hdcp2_fallback_count.attr,
	&dev_attr_hdcp2_fail_count.attr,
	&dev_attr_hdcp1_success_count.attr,
	&dev_attr_hdcp1_fail_count.attr,
	&dev_attr_hdcp0_count.attr,
	&dev_attr_hdcp_enc_lvl.attr,
	NULL };

static const struct attribute_group hdcp_group = {
	.name = "drm-hdcp",
	.attrs = hdcp_attrs
};

static void hdcp_worker(struct work_struct *work)
{
	int ret = EIO;
	ktime_t delta;
	bool hdcp2_capable = false;
	bool hdcp1_capable = false;

	struct hdcp_device *hdcp_dev =
		container_of(work, struct hdcp_device, hdcp_work.work);
	struct dptx *dptx =
		container_of(hdcp_dev, struct dptx, hdcp_dev);

	enum auth_state state = hdcp_get_auth_state(dptx);

	if (state != HDCP_AUTH_RESET && state != HDCP_AUTH_IDLE) {
		dptx_info(dptx, "HDCP auth is skipped during %s state\n",
			get_auth_state_str(state));
		return;
	}

	if (hdcp_max_ver >= 2) {
		ret = run_hdcp_auth22(dptx);
		if (!ret)
			return;
		hdcp2_capable = (ret != -EOPNOTSUPP);
	} else {
		dptx_info(dptx, "Not trying HDCP22. hdcp_max_ver is %lu\n",
			  hdcp_max_ver);
	}

	if (hdcp_max_ver >= 1) {
		ret = run_hdcp_auth13(dptx);
		if (!ret) {
			hdcp_dev->hdcp2_fallback_count += hdcp2_capable;
			hdcp_dev->hdcp1_success_count += !hdcp2_capable;
			return;
		}
		hdcp1_capable = (ret != -EOPNOTSUPP);
	} else {
		dptx_info(dptx, "Not trying HDCP13. hdcp_max_ver is %lu\n",
			  hdcp_max_ver);
	}

	state = hdcp_get_auth_state(dptx);
	if (state == HDCP_AUTH_RESET || state == HDCP_AUTH_ABORT ||
	    state == HDCP_AUTH_SHUTDOWN) {
		// if aborted, don't count it as a legitimate failure.
		dptx_info(dptx, "HDCP aborted\n");
		return;
	}

	hdcp_dev->hdcp2_fail_count += (hdcp2_capable);
	hdcp_dev->hdcp1_fail_count += (!hdcp2_capable && hdcp1_capable);
	hdcp_dev->hdcp0_count += (!hdcp2_capable && !hdcp1_capable);
}

int handle_cp_irq_set(struct dptx *dptx)
{
	enum auth_state state = hdcp_get_auth_state(dptx);

	switch (state) {
	case HDCP2_AUTH_PROGRESS:
	case HDCP2_AUTH_DONE:
		dptx_info(dptx, "Handling DP_CP_IRQ 22");
		dptx_write_regfield(dptx, dptx->ctrl_fields->field_cp_irq, 1);
		usleep_range(10, 20);
		dptx_write_regfield(dptx, dptx->ctrl_fields->field_cp_irq, 0);
		break;
	case HDCP1_AUTH_PROGRESS:
	case HDCP1_AUTH_DONE:
		dptx_info(dptx, "Handling DP_CP_IRQ 13");
		uint8_t bstatus;
		int ret = dptx_read_bytes_from_dpcd(dptx, DP_AUX_HDCP_BSTATUS,
			&bstatus, 1);
		if (ret) {
			dptx_err(dptx, "dpcd byte read fail (%d)\n", ret);
			return 0;
		}
		if (bstatus & (DP_BSTATUS_LINK_FAILURE | DP_BSTATUS_REAUTH_REQ)) {
			dptx_err(dptx, "Re-auth due to BSTAT(%d)\n", bstatus);
			hdcp_set_auth_state(dptx, HDCP_AUTH_IDLE);
			if (dptx->hdcp_dev.link_failure_cnt < MAX_LINK_FAILURE_RETRY_CNT) {
				dptx->hdcp_dev.link_failure_cnt++;
				schedule_delayed_work(&dptx->hdcp_dev.hdcp_work, 0);
			} else {
				dptx_err(dptx, "Give up on HDCP during this session\n");
			}
		}
		break;
	default:
		dptx_err(dptx, "Unexpected CP_IRQ during state %d\n", state);
		break;
	}

	return 0;
}

void dptx_hdcp_connect(struct dptx *dptx, bool is_dpcd12_plus)
{
	hdcp_tee_connect_info(dptx, true);
	hdcp_set_auth_state(dptx, HDCP_AUTH_RESET);
	dptx->hdcp_dev.connect_time = ktime_get();
	dptx->hdcp_dev.is_dpcd12_plus = is_dpcd12_plus;
	dptx_soft_reset(dptx, DPTX_SRST_CTRL_HDCP);
	schedule_delayed_work(&dptx->hdcp_dev.hdcp_work,
		msecs_to_jiffies(hdcp_delay_ms));
}

void dptx_hdcp_disconnect(struct dptx *dptx)
{
	hdcp_tee_connect_info(dptx, false);
	hdcp_set_auth_state(dptx, HDCP_AUTH_ABORT);
	cancel_delayed_work_sync(&dptx->hdcp_dev.hdcp_work);
	dptx->hdcp_dev.connect_time = 0;
	dptx->hdcp_dev.is_dpcd12_plus = 0;
}

void dptx_hdcp_physical_disconnect(struct dptx *dptx)
{
	dptx->hdcp_dev.link_failure_cnt = 0;
}

int dptx_hdcp_probe(struct dptx *dptx)
{
	hdcp_tee_init(dptx);

	int ret = sysfs_create_group(&dptx->dev->kobj, &hdcp_group);

	if (ret) {
		dptx_info(dptx, "failed to create hdcp sysfs files\n");
		return ret;
	}

	dptx->hdcp_dev.hdcp_auth_state = HDCP_AUTH_ABORT;
	INIT_DELAYED_WORK(&dptx->hdcp_dev.hdcp_work, hdcp_worker);

	dptx_hdcp_auth_state_probe(dptx);
	return 0;
}

void dptx_hdcp_remove(struct dptx *dptx)
{
	dptx_hdcp_disconnect(dptx);
	hdcp_tee_close(dptx);
}

void dptx_hdcp_dump_regs(struct dptx *dptx)
{
	dptx_err(dptx, "HDCP Reg Dump:\n");
	dptx_err(dptx, "  DPTX_VERSION_NUMBER:  0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VERSION_NUMBER));
	dptx_err(dptx, "  DPTX_VERSION_TYPE:    0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_VERSION_TYPE));
	dptx_err(dptx, "  DPTX_CONFIG_REG1:     0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_CONFIG_REG1));
	dptx_err(dptx, "  VSAMPLE_CTRL[0]:      0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], VSAMPLE_CTRL));
	dptx_err(dptx, "  GENERAL_INTERRUPT[0]: 0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], GENERAL_INTERRUPT));
	dptx_err(dptx, "  HDCPCFG:              0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], HDCPCFG));
	dptx_err(dptx, "  HDCPOBS:              0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], HDCPOBS));
	dptx_err(dptx, "  HDCPAPIINTSTAT:       0x%08x\n",
		 dptx_read_reg(dptx, dptx->regs[DPTX], HDCPAPIINTSTAT));
}

