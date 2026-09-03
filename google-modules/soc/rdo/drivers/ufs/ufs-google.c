// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
 */

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <scsi/scsi_cmnd.h>
#include <trace/hooks/ufshcd.h>
#include <ufs/ufshcd.h>

#include <ip-idle-notifier/google_ip_idle_notifier.h>

#ifndef HAVE_UFSHCD_RPM_GET_SYNC
#include <drivers/ufs/core/ufshcd-priv.h>
#endif
#include <drivers/ufs/host/ufshcd-pltfrm.h>

#include "ufs-google-dbg.h"
#include "ufs-google-platform.h"
#include "ufs-google-priv.h"
#include "ufs-google.h"
#include "ufs-pixel-crypto.h"
#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
#include "ufs-pixel.h"
#endif

#define CREATE_TRACE_POINTS
#include "ufs-google-trace.h"
#undef CREATE_TRACE_POINTS

#define MCQ_CFG_n(r, i)		((r) + MCQ_QCFG_SIZE * (i))
#define MCQ_OPR_OFFSET_n(p, i)	\
	(hba->mcq_opr[(p)].offset + hba->mcq_opr[(p)].stride * (i))

#define FIPS_140_TIMEOUT_US 400
#define FIPS_140_DELAY_US 10
#define PHY_STATUS_TIMEOUT_US 570000
#define PHY_STATUS_DELAY_US 190000
#define UIC_COMMAND_POLL_TIMEOUT_US 4000000
#define UIC_COMMAND_DELAY_US 10
#define CPORT_CONNECTION_TIMEOUT_US 50000
#define CPORT_CONNECTION_DELAY_US 100

#define RESETB_DELAY_MIN_US 200
#define RESETB_DELAY_MAX_US 300
#define PWR_EN_DELAY_MIN_US 1500
#define PWR_EN_DELAY_MAX_US 2000
#define PWR_RESET_DELAY_MIN_US 8000
#define PD_RESET_DELAY_US (10 * 1000)
#define REFCLK_DELAY_MIN_US 300
#define REFCLK_DELAY_MAX_US 310
#define GPIO_LSS_DELAY_MIN_US 100
#define GPIO_LSS_DELAY_MAX_US 110
#define LOW_POWER_DELAY_MS 4
#define UFS_SLEEP_DELAY_MS 1000

#define PSM_STATUS_VALID_STATE_MASK 0x1F
#define PSM_STATUS_VALID_STATE_ON 0x10
#define PSM_STATUS_VALID_STATE_PG 0x11
#define PSM_STATUS_VALID_STATE_PREP 0x12
#define PSM_STATUS_VALID_STATE_OFF 0x13
#define PSM_POLL_DELAY_US 10
#define PSM_POLL_TIMEOUT_US 10000
#define SW_H8_ENTRY_ATTEMPTS 5

#define MAX_UFS_HOSTS 2
static struct ufs_google_host *ufs_host_backup[MAX_UFS_HOSTS];
static atomic_t ufs_host_index;
static char *ufs_event_type_str[] = {
	/* enum ufs_event_type */
	[UFS_EVT_PA_ERR] = "UFS_EVT_PA_ERR",
	[UFS_EVT_DL_ERR] = "UFS_EVT_DL_ERR",
	[UFS_EVT_NL_ERR] = "UFS_EVT_NL_ERR",
	[UFS_EVT_TL_ERR] = "UFS_EVT_TL_ERR",
	[UFS_EVT_DME_ERR] = "UFS_EVT_DME_ERR",
	[UFS_EVT_AUTO_HIBERN8_ERR] = "UFS_EVT_AUTO_HIBERN8_ERR",
	[UFS_EVT_FATAL_ERR] = "UFS_EVT_FATAL_ERR",
	[UFS_EVT_LINK_STARTUP_FAIL] = "UFS_EVT_LINK_STARTUP_FAIL",
	[UFS_EVT_RESUME_ERR] = "UFS_EVT_RESUME_ERR",
	[UFS_EVT_SUSPEND_ERR] = "UFS_EVT_SUSPEND_ERR",
	[UFS_EVT_WL_SUSP_ERR] = "UFS_EVT_WL_SUSP_ERR",
	[UFS_EVT_WL_RES_ERR] = "UFS_EVT_WL_RES_ERR",
	[UFS_EVT_DEV_RESET] = "UFS_EVT_DEV_RESET",
	[UFS_EVT_HOST_RESET] = "UFS_EVT_HOST_RESET",
	[UFS_EVT_ABORT] = "UFS_EVT_ABORT",
};
#define GEVENT_HBA_ERROR "UFS_EVT_HBA_ERROR"
#define GEVENT_SCSI_TIMEOUT "UFS_EVT_SCSI_TIMEOUT"

static inline void ufs_auto_hibern8_update(struct ufs_hba *hba, bool on)
{
	if (!ufshcd_is_auto_hibern8_supported(hba))
		return;

	ufshcd_writel(hba, on ? hba->ahit : 0, REG_AUTO_HIBERNATE_IDLE_TIMER);
}

/*
 * ufs_clk_switch - Generic helper to enable/disable a clock.
 * @host: The UFS Google host structure.
 * @clk: The clock resource to operate on (e.g., host->deep_h8_clk).
 * @enable: True to enable the clock, false to disable.
 * @cap: The specific GCAP_RSC_... capability flag required for this clock.
 */
static inline void ufs_clk_switch(struct ufs_google_host *host, struct clk *clk,
				  bool enable, u64 cap)
{
	if (!(host->caps & cap))
		return;

	if (enable) {
		int ret = clk_prepare_enable(clk);

		if (ret)
			dev_err(host->hba->dev,
				"failed to enable clk (cap mask: 0x%llx), ret=%d",
				cap, ret);
	} else {
		clk_disable_unprepare(clk);
	}
}

static void ufs_google_mark_device_off(struct ufs_google_host *host)
{
	host->device_off_time = ktime_get_boottime();
}

static void ufs_google_wait_device_ready(struct ufs_google_host *host)
{
	s64 power_reset_deficit;

	/*
	 * We need to wait at least PWR_RESET_DELAY_MIN_US between turning
	 * the device power off to turning the power back on. For the
	 * calculation use the time point at which turning the power on is
	 * safe (host->device_off_time + PWR_RESET_DELAY_MIN_US) and subtract
	 * current time from it, if the value is positive - that is how long
	 * we need to wait.
	 */
	power_reset_deficit = ktime_to_us(ktime_add_us(host->device_off_time,
						       PWR_RESET_DELAY_MIN_US) -
					  ktime_get_boottime());

	if (power_reset_deficit > 0)
		usleep_range(power_reset_deficit, power_reset_deficit + 100);
}

#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
static int ufs_google_mphy_indirect_reg_read(struct ufs_hba *hba,
					     struct mphy_reg *reg);
static int ufs_google_get_phy_register(struct ufs_hba *hba,
				       struct mphy_reg *reg)
{
	int ret;

	/*
	 * Reading the phy version registers requires the devices to be active
	 * and the link not to be in hibern8 state. Add a small delay to make
	 * sure all transitions completed.
	 */
	ufshcd_rpm_get_sync(hba);
	ufshcd_hold(hba);
	ufs_auto_hibern8_update(hba, false);
	mdelay(10);

	ret = ufs_google_mphy_indirect_reg_read(hba, reg);

	ufs_auto_hibern8_update(hba, true);
	ufshcd_release(hba);
	ufshcd_rpm_put_sync(hba);

	return ret;
}

static char *ufs_google_get_phy_version(struct ufs_hba *hba)
{
	char *phy_version = NULL;
	int ret;
	struct mphy_reg reg = {
		.addr = MPHY_FIRMWARE_VER_REG0,
	};

	ret = ufs_google_get_phy_register(hba, &reg);
	if (!ret)
		phy_version = kasprintf(GFP_KERNEL, "%u.%u.%u",
					(reg.data >> 12) & 0xF,
					(reg.data >> 4) & 0xFF, reg.data & 0xF);

	return phy_version;
}

static char *ufs_google_get_phy_release_date(struct ufs_hba *hba)
{
	char *phy_release_date = NULL;
	int ret;
	struct mphy_reg reg = {
		.addr = MPHY_FIRMWARE_VER_REG1,
	};

	ret = ufs_google_get_phy_register(hba, &reg);
	if (!ret)
		phy_release_date = kasprintf(GFP_KERNEL, "%u/%02u/%02u",
					     (reg.data & 0x7) + 2018,
					     (reg.data >> 3) & 0xF,
					     (reg.data >> 7) & 0x1F);

	return phy_release_date;
}

struct pixel_ufs *to_pixel_ufs(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	return &host->pixel_ufs;
}

static int pixel_crypto_init(struct ufs_hba *hba)
{
	// TODO: fill out similar to exynos_crypto_init;
	return 0;
}

static int ufs_google_set_pwr_mode(struct ufs_hba *hba,
				   const struct ufs_pa_layer_attr *pwr_mode)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	if (pwr_mode->gear_rx != pwr_mode->gear_tx ||
	    pwr_mode->gear_rx > UFS_HS_G5) {
		dev_err(hba->dev, "unsupported gears rx=%u tx=%u\n",
			pwr_mode->gear_rx, pwr_mode->gear_tx);
		return -EINVAL;
	}

	if (pwr_mode->lane_rx != pwr_mode->lane_tx ||
	    pwr_mode->lane_rx > UFS_LANE_2) {
		dev_err(hba->dev, "unsupported lanes rx=%u tx=%u\n",
			pwr_mode->lane_rx, pwr_mode->lane_tx);
		return -EINVAL;
	}

	/*
	 * We do not support PWM gear, and ufshcd_negotiate_pwr_params() only
	 * considers FAST_MODE for hs support. Make only FAST and UNCHANGED
	 * valid.
	 */
	if (pwr_mode->pwr_rx != pwr_mode->pwr_tx ||
	    (pwr_mode->pwr_rx != FAST_MODE && pwr_mode->pwr_rx != UNCHANGED)) {
		dev_err(hba->dev, "unsupported power mode rx=%u tx=%u\n",
			pwr_mode->pwr_rx, pwr_mode->pwr_tx);
		return -EINVAL;
	}

	/* We only support HS Rate B, as we do not have Rate A calibration */
	if (pwr_mode->hs_rate != PA_HS_MODE_B) {
		dev_err(hba->dev, "unsupported hs rate=%u\n",
			pwr_mode->hs_rate);
		return -EINVAL;
	}

	memcpy(&host->google_pwr_mode, pwr_mode, sizeof(*pwr_mode));

	return 0;
}

static void ufs_google_get_pwr_mode(struct ufs_hba *hba,
				    struct ufs_pa_layer_attr *pwr_mode)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	memcpy(pwr_mode, &host->google_pwr_mode, sizeof(*pwr_mode));
}

#if IS_ENABLED(CONFIG_UFS_PIXEL_READ_BOOST)
static bool ufs_google_get_read_boost(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	return host->read_boost.current_mode;
}

static void ufs_google_devfreq_scale_down(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;
	u32 i;

	for (i = 0; i < host->read_boost.devfreq_count; i++) {
		struct ufs_devfreq *ufs_df = host->read_boost.devfreqs + i;

		ret = dev_pm_qos_update_request(
			&ufs_df->qos_req_min,
			PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
		if (ret < 0) {
			dev_err(hba->dev,
				"failed to update %s devfreq to min default value (%d)\n",
				ufs_df->name, ret);
		}

		ret = dev_pm_qos_update_request(
			&ufs_df->qos_req_max,
			PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
		if (ret < 0) {
			dev_err(hba->dev,
				"failed to update %s devfreq to max default value (%d)\n",
				ufs_df->name, ret);
		}
	}
}

static int ufs_google_devfreq_scale_up(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;
	u32 i;

	for (i = 0; i < host->read_boost.devfreq_count; i++) {
		struct ufs_devfreq *ufs_df = host->read_boost.devfreqs + i;

		ret = dev_pm_qos_update_request(&ufs_df->qos_req_min,
						ufs_df->max_freq);
		if (ret < 0) {
			dev_err(hba->dev,
				"failed to update %s min devfreq to %u KHZ (%d)\n",
				ufs_df->name, ufs_df->max_freq, ret);
			ufs_google_devfreq_scale_down(hba);
			break;
		}

		ret = dev_pm_qos_update_request(&ufs_df->qos_req_max,
						ufs_df->max_freq);
		if (ret < 0) {
			dev_err(hba->dev,
				"failed to update %s max devfreq to %u KHZ (%d)\n",
				ufs_df->name, ufs_df->max_freq, ret);
			ufs_google_devfreq_scale_down(hba);
			break;
		}
	}

	return ret;
}

static int ufs_google_set_read_boost(struct ufs_hba *hba, bool enabled)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	trace_ufs_google_read_boost(PERFETTO_EVENT_SLICE_OPEN, enabled, 0);

	if (enabled == host->read_boost.current_mode)
		goto out;

	if (enabled) {
		ret = ufshcd_rpm_get_sync(hba);
		if (ret < 0) {
			dev_err(hba->dev, "read_boost: rpm_get failed (%d)\n",
				ret);
			goto out;
		}
		ufshcd_hold(hba);
		ufs_auto_hibern8_update(hba, false);
		ret = ufs_google_devfreq_scale_up(hba);
		if (ret < 0) {
			ufs_auto_hibern8_update(hba, true);
			ufshcd_release(hba);
			ufshcd_rpm_put_sync(hba);
		}
	} else {
		ufs_google_devfreq_scale_down(hba);
		ufs_auto_hibern8_update(hba, true);
		ufshcd_release(hba);
		ufshcd_rpm_put_sync(hba);
	}

	if (ret >= 0)
		host->read_boost.current_mode = enabled;

out:
	trace_ufs_google_read_boost(PERFETTO_EVENT_SLICE_END, enabled, ret);

	return ret;
}

static int ufs_google_setup_devfreq(struct ufs_google_host *host, u32 index)
{
	struct ufs_devfreq *ufs_df = host->read_boost.devfreqs + index;
	struct ufs_hba *hba = host->hba;
	int ret;

	ret = of_property_read_string_index(hba->dev->of_node,
					    "read-boost-devfreq-names", index,
					    &ufs_df->name);
	if (ret < 0) {
		dev_err(hba->dev,
			"failed to read 'read-boost-devfreq-names' at index %d (%d)\n",
			index, ret);
		return ret;
	}

	ret = of_property_read_u32_index(hba->dev->of_node,
					 "read-boost-devfreq-maxfreqs", index,
					 &ufs_df->max_freq);
	if (ret < 0) {
		dev_err(hba->dev,
			"failed to read 'read-boost-devfreq-maxfreqs' at index %d (%d)\n",
			index, ret);
		return ret;
	}

	ufs_df->df = gs_perf_domain_find_devfreq(ufs_df->name,
						 GS_RECOMMENDED_DEVFREQ);
	if (IS_ERR_OR_NULL(ufs_df->df)) {
		ret = PTR_ERR(ufs_df->df);
		dev_err(hba->dev, "failed to find %s devfreq (%d)\n",
			ufs_df->name, ret);
		return ret;
	}

	ret = google_pm_qos_add_devfreq_request(
		ufs_df->df, &ufs_df->qos_req_min, DEV_PM_QOS_MIN_FREQUENCY,
		PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
	if (ret) {
		dev_err(hba->dev, "failed to add %s devfreq min request (%d)\n",
			ufs_df->name, ret);
		return ret;
	}

	ret = google_pm_qos_add_devfreq_request(
		ufs_df->df, &ufs_df->qos_req_max, DEV_PM_QOS_MAX_FREQUENCY,
		PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	if (ret)
		dev_err(hba->dev, "failed to add %s devfreq max request (%d)\n",
			ufs_df->name, ret);

	return ret;
}

static int ufs_google_setup_devfreqs(struct ufs_google_host *host)
{
	struct ufs_hba *hba = host->hba;
	int count, max_freq_count, ret;
	u32 i;

	count = of_property_count_strings(hba->dev->of_node,
					  "read-boost-devfreq-names");
	if (count <= 0) {
		dev_info(hba->dev, "no devfreqs found\n");
		return 0;
	}

	max_freq_count =
		of_property_count_elems_of_size(hba->dev->of_node,
						"read-boost-devfreq-maxfreqs",
						sizeof(u32));
	if (max_freq_count != count) {
		dev_err(hba->dev,
			"mismatch count of devfreq names (%d) and frequencies (%d)\n",
			count, max_freq_count);
		return -EINVAL;
	}

	host->read_boost.devfreqs = devm_kcalloc(hba->dev, count,
						 sizeof(struct ufs_devfreq),
						 GFP_KERNEL);
	if (!host->read_boost.devfreqs)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		ret = ufs_google_setup_devfreq(host, i);
		if (ret)
			break;

		dev_info(hba->dev, "added devfreq %s (max@%u)\n",
			 host->read_boost.devfreqs[i].name,
			 host->read_boost.devfreqs[i].max_freq);

		host->read_boost.devfreq_count++;
	}

	return ret;
}

static void ufs_google_devfreq_cleanup(struct ufs_google_host *host, u32 index)
{
	struct ufs_devfreq *ufs_df = host->read_boost.devfreqs + index;
	int ret;

	if (dev_pm_qos_request_active(&ufs_df->qos_req_min)) {
		ret = google_pm_qos_remove_devfreq_request(ufs_df->df,
							   &ufs_df->qos_req_min);
		if (ret)
			dev_err(host->hba->dev,
				"failed to remove %s devfreq min (%d)\n",
				ufs_df->name, ret);
	}

	if (dev_pm_qos_request_active(&ufs_df->qos_req_max)) {
		ret = google_pm_qos_remove_devfreq_request(ufs_df->df,
							   &ufs_df->qos_req_max);
		if (ret)
			dev_err(host->hba->dev,
				"failed to remove %s devfreq max (%d)\n",
				ufs_df->name, ret);
	}
}

static void ufs_google_readboost_cleanup(struct ufs_google_host *host)
{
	u32 i;

	for (i = 0; i < host->read_boost.devfreq_count; i++)
		ufs_google_devfreq_cleanup(host, i);
}
#endif /* CONFIG_UFS_PIXEL_READ_BOOST */

static const struct pixel_ops pixel_ops = {
	.crypto_init = pixel_crypto_init,
	.set_pwr_mode = ufs_google_set_pwr_mode,
	.get_pwr_mode = ufs_google_get_pwr_mode,
	.get_phy_version = ufs_google_get_phy_version,
	.get_phy_release_date = ufs_google_get_phy_release_date,
#if IS_ENABLED(CONFIG_UFS_PIXEL_READ_BOOST)
	.get_read_boost = ufs_google_get_read_boost,
	.set_read_boost = ufs_google_set_read_boost,
#endif
};
#endif

static int ufs_google_phy_setup_vreg(struct ufs_hba *hba, bool on);
static int ufs_google_pd_notifier(struct notifier_block *nb,
				  unsigned long action, void *data);
static void ufs_google_sleep_handler(struct work_struct *work);
static int ufs_google_psm_wait_for_state(struct ufs_google_host *host,
					 void __iomem *status_reg_mmio,
					 u32 target_state, const char *id);

/**
 * ufs_google_set_acg - Enable or disable REF_CLK Auto Clock Gating (ACG)
 * @hba: host controller instance
 * @on: true to enable ACG, false to disable
 *
 * This function enables or disables the auto clock gating for both
 * gpio_ref_clk and mphy_ref_clk.
 *
 * Returns 0 on success, or a negative error code on failure.
 */
static int ufs_google_set_acg(struct ufs_hba *hba, bool on)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;

	ret = goog_cpm_clk_toggle_auto_gate(host->gpio_refclk, on);
	if (ret) {
		dev_err(hba->dev, "%s gpio_refclk ACG failed (%d)",
			on ? "enable" : "disable", ret);
		return ret;
	}

	ret = goog_cpm_clk_toggle_auto_gate(host->mphy_refclk, on);
	if (ret) {
		dev_err(hba->dev, "%s mphy_refclk ACG failed (%d)",
			on ? "enable" : "disable", ret);
		return ret;
	}

	return 0;
}

static int ufs_google_toggle_vreg(struct ufs_hba *hba, struct ufs_vreg *vreg,
				  bool on)
{
	int ret;

	if (!vreg || vreg->enabled == on || vreg->always_on)
		return 0;

	ret = on ? regulator_enable(vreg->reg) : regulator_disable(vreg->reg);
	if (ret) {
		dev_err(hba->dev, "vreg %s toogle %s failed (%d)", vreg->name,
			on ? "on" : "off", ret);
		return ret;
	}

	vreg->enabled = on;

	return 0;
}

static int ufs_google_init_vreg(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct ufs_google_host *host;
	struct regulator *reg;
	int ret;

	host = ufshcd_get_variant(hba);

	ret = ufshcd_populate_vreg(hba->dev, "vdd0p75", &host->vdd0p75, false);
	if (ret)
		return ret;

	ret = ufshcd_populate_vreg(hba->dev, "vdd1p2", &host->vdd1p2, false);
	if (ret)
		return ret;

	reg = devm_regulator_get(dev, "vdd0p75");
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(dev, "Failed to get 0.75V PHY regulator(%d)\n", ret);
		return ret;
	}
	host->vdd0p75->reg = reg;

	reg = devm_regulator_get(dev, "vdd1p2");
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(dev, "Failed to get 1.2V PHY regulator(%d)\n", ret);
		return ret;
	}
	host->vdd1p2->reg = reg;

	return 0;
}

static int ufs_google_attach_power_domains(struct ufs_google_host *host)
{
	int err;
	struct ufs_hba *hba = host->hba;

	host->ufs_prep_pd =
		dev_pm_domain_attach_by_name(hba->dev, "ufs_prep_pd");
	if (IS_ERR(host->ufs_prep_pd)) {
		err = PTR_ERR(host->ufs_prep_pd);
		host->ufs_prep_pd = NULL;
		return err;
	}

	host->ufs_pd = dev_pm_domain_attach_by_name(hba->dev, "ufs_pd");
	if (IS_ERR(host->ufs_pd)) {
		dev_pm_domain_detach(host->ufs_prep_pd, true);
		err = PTR_ERR(host->ufs_pd);
		host->ufs_pd = NULL;
		return err;
	}

	return 0;
}

static inline void ufs_google_pd_get_sync(struct ufs_google_host *host)
{
	if (!(host->caps & GCAP_RSC_UFS_PD))
		return;

	pm_runtime_get_sync(host->ufs_prep_pd);
	pm_runtime_get_sync(host->ufs_pd);
}

static inline void ufs_google_pd_put_sync(struct ufs_google_host *host)
{
	if (!(host->caps & GCAP_RSC_UFS_PD))
		return;

	pm_runtime_put_sync(host->ufs_pd);
	pm_runtime_put_sync(host->ufs_prep_pd);
}

static void ufs_google_detach_power_domains(struct ufs_google_host *host)
{
	if (!(host->caps & GCAP_RSC_UFS_PD))
		return;

	pm_runtime_disable(host->ufs_pd);
	dev_pm_domain_detach(host->ufs_pd, true);

	pm_runtime_disable(host->ufs_prep_pd);
	dev_pm_domain_detach(host->ufs_prep_pd, true);

	pm_runtime_disable(host->cpu_top_cl);
	dev_pm_domain_detach(host->cpu_top_cl, true);
}

static void ufs_google_init_caps(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int i;

	struct dts_cap {
		const char *prop_name;
		u64 cap;
	};

	static const struct dts_cap dts_caps[] = {
		{ "google,enable-phy-patching", GCAP_PHY_PATCHING },
		{ "google,enable-hc-ah8-pg", GCAP_HC_AH8_PG },
		{ "google,enable-hc-swh8-pg", GCAP_HC_SWH8_PG },
		{ "google,enable-phy-calibration", GCAP_PHY_CAL },
		{ "google,enable-local-rpm", GCAP_LOCAL_RPM },
		{ "google,enable-local-swh8", GCAP_LOCAL_SWH8 },
		{ "google,decouple-ufs-en", GCAP_DECOUPLE_UFS_EN },
		{ "google,enable-hs-lss", GCAP_HS_LSS },
		{ "google,enable-refclk-acg", GCAP_REF_CLK_ACG },
		{ "google,skip-cport-setup", GCAP_SKIP_CPORT_SETUP },
		{ "google,mphy-pmc-war", GCAP_MPHY_PMC_WAR },
		{ "google,rext-internal", GCAP_REXT_INTERNAL },
		{ "google,limit-reset-to-eh", GCAP_LIMIT_RESET_TO_EH },
	};

	for (i = 0; i < ARRAY_SIZE(dts_caps); i++) {
		if (of_property_read_bool(hba->dev->of_node,
					  dts_caps[i].prop_name))
			host->caps |= dts_caps[i].cap;
	}
}

static void ufs_google_devm_cleanup(void *data)
{
	struct ufs_google_host *host = data;

	mutex_destroy(&host->indirect_reg_mutex);
	mutex_destroy(&host->ufs_pm_lock);

#if IS_ENABLED(CONFIG_UFS_PIXEL_READ_BOOST)
	ufs_google_readboost_cleanup(host);
#endif
}

static void ufs_google_vh_ufs_check_int_errors_handler(void *data,
						       struct ufs_hba *hba,
						       bool queue_eh_work)
{
	trace_ufs_google_event(PERFETTO_EVENT_INSTANT, GEVENT_HBA_ERROR,
			       hba->errors);
}

static void ufs_google_vh_ufs_eh_timed_out_handler(void *data,
						   struct ufs_hba *hba,
						   struct scsi_cmnd *scmd)
{
	/* Trace SCSI command timeout with OP code */
	trace_ufs_google_event(PERFETTO_EVENT_INSTANT, GEVENT_SCSI_TIMEOUT,
			       scmd->cmnd[0]);
}

static int ufs_google_register_vh_handlers(struct ufs_google_host *host)
{
	int ret = 0;

	UFS_GOOG_REGISTER_VH_HANDLER(ufs_check_int_errors);
	UFS_GOOG_REGISTER_VH_HANDLER(ufs_eh_timed_out);

	return ret;
}

static int ufs_google_set_device_on(struct ufs_google_host *host)
{
	struct ufs_hba *hba = host->hba;
	int err = 0;

	if (host->device_on)
		return 0;

	if (!(host->caps & GCAP_DECOUPLE_UFS_EN))
		ufs_google_wait_device_ready(host);

	/* reconfigure pin as refclk */
	if (host->caps & GCAP_RSC_REF_CLK_PINCTRL) {
		err = pinctrl_select_state(host->pinctrl,
					   host->refclk_on_state);
		if (err < 0) {
			dev_err(hba->dev, "pin select state fail, %d\n", err);
			return err;
		}
	}

	/* 2h. reset = 0 */
	if (host->caps & GCAP_RSC_RSTN_GPIO)
		gpiod_set_value(host->resetb, 0);

	/* 2h. enable vccq */
	if (host->caps & GCAP_RSC_UFS_EN_GPIO)
		gpiod_set_value(host->pwr_en, 1);
	usleep_range(PWR_EN_DELAY_MIN_US, PWR_EN_DELAY_MAX_US);

	/* 2h. config HS/LS LSS */
	if (ufs_use_hs_lss(host)) {
		gpiod_set_value(host->gpio_lss, 1);
		usleep_range(GPIO_LSS_DELAY_MIN_US, GPIO_LSS_DELAY_MAX_US);
	}

	/* 2h. drive refclk */
	ufs_clk_switch(host, host->gpio_refclk, true, GCAP_RSC_GPIO_REF_CLK);
	usleep_range(REFCLK_DELAY_MIN_US, REFCLK_DELAY_MAX_US);

	/* 2h. reset = 1 */
	if (host->caps & GCAP_RSC_RSTN_GPIO)
		gpiod_set_value(host->resetb, 1);

	host->device_on = true;
	return 0;
}

static int ufs_google_set_device_off(struct ufs_google_host *host)
{
	struct ufs_hba *hba = host->hba;
	int err = 0;

	if (!host->device_on)
		return 0;

	if (host->caps & GCAP_RSC_GPIO_REF_CLK)
		clk_disable_unprepare(host->gpio_refclk);
	usleep_range(10, 12);

	if (ufs_use_hs_lss(host))
		gpiod_set_value(host->gpio_lss, 0);

	if (host->caps & GCAP_RSC_RSTN_GPIO)
		gpiod_set_value(host->resetb, 0);

	/* Set UFS_EN to 0 to disable vcc/vccq */
	if (host->caps & GCAP_RSC_UFS_EN_GPIO)
		gpiod_set_value(host->pwr_en, 0);

	if (!(host->caps & GCAP_DECOUPLE_UFS_EN))
		ufs_google_mark_device_off(host);

	if (host->caps & GCAP_RSC_REF_CLK_PINCTRL) {
		err = pinctrl_select_state(host->pinctrl,
					   host->refclk_off_state);
		if (err < 0) {
			dev_err(hba->dev, "pin select state fail, %d\n", err);
			return err;
		}
	}

	host->device_on = false;
	return 0;
}

static void ufs_google_init_rext(struct ufs_google_host *host)
{
	struct ufs_hba *hba = host->hba;

	if (!(host->caps & GCAP_REXT_INTERNAL))
		return;

	of_property_read_u32(hba->dev->of_node, "google,rext-val", &host->rext_val);
	dev_info(hba->dev, "UFS REXT Internal mode active. Trim: %d\n", host->rext_val);
}

static int ufs_google_init(struct ufs_hba *hba)
{
	int err = 0;
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_google_host *host;
	u32 ah8_timer_scale;
	u32 ah8_idle_timer_value;
	u32 ahit = 0;
	int id;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host) {
		err = -ENOMEM;
		goto out;
	}

	/* two way bind between variant and hba */
	host->hba = hba;
	ufshcd_set_variant(hba, host);

	host->ufs_top_mmio = devm_platform_ioremap_resource_byname(pdev, "ufs_top");
	if (IS_ERR(host->ufs_top_mmio)) {
		err = PTR_ERR(host->ufs_top_mmio);
		dev_err(dev, "%s: failed to init ufs_top mmio, error code: %d\n",
			__func__, err);
		goto out_unset;
	}

	host->ufs_phy_sram_mmio =
		devm_platform_ioremap_resource_byname(pdev, "ufs_phy_sram");
	if (IS_ERR(host->ufs_phy_sram_mmio)) {
		err = PTR_ERR(host->ufs_phy_sram_mmio);
		dev_err(dev,
			"%s: failed to init ufs_phy_sram mmio, error code: %d\n",
			__func__, err);
		goto out_unset;
	}

	host->ufs_ss_mmio =
		devm_platform_ioremap_resource_byname(pdev, "ufs_ss");
	if (IS_ERR(host->ufs_ss_mmio)) {
		err = PTR_ERR(host->ufs_ss_mmio);
		dev_err(dev,
			"%s: failed to init ufs_ss mmio, error code: %d. skip\n",
			__func__, err);
		host->ufs_ss_mmio = NULL;
	}

	host->hsios_psm_status_mmio =
		devm_platform_ioremap_resource_byname(pdev, "psm_status_hsios");
	if (IS_ERR(host->hsios_psm_status_mmio)) {
		err = PTR_ERR(host->hsios_psm_status_mmio);
		dev_err(dev,
			"%s: failed to init psm_status_hsios_mmio, error code: %d. skip\n",
			__func__, err);
		host->hsios_psm_status_mmio = NULL;
	}

	host->ufs_hc_psm_status_mmio =
		devm_platform_ioremap_resource_byname(pdev,
						      "psm_status_ufs_hc");
	if (IS_ERR(host->ufs_hc_psm_status_mmio)) {
		err = PTR_ERR(host->ufs_hc_psm_status_mmio);
		dev_err(dev,
			"%s: failed to init ufs_hc_psm_status_mmio, error code: %d. skip\n",
			__func__, err);
		host->ufs_hc_psm_status_mmio = NULL;
	}

	host->ufs_phy_psm_status_mmio =
		devm_platform_ioremap_resource_byname(pdev,
						      "psm_status_ufs_phy");
	if (IS_ERR(host->ufs_phy_psm_status_mmio)) {
		err = PTR_ERR(host->ufs_phy_psm_status_mmio);
		dev_err(dev,
			"%s: failed to init ufs_phy_psm_status_mmio, error code: %d. skip\n",
			__func__, err);
		host->ufs_phy_psm_status_mmio = NULL;
	}

	err = ufs_google_init_vreg(hba);
	if (err) {
		dev_err(dev, "vreg init failed, error code: %d", err);
		goto out_unset;
	}

	host->phy_rst = devm_reset_control_get(dev, "phy_rst");
	if (IS_ERR(host->phy_rst)) {
		err = PTR_ERR(host->phy_rst);
		goto out_unset;
	}

	/* config pin as refclk */
	host->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(host->pinctrl)) {
		dev_err(dev, "refclk pinctrl get fail.");
		goto out_unset;
	}
	host->refclk_on_state = pinctrl_lookup_state(host->pinctrl, "refclk_on");
	if (IS_ERR(host->refclk_on_state))
		dev_err(dev, "refclk pinctrl get on state fail. skip.");

	host->refclk_off_state = pinctrl_lookup_state(host->pinctrl, "refclk_off");
	if (IS_ERR(host->refclk_off_state))
		dev_err(dev, "refclk pinctrl get off state fail. skip.");

	if (!IS_ERR(host->refclk_on_state) && !IS_ERR(host->refclk_off_state))
		host->caps |= GCAP_RSC_REF_CLK_PINCTRL;

	ufs_google_phy_setup_vreg(hba, true);

	if (!of_property_read_u32(pdev->dev.of_node, "ah8-timer-scale",
				  &ah8_timer_scale) &&
	    !of_property_read_u32(pdev->dev.of_node, "ah8-idle-timer-value",
				  &ah8_idle_timer_value)) {
		ahit = FIELD_PREP(UFSHCI_AHIBERN8_TIMER_MASK,
				  ah8_idle_timer_value) |
		       FIELD_PREP(UFSHCI_AHIBERN8_SCALE_MASK, ah8_timer_scale);
	}

	err = of_property_read_u32(pdev->dev.of_node, "ip-idle-index",
				   &host->ip_idle_index);
	if (err)
		dev_err(dev, "failed to acquire ip-idle-index value. skip\n");
	else
		host->caps |= GCAP_RSC_IP_IDLE;

	host->phy_cal_size = of_property_count_elems_of_size(pdev->dev.of_node,
							     "phy-cal-data",
							     sizeof(u32));
	if (host->phy_cal_size < 0) {
		err = host->phy_cal_size;
		dev_err(dev, "failed to obtain phy cal size (%d)\n", err);
		goto out_unset;
	}

	host->phy_cal_data = devm_kcalloc(dev, host->phy_cal_size,
					  sizeof(*host->phy_cal_data),
					  GFP_KERNEL);
	if (!host->phy_cal_data) {
		err = -ENOMEM;
		goto out_unset;
	}

	err = of_property_read_u32_array(pdev->dev.of_node, "phy-cal-data",
					 (uint32_t *)host->phy_cal_data,
					 host->phy_cal_size);
	if (err) {
		dev_err(dev, "failed to read 'phy-cal-data' array (%d)\n", err);
		goto out_unset;
	}

	/* assert device reset and start config device pins */
	host->resetb = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(host->resetb)) {
		err = PTR_ERR(host->resetb);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire reset gpio: %d\n", err);
	} else {
		host->caps |= GCAP_RSC_RSTN_GPIO;
	}

	host->pwr_en = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(host->pwr_en)) {
		err = PTR_ERR(host->pwr_en);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire power gpio: %d. skip\n",
				err);
	} else {
		host->caps |= GCAP_RSC_UFS_EN_GPIO;
	}

	host->gpio_lss = devm_gpiod_get(dev, "lss", GPIOD_OUT_LOW);
	if (IS_ERR(host->gpio_lss)) {
		err = PTR_ERR(host->gpio_lss);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "failed to acquire lss gpio: %d. skip\n",
				err);
	} else {
		host->caps |= GCAP_RSC_LSS_GPIO;
	}

	host->gpio_refclk = devm_clk_get(dev, "gpio_ref_clk");
	if (IS_ERR(host->gpio_refclk)) {
		dev_err(dev, "failed to acquire gpio_ref_clk: %ld. skip\n",
			PTR_ERR(host->gpio_refclk));
		host->gpio_refclk = NULL;
	} else {
		host->caps |= GCAP_RSC_GPIO_REF_CLK;
	}

	host->mphy_refclk = devm_clk_get(dev, "mphy_ref_clk");
	if (IS_ERR(host->mphy_refclk)) {
		dev_err(dev, "failed to acquire mphy_refclk: %ld. skip\n",
			PTR_ERR(host->mphy_refclk));
		host->mphy_refclk = NULL;
	}

	host->hsios_aux_clk = devm_clk_get(dev, "aux_clk");
	if (IS_ERR(host->hsios_aux_clk)) {
		dev_err(dev, "failed to acquire hsios_aux_clk: %ld. skip\n",
			PTR_ERR(host->hsios_aux_clk));
		host->hsios_aux_clk = NULL;
	} else {
		host->caps |= GCAP_RSC_AUX_CLK;
	}

	host->deep_h8_clk = devm_clk_get(dev, "deep_h8_clk");
	if (IS_ERR(host->deep_h8_clk)) {
		dev_err(dev, "failed to acquire deep_h8_clk: %ld. skip\n",
			PTR_ERR(host->deep_h8_clk));
		host->deep_h8_clk = NULL;
	} else {
		host->caps |= GCAP_RSC_DEEP_H8_CLK;
		ufs_clk_switch(host, host->deep_h8_clk, true,
			       GCAP_RSC_DEEP_H8_CLK);
	}

	host->hsios_cp_core_clk = devm_clk_get(dev, "cp_core_clk");
	if (IS_ERR(host->hsios_cp_core_clk)) {
		dev_warn(dev,
			 "failed to acquire hsios_cp_core_clk: %ld. skip\n",
			 PTR_ERR(host->hsios_cp_core_clk));
		host->hsios_cp_core_clk = NULL;
	} else {
		host->caps |= GCAP_RSC_CP_CORE_CLK;
		ufs_clk_switch(host, host->hsios_cp_core_clk, true,
			       GCAP_RSC_CP_CORE_CLK);
	}

	err = ufs_google_attach_power_domains(host);
	if (err) {
		dev_err(dev,
			"%s: failed to attach power domains, error code: %d. skip\n",
			__func__, err);
	} else {
		host->caps |= GCAP_RSC_UFS_PD;
	}

	/*
	 * cpu_top_cl is mapped to CPU_TOP_CL in the device-tree, a high-level
	 * power domain within the CPU power management architecture.
	 * CPU_TOP_CL is associated with deeper idle states like C4_LITE and
	 * C4_DEEP. These states are entered when all underlying CPU clusters
	 * and cores are idle, allowing for significant power savings by
	 * powering down larger sections of the CPU complex, including the
	 * DSU (DynamIQ Shared Unit) and other components.
	 */
	host->cpu_top_cl = dev_pm_domain_attach_by_name(hba->dev, "cpu_top_cl");
	if (IS_ERR_OR_NULL(host->cpu_top_cl)) {
		dev_warn(dev, "failed to acquire cpu top cluster: %ld. skip\n",
			 PTR_ERR(host->cpu_top_cl));
		host->cpu_top_cl = NULL;
	} else {
		host->caps |= GCAP_RSC_CPU_TOP_CL;
	}

	ufs_google_init_caps(hba);
	dev_info(hba->dev, "google caps=%64pbl", &host->caps);

	ufs_google_init_rext(host);

	if (host->caps & GCAP_REF_CLK_ACG &&
	    (!host->mphy_refclk || !host->gpio_refclk)) {
		err = -EINVAL;
		dev_err(hba->dev, "GCAP_REF_CLK_ACG: Missing refclk(s).");
		goto out_unset;
	}

	if (host->caps & GCAP_PHY_PATCHING) {
		/* if google,enable-phy-patching is set, further read patching mode */
		u32 patch_mode;

		err = of_property_read_u32(hba->dev->of_node,
					   "google,phy-patching-mode",
					   &patch_mode);
		if (err) {
			dev_err(hba->dev, "invalid phy patching mode %d", err);
			goto out_unset;
		}
		host->phy_patch_mode = patch_mode;
	} else {
		/* default use ROM mode */
		host->phy_patch_mode = PHY_ROM_MODE;
	}
	dev_info(hba->dev, "phy patching mode=%d", host->phy_patch_mode);

	if (host->caps & GCAP_LOCAL_RPM)
		INIT_DELAYED_WORK(&host->ufs_sleep_work,
				  ufs_google_sleep_handler);

	ufs_google_plat_set_gops(host);
	if (!host->gops) {
		dev_err(hba->dev, "gops not set\n");
		goto out_unset;
	}

	err = ufs_google_register_vh_handlers(host);
	if (err) {
		dev_err(hba->dev, "vendor hooks registration failed");
		goto out_unset;
	}

	err = ufs_plat_config_cpm(host);
	if (err) {
		dev_err(hba->dev, "%s: failed to init ufs cpm, error code: %d.",
			__func__, err);
		goto out_unset;
	}

	/*
	 * Turning ufs_pd ON changes PSM state from PREP to ON. So the setup required in
	 * PREP state is done with PRE_ON of ufs_pd notifier.
	 * ufs_prep_pd which changes PSM state from OFF to PREP does not need a notifier.
	 */
	host->top_nb.notifier_call = ufs_google_pd_notifier;
	dev_pm_genpd_add_notifier(host->ufs_pd, &host->top_nb);

	/*
	 * Move PSM from OFF to PREP and then PREP to ON.
	 * If the PSM is already in PREP state, ufs_prep_pd turn ON request
	 * to CPM will have no effect.
	 */
	ufs_google_pd_get_sync(host);

	err = ufs_google_set_device_on(host);
	if (err) {
		dev_err(hba->dev, "set device on failed (%d)", err);
		goto out_unset;
	}

	/*
	 * This quirk should be set at least for LGA, MBU and LAJ. See also:
	 * https://b.corp.google.com/issues/496003761 (LAJ)
	 * https://b.corp.google.com/issues/498248007 (MBU)
	 * https://b.corp.google.com/issues/498535696 (MBU)
	 */
	hba->android_quirks |= UFSHCD_ANDROID_QUIRK_AH8_BREAKS_DME;
#if IS_ENABLED(CONFIG_SOC_LGA)
	hba->android_quirks |= UFSHCD_ANDROID_QUIRK_SET_IID_TO_ONE;
#endif
	hba->caps |= UFSHCD_CAP_RPM_AUTOSUSPEND;
	if (!(host->caps & GCAP_LOCAL_RPM)) {
		hba->caps |= UFSHCD_CAP_CLK_GATING;
		hba->caps |= UFSHCD_CAP_HIBERN8_WITH_CLK_GATING;
		hba->host->rpm_autosuspend_delay = UFS_SLEEP_DELAY_MS;
	} else {
		hba->host->rpm_autosuspend_delay = LOW_POWER_DELAY_MS;
	}

	if (ahit)
		hba->ahit = ahit;
	else
		hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8;

	/* store ufs host symbol for ramdump analysis */
	id = atomic_inc_return(&ufs_host_index) - 1;
	if (id < MAX_UFS_HOSTS)
		ufs_host_backup[id] = host;

	/* configure desired power parameters */
	host->google_pwr_mode = (struct ufs_pa_layer_attr){
		.gear_rx = UFS_HS_G5,
		.gear_tx = UFS_HS_G5,
		.lane_rx = UFS_LANE_2,
		.lane_tx = UFS_LANE_2,
		.pwr_rx = FAST_MODE,
		.pwr_tx = FAST_MODE,
		.hs_rate = PA_HS_MODE_B,
	};

	mutex_init(&host->indirect_reg_mutex);
	mutex_init(&host->ufs_pm_lock);

	err = devm_add_action_or_reset(dev, ufs_google_devm_cleanup, host);
	if (err) {
		dev_err(hba->dev,
			"failed to register ufs_google_devm_cleanup\n");
		goto out_unset;
	}

	ufs_google_init_dbg(hba);

#if IS_ENABLED(CONFIG_UFS_PIXEL_READ_BOOST)
	err = ufs_google_setup_devfreqs(host);
	if (err) {
		ufs_google_detach_power_domains(host);
		return err;
	}
#endif

#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
	err = pixel_init(hba, dev, &pixel_ops);

	if (err)
		ufs_google_detach_power_domains(host);
	return err;
#else
	return 0;
#endif

out_unset:
	ufshcd_set_variant(hba, NULL);
out:
	return err;
}

static void ufs_google_select_mphy_fw_mode(struct ufs_google_host *host)
{
	u32 data;

	data = ufs_top_csr_readl(host, REG_HSIOS_UFS_PWR_CTRL);
	if (ufs_should_apply_phy_patch(host)) {
		if (host->phy_patch_mode == PHY_BOOTLD_BYPASS_MODE) {
			data &= ~UFS_SRAM_BYPASS_MASK;
			data |= UFS_SRAM_BOOTLOAD_BYPASS_MASK;
		} else {
			dev_err(host->hba->dev, "invalid mode %d",
				host->phy_patch_mode);
		}
	} else {
		/* Use PHY_ROM_MODE */
		data |= UFS_SRAM_BYPASS_MASK;
		data |= UFS_SRAM_BOOTLOAD_BYPASS_MASK;
	}
	ufs_top_csr_writel(host, data, REG_HSIOS_UFS_PWR_CTRL);
}

/**
 * ufs_google_pd_notifier - UFS Power domain notifier
 * Handle `hsio_s_ufs_pd` power on and off callback.
 * We assume the power domain is in ON state when enter kernel.
 */
static int ufs_google_pd_notifier(struct notifier_block *nb,
				  unsigned long action, void *dat)
{
	struct ufs_google_host *host = container_of(nb, struct ufs_google_host,
						    top_nb);
	u32 data;
	struct ufs_hba *hba = host->hba;
	u32 intr_status;
	int err = 0;

	switch (action) {
	case GENPD_NOTIFY_PRE_ON:
		host->calibration_needed = true;
		host->phy_patching_needed = true;
		host->phy_init_needed = true;

		err = ufs_google_psm_wait_for_state(host,
						    host->ufs_phy_psm_status_mmio,
						    PSM_STATUS_VALID_STATE_PG,
						    "PHY");
		if (err)
			return err;

		ufs_google_select_mphy_fw_mode(host);

		data = ufs_top_csr_readl(host, REG_HSIOS_UFS_PHYCLK_SEL);
		data |= REF_CLK_SEL_MASK & REF_CLK_SEL;
		ufs_top_csr_writel(host, data, REG_HSIOS_UFS_PHYCLK_SEL);

		data = ufs_top_csr_readl(host, REG_HSIOS_UFS_MPHY_CFG);
		data |= UFS_MPHY_CFG_MASK & UFS_MPHY_CFG;
		ufs_top_csr_writel(host, data, REG_HSIOS_UFS_MPHY_CFG);

		data = ufs_top_csr_readl(host, REG_HSIOS_UFS_CFG_CLKSEL);
		data |= CFG_CLK_SEL_MASK & CFG_CLK_SEL;
		ufs_top_csr_writel(host, data, REG_HSIOS_UFS_CFG_CLKSEL);

		/*
		 * Configure the UFS Reference Resistor (REXT) analog matching.
		 * If the GCAP_REXT_INTERNAL capability is declared by the
		 * Device Tree, program the persistent dynamic trim setting
		 * (host->rext_val) stashed stably during the driver probe
		 * initialization phase and enable internal matching mode.
		 * If the capability is absent, explicitly disable the
		 * internal matching arrays.
		 */
		data = ufs_top_csr_readl(host, REG_HSIOS_UFS_REXT_CTRL);
		data &= ~UFS_REXT_EN_MASK;
		if (host->caps & GCAP_REXT_INTERNAL) {
			data &= ~UFS_REXT_CONTROL_MASK;
			data |= FIELD_PREP(UFS_REXT_CONTROL_MASK, host->rext_val);
			data |= UFS_REXT_EN_MASK;
		}
		ufs_top_csr_writel(host, data, REG_HSIOS_UFS_REXT_CTRL);

		/* 2e. set UFS_LA_RSTMODE_REQ according to LSS mode */
		data = FIELD_PREP(UFS_LA_RSTMODE_REQ_MASK,
				  ufs_use_hs_lss(host));
		ufs_top_csr_writel(host, data, REG_HSIOS_UFS_LA_RSTMODE_REQ);

		break;
	case GENPD_NOTIFY_ON:
		/* TODO: b/458531873 - let CPM poll PHY state as well */
		/* 2f: Wait for HC and PHY PSM to reach ON state */
		err = ufs_google_psm_wait_for_state(host,
						    host->ufs_phy_psm_status_mmio,
						    PSM_STATUS_VALID_STATE_ON,
						    "PHY");
		if (err)
			return err;

		/* ufs_wait_for_fips_140 */
		err = readl_poll_timeout(host->ufs_top_mmio +
						 REG_HSIOS_UFS_ENC_STAT,
					 data, data & CRYPTO_KAT_STAT_MASK,
					 FIPS_140_DELAY_US,
					 FIPS_140_TIMEOUT_US);
		if (err)
			return err;

		/*
		 * HPG recommends to keep refclk gating control always-on,
		 * thus should config only on first initialization
		 */
		ufs_plat_set_refclk_control(host, true);

		intr_status = ufshcd_readl(hba, REG_IS);
		intr_status &= VS_INTERRUPT_MASK;
		ufshcd_writel(hba, intr_status, REG_IS);

		break;
	case GENPD_NOTIFY_OFF:
		err = ufs_google_psm_wait_for_state(host,
						    host->ufs_phy_psm_status_mmio,
						    PSM_STATUS_VALID_STATE_PG,
						    "PHY");
		if (err)
			return err;

		host->calibration_needed = true;
		host->phy_patching_needed = true;

		break;
	default:
		break;
	}

	return err;
}

static int ufs_google_lga_set_dma_mask(struct ufs_hba *hba)
{
	return dma_set_mask_and_coherent(hba->dev, DMA_BIT_MASK(36));
}

static int ufs_google_apply_dev_quirks(struct ufs_hba *hba)
{
	/*
	 * The purpose of this function is to customize UFS device behavior by
	 * setting hba->dev_quirks and/or by calling ufshcd_dme_set(). Disable
	 * AHIT from this function in SDB mode since this function is the first
	 * host driver callback after it has been decided whether or not MCQ
	 * will be used.
	 */
	if (hba->mcq_enabled)
		return 0;

	/*
	 * Disable AHIT in SDB mode. We can't call
	 * ufshcd_configure_auto_hibern8() because it would trigger a kernel
	 * crash. Calling that function is only safe after LUN scanning
	 * completed. TODO(b/412982589): analyze why UFS command timeouts occur
	 * with AHIT enabled in SDB mode.
	 */
	hba->ahit = 0;
	ufshcd_writel(hba, hba->ahit, REG_AUTO_HIBERNATE_IDLE_TIMER);
	hba->quirks |= UFSHCD_QUIRK_BROKEN_AUTO_HIBERN8;

	return 0;
}

/**
 * ufshcd_google_dme_get_layer_enable - wrapper for getting DME_VS_LAYER_ENABLE,
 * used as @op in readx_poll_timeout
 * @hba: host controller instance
 */
static inline u32 ufshcd_google_dme_get_layer_enable(struct ufs_hba *hba)
{
	u32 data;

	ufshcd_dme_get(hba, UIC_ARG_MIB(UNIPRO_DME_LAYER_EN), &data);
	return data;
}

/**
 * ufshcd_google_dme_set_seq - set a sequence of DME attributes
 * @hba: host controller instance
 * @arr: array of dme attributes to be set
 * @num: size of @arr
 */
static int ufshcd_google_dme_set_seq(struct ufs_hba *hba,
				     const struct ufs_dme_attr *arr, int num)
{
	int i, err;

	for (i = 0; i < num; ++i) {
		err = ufshcd_dme_set_attr(hba, arr[i].sel, ATTR_SET_NOR,
					  arr[i].val, arr[i].peer);
		if (err)
			return err;
	}

	return 0;
}

static int ufs_google_wait_for_uic_cmd(struct ufs_hba *hba,
				       struct uic_command *uic_cmd)
{
	u32 val;

	if (read_poll_timeout(ufshcd_readl, val, val & UIC_COMMAND_COMPL,
			      UIC_COMMAND_DELAY_US, UIC_COMMAND_POLL_TIMEOUT_US,
			      false, hba, REG_INTERRUPT_STATUS)) {
		dev_err(hba->dev, "uic completion timeout val=%08x\n", val);
		return -ETIMEDOUT;
	}

	ufshcd_writel(hba, UIC_COMMAND_COMPL, REG_INTERRUPT_STATUS);

	/*
	 * The UICCMDARG2 register contains the ConfigResultCode in the
	 * lower 8 bits. A value 0 indicates success and all others
	 * indicate failure. See JESD223E 5.6.3 for additional details.
	 */
	uic_cmd->argument2 |= ufshcd_readl(hba, REG_UIC_COMMAND_ARG_2) &
			      MASK_UIC_COMMAND_RESULT;

	/*
	 * The UICCMDARG3 register contains the value of the attribute
	 * returned by the uic command in case of DME_GET or the value
	 * to be set in case of DME_SET. While retrieving the set value
	 * is redundant here, we are following the convention from the
	 * core driver. See JESD223E 5.6.4 for additional details.
	 */
	uic_cmd->argument3 = ufshcd_readl(hba, REG_UIC_COMMAND_ARG_3);

	/*
	 * This function may return a positive value to indicate the
	 * error is coming from the MIPI UniPro layer. Callers of this
	 * function should assume any non-zero value as a failure.
	 * Value Definitions:
	 * 00h SUCCESS
	 * 01h INVALID_MIB_ATTRIBUTE
	 * 02h INVALID_MIB_ATTRIBUTE_VALUE
	 * 03h READ_ONLY_MIB_ATTRIBUTE
	 * 04h WRITE_ONLY_MIB_ATTRIBUTE
	 * 05h BAD_INDEX
	 * 06h LOCKED_MIB_ATTRIBUTE
	 * 07h BAD_TEST_FEATURE_INDEX
	 * 08h PEER_COMMUNICATION_FAILURE
	 * 09h BUSY
	 * 0Ah DME_FAILURE
	 * 0Bh-FFh Reserved
	 */
	return uic_cmd->argument2 & MASK_UIC_COMMAND_RESULT;
}

static void __ufs_google_send_uic_cmd(struct ufs_hba *hba,
				      const struct uic_command *uic_cmd)
{
	ufshcd_writel(hba, uic_cmd->argument1, REG_UIC_COMMAND_ARG_1);
	ufshcd_writel(hba, uic_cmd->argument2, REG_UIC_COMMAND_ARG_2);
	ufshcd_writel(hba, uic_cmd->argument3, REG_UIC_COMMAND_ARG_3);
	ufshcd_writel(hba, uic_cmd->command & COMMAND_OPCODE_MASK,
		      REG_UIC_COMMAND);
}

static int ufs_google_send_uic_cmd(struct ufs_hba *hba,
				   struct uic_command *uic_cmd)
{
	guard(mutex)(&hba->uic_cmd_mutex);
	if (hba->android_quirks & UFSHCD_ANDROID_QUIRK_AH8_BREAKS_DME) {
		unsigned long flags;

		local_irq_save(flags);
		preempt_disable();
		__ufs_google_send_uic_cmd(hba, uic_cmd);
		preempt_enable();
		local_irq_restore(flags);
	} else {
		__ufs_google_send_uic_cmd(hba, uic_cmd);
	}
	return ufs_google_wait_for_uic_cmd(hba, uic_cmd);
}

static int ufs_google_dme_set(struct ufs_hba *hba, u32 attr_sel, u32 mib_val)
{
	struct uic_command uic_cmd = {
		.command = UIC_CMD_DME_SET,
		.argument1 = attr_sel,
		.argument2 = UIC_ARG_ATTR_TYPE(ATTR_SET_NOR),
		.argument3 = mib_val,
	};
	int ret;

	ufshcd_hold(hba);
	ret = ufs_google_send_uic_cmd(hba, &uic_cmd);
	ufshcd_release(hba);
	if (ret)
		dev_err(hba->dev, "google_dme_set failed ret=%d\n", ret);

	return ret;
}

static int ufs_google_dme_get(struct ufs_hba *hba, u32 attr_sel, u32 *mib_val)
{
	struct uic_command uic_cmd = {
		.command = UIC_CMD_DME_GET,
		.argument1 = attr_sel,
	};
	int ret;

	ufshcd_hold(hba);
	ret = ufs_google_send_uic_cmd(hba, &uic_cmd);
	ufshcd_release(hba);
	if (ret) {
		dev_err(hba->dev, "google_dme_get failed ret=%d\n", ret);
		return ret;
	}

	*mib_val = uic_cmd.argument3;

	return 0;
}

static void ufs_google_config_uic_intr(struct ufs_hba *hba, bool enable)
{
	u32 set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);

	if (enable)
		set |= UIC_COMMAND_COMPL;
	else
		set &= ~UIC_COMMAND_COMPL;

	ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);
}

static int ufs_google_mphy_indirect_reg_write(struct ufs_hba *hba,
					      const struct mphy_reg *reg)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;

	guard(mutex)(&host->indirect_reg_mutex);

	ufs_google_config_uic_intr(hba, false);

	ret = ufs_google_dme_set(hba, UIC_ARG_MIB(MPHY_G5_CBAPBPADDRLSB_OFFSET),
				 reg->addr & 0xFF);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPADDRLSB_OFFSET\n");
		goto out;
	}

	ret = ufs_google_dme_set(hba, UIC_ARG_MIB(MPHY_G5_CBAPBPADDRMSB_OFFSET),
				 reg->addr >> 8);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPADDRMSB_OFFSET\n");
		goto out;
	}

	ret = ufs_google_dme_set(hba,
				 UIC_ARG_MIB(MPHY_G5_CBAPBPWDATALSB_OFFSET),
				 reg->data & 0xFF);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPWDATALSB_OFFSET\n");
		goto out;
	}

	ret = ufs_google_dme_set(hba,
				 UIC_ARG_MIB(MPHY_G5_CBAPBPWDATAMSB_OFFSET),
				 reg->data >> 8);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPWDATAMSB_OFFSET\n");
		goto out;
	}

	ret = ufs_google_dme_set(hba,
				 UIC_ARG_MIB(MPHY_G5_CBAPBPWRITESEL_OFFSET),
				 0x1);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPWRITESEL_OFFSET\n");
	}

out:
	ufs_google_config_uic_intr(hba, true);

	return ret;
}

static int ufs_google_mphy_indirect_reg_read(struct ufs_hba *hba,
					     struct mphy_reg *reg)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	u32 data;
	int ret;

	guard(mutex)(&host->indirect_reg_mutex);

	ufs_google_config_uic_intr(hba, false);

	ret = ufs_google_dme_set(hba, UIC_ARG_MIB(MPHY_G5_CBAPBPADDRLSB_OFFSET),
				 reg->addr & 0xFF);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPADDRLSB_OFFSET\n");
		goto out;
	}

	ret = ufs_google_dme_set(hba, UIC_ARG_MIB(MPHY_G5_CBAPBPADDRMSB_OFFSET),
				 reg->addr >> 8);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPADDRMSB_OFFSET\n");
		goto out;
	}

	ret = ufs_google_dme_set(hba,
				 UIC_ARG_MIB(MPHY_G5_CBAPBPWRITESEL_OFFSET),
				 0x0);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPWRITESEL_OFFSET\n");
		goto out;
	}

	ret = ufs_google_dme_get(hba,
				 UIC_ARG_MIB(MPHY_G5_CBAPBPRDATALSB_OFFSET),
				 &data);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPRDATALSB_OFFSET\n");
		goto out;
	}
	reg->data = data & 0xFF;

	ret = ufs_google_dme_get(hba,
				 UIC_ARG_MIB(MPHY_G5_CBAPBPRDATAMSB_OFFSET),
				 &data);
	if (ret) {
		dev_err(hba->dev,
			"failed to set MPHY_G5_CBAPBPRDATAMSB_OFFSET\n");
		reg->data = 0;
	} else {
		reg->data |= data << 8;
	}

out:
	ufs_google_config_uic_intr(hba, true);

	return ret;
}

static void ufs_google_mphy_apb_mode_select(struct ufs_google_host *host,
					    bool enable)
{
	u32 data;

	data = ufs_top_csr_readl(host, REG_HSIOS_UFS_SRAM_APB_MODE);
	data &= ~UFS_SRAM_APB_MODE_MASK;
	data |= enable;
	ufs_top_csr_writel(host, data, REG_HSIOS_UFS_SRAM_APB_MODE);

	/* UFS HC databook suggests to wait 10 clock cycles of refclk(38.4MHz)
	 * which will be 260ns for the mode change to reflect.
	 * Documentation/timers/timers-howto.rst suggests to us udelay if
	 * SLEEPING FOR "A FEW" USECS ( < ~10us ) */
	 udelay(1);
}

static int ufs_google_set_done_bits(struct ufs_hba *hba)
{
	int ret;
	static const struct mphy_reg set_done_registers[] = {
		{ RAWLANEANONN_DIG_RX_AFE_DFE_CAL_DONE_LANE0, 0xc },
		{ RAWLANEANONN_DIG_RX_AFE_DFE_CAL_DONE_LANE1, 0xc },
		{ RAWLANEANONN_DIG_RX_STARTUP_CAL_ALGO_CTL_1_LANE0, 0x2 },
		{ RAWLANEANONN_DIG_RX_STARTUP_CAL_ALGO_CTL_1_LANE1, 0x2 },
		{ RAWLANEANONN_DIG_RX_CONT_ALGO_CTL_LANE0, 0x60 },
		{ RAWLANEANONN_DIG_RX_CONT_ALGO_CTL_LANE1, 0x60 },
	};
	struct mphy_reg mpll_status = {
		.addr = RAWCMN_DIG_AON_CMNCAL_MPLL_STATUS,
		.data = 0x2,
	};

	ret = ufs_google_mphy_indirect_reg_write(hba, &mpll_status);
	if (ret) {
		dev_err(hba->dev,
			"failed to write phy reg: 0x%x, value: 0x%x (%d)\n",
			mpll_status.addr, mpll_status.data, ret);
		return ret;
	}

	/* Registers below require read modify write with bit set */
	for (size_t i = 0; i < ARRAY_SIZE(set_done_registers); i++) {
		struct mphy_reg tmp_reg;

		tmp_reg.addr = set_done_registers[i].addr;

		ret = ufs_google_mphy_indirect_reg_read(hba, &tmp_reg);
		if (ret) {
			dev_err(hba->dev, "failed to read phy reg: 0x%x (%d)\n",
				tmp_reg.addr, ret);
			return ret;
		}

		tmp_reg.data |= set_done_registers[i].data;

		ret = ufs_google_mphy_indirect_reg_write(hba, &tmp_reg);
		if (ret) {
			dev_err(hba->dev,
				"failed to write phy reg: 0x%x, value: 0x%x (%d)\n",
				tmp_reg.addr, tmp_reg.data, ret);
			return ret;
		}
	}

	return 0;
}

static int ufs_google_apply_calibration(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	ktime_t calibration_start = ktime_get();
	size_t i;
	int ret;

	for (i = 0; i < host->phy_cal_size; i++) {
		ret = ufs_google_mphy_indirect_reg_write(hba,
							 &host->phy_cal_data[i]);
		if (ret) {
			dev_err(hba->dev,
				"failed to apply calibration parameter addr=%x data=%x\n",
				host->phy_cal_data[i].addr,
				host->phy_cal_data[i].data);
			goto out;
		}
	}

	ret = ufs_google_set_done_bits(hba);

out:
	dev_info(hba->dev, "applying calibration took %lld usec\n",
		 ktime_to_us(ktime_sub(ktime_get(), calibration_start)));

	return ret;
}

static int ufs_google_apply_mphy_pmc_war(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	static const struct mphy_reg pre_pmc_cfg = {
		.addr = SUP_DIG_ANA_XF_BG_OVRD_OUT,
		.data = 0xC,
	};
	static const struct mphy_reg post_pmc_cfg = {
		.addr = SUP_DIG_ANA_XF_BG_OVRD_OUT,
		.data = 0,
	};
	static const u32 lane_attrs[] = {
		PA_ACTIVERXDATALANES,
		PA_ACTIVETXDATALANES,
	};
	u32 mode;
	int ret;

	/* Apply necessary work around for HS-LSS case before PMC */
	if (host->caps & GCAP_HS_LSS) {
		ret = ufs_google_mphy_indirect_reg_write(hba, &pre_pmc_cfg);
		if (ret) {
			dev_err(hba->dev,
				"failed to set addr=%x data=%x (%d)\n",
				pre_pmc_cfg.addr, pre_pmc_cfg.data, ret);
			return ret;
		}
	}

	/* Perform PMC to 2x lanes */
	for (u32 i = 0; i < ARRAY_SIZE(lane_attrs); i++) {
		ret = ufshcd_dme_set(hba, UIC_ARG_MIB(lane_attrs[i]),
				     UFS_LANE_2);
		if (ret) {
			dev_err(hba->dev,
				"failed to set lane attribute %#x to 2 (%d)\n",
				lane_attrs[i], ret);
			return ret;
		}
	}

	/* Trigger PMC */
	mode = host->caps & GCAP_HS_LSS ? FASTAUTO_MODE : SLOWAUTO_MODE;
	ret = ufshcd_uic_change_pwr_mode(hba, mode << 4 | mode);
	if (ret) {
		dev_err(hba->dev,
			"failed to set perform power mode change (%d)\n", ret);
		return ret;
	}

	/* Apply necessary work around for HS-LSS case after PMC */
	if (host->caps & GCAP_HS_LSS) {
		ret = ufs_google_mphy_indirect_reg_write(hba, &post_pmc_cfg);
		if (ret)
			dev_err(hba->dev,
				"failed to set addr=%x data=%x (%d)\n",
				post_pmc_cfg.addr, post_pmc_cfg.data, ret);
	}

	return ret;
}

static int ufs_google_phy_initialization(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;
	u32 data;
	u64 fw_update_duration_us = 0;
	ktime_t fw_update_start;
	size_t attr_len = 0;
	const struct ufs_dme_attr *rmmi_attrs;
	bool skip_phy_init =
		device_property_read_bool(hba->dev, "google,skip-ufs-phy-init");

	/*
	 * MPHY_G5_CBULPH8_OFFSET controls PHY PG which should match HC PG
	 * configuration. If neither AH8 nor SWH8 support HC PG, then disable
	 * PHY as well. Otherwise enable it.
	 */
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(MPHY_G5_CBULPH8_OFFSET),
			     !!(host->caps &
				(GCAP_HC_AH8_PG | GCAP_HC_SWH8_PG)));
	if (ret)
		return ret;

	/* Trigger configuration update */
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(UNIPRO_DME_MPHY_CFG_UPD), 0x1);
	if (ret)
		return ret;

	if (!host->phy_init_needed) {
		dev_info(hba->dev, "phy init skipped");
		return 0;
	}

	/* Program RMMI attributes and update configuration */
	ret = ufs_plat_get_phy_rmmi_attrs(host, &rmmi_attrs, &attr_len);
	if (ret) {
		dev_err(hba->dev, "get rmmi attrs failed %d", ret);
		return ret;
	}
	ret = ufshcd_google_dme_set_seq(hba, rmmi_attrs, attr_len);
	if (ret)
		return ret;

	/*
	 * PDTE MPHY Setting Update: b/341119164
	 * Set value to 0 for both the lanes before phy reset release
	 */
	ret = ufs_plat_get_phy_rmmi_tx_eq_attrs(host, &rmmi_attrs, &attr_len);
	if (ret) {
		dev_err(hba->dev, "get tx eq rmmi attrs failed %d", ret);
		return ret;
	}
	ret = ufshcd_google_dme_set_seq(hba, rmmi_attrs, attr_len);
	if (ret)
		return ret;

	/* 3f. Release phy reset */
	ret = reset_control_deassert(host->phy_rst);
	if (ret)
		return ret;

	if (!skip_phy_init) {
		ret = readl_poll_timeout(host->ufs_top_mmio +
						 REG_HSIOS_UFS_PWR_CTRL,
					 data, data & UFS_SRAM_INIT_DONE_MASK,
					 PHY_STATUS_DELAY_US,
					 PHY_STATUS_TIMEOUT_US);
		if (ret)
			return ret;
	}

	/* 3g. Check if Rate B calibration needs to be applied */
	if (ufs_should_apply_phy_cal(host)) {
		ret = ufs_google_apply_calibration(hba);
		if (ret)
			return ret;
		host->calibration_needed = false;
	} else {
		dev_info(hba->dev, "skipping rate B calibration");
	}

	if (ufs_should_apply_phy_patch(host)) {
		const u32 *patch_data;
		size_t patch_sz;

		fw_update_start = ktime_get();

		/* Enable APB access mode for Phy SRAM */
		ufs_google_mphy_apb_mode_select(host, true);

		/* Apply UFS PHY SRAM patch */
		ret = ufs_plat_get_phy_fw_patch(host, &patch_data, &patch_sz);
		if (ret) {
			dev_err(hba->dev, "get mphy patch failed %d", ret);
			return ret;
		}
		memcpy_toio(host->ufs_phy_sram_mmio, patch_data, patch_sz);

		/* Disable APB access mode for Phy SRAM */
		ufs_google_mphy_apb_mode_select(host, false);

		/* 3h. Start FW calibraion */
		data = ufs_top_csr_readl(host, REG_HSIOS_UFS_PWR_CTRL);
		data |= UFS_SRAM_EXT_LD_DONE_MASK;
		ufs_top_csr_writel(host, data, REG_HSIOS_UFS_PWR_CTRL);

		fw_update_duration_us +=
			ktime_to_us(ktime_sub(ktime_get(), fw_update_start));

		dev_info(hba->dev, "phy firmware update took %lld usec\n",
			 fw_update_duration_us);

		host->phy_patching_needed = false;
	}

	/*
	 * Apply mphy configuration during phy init for asymmetric power mode
	 * change workaround.
	 */
	if ((host->caps & GCAP_MPHY_PMC_WAR) && (host->caps & GCAP_HS_LSS)) {
		static const struct mphy_reg mphy_cfg[] = {
			{ LANE1_DIG_RX_CDR_CDR_CTL_6, 0x2D56 },
			{ LANE2_DIG_RX_CDR_CDR_CTL_6, 0x2D56 },
		};
		for (u32 i = 0; i < ARRAY_SIZE(mphy_cfg); i++) {
			ret = ufs_google_mphy_indirect_reg_write(hba,
								 &mphy_cfg[i]);
			if (ret) {
				dev_err(hba->dev,
					"failed to write init war reg %x (%d)\n",
					mphy_cfg[i].addr, ret);
				return ret;
			}
		}
	}

	/* 3i. Enable MPHY */
	ret = ufshcd_dme_set(hba,
			   UIC_ARG_MIB(UNIPRO_DME_MPHY_DISABLE), 0x0);
	if (ret)
		return ret;

	if (skip_phy_init)
		return ret;

	/* 3j. Wait for MPHY ready */
	ret = ufs_plat_poll_phy_ready(host);
	if (ret)
		dev_err(hba->dev, "mphy not ready %d", ret);

	host->phy_init_needed = false;

	return ret;
}

/*
 * ufs_cport_setup - CPORT setup for DW UFS
 * @hba: host controller instance
 */
static int ufs_cport_setup(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	static const struct ufs_dme_attr connection_attrs[] = {
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 0x0, DME_LOCAL },
		{ UIC_ARG_MIB(N_DEVICEID), 0x0, DME_LOCAL },
		{ UIC_ARG_MIB(N_DEVICEID_VALID), 0x1, DME_LOCAL },
		{ UIC_ARG_MIB(T_PEERDEVICEID), 0x1, DME_LOCAL },
		{ UIC_ARG_MIB(T_PEERCPORTID), 0x0, DME_LOCAL },
		{ UIC_ARG_MIB(T_TRAFFICCLASS), 0x0, DME_LOCAL },
		{ UIC_ARG_MIB(T_CPORTFLAGS), 0x6, DME_LOCAL },
		{ UIC_ARG_MIB(T_CPORTMODE), 0x1, DME_LOCAL },
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 0x1, DME_LOCAL },
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 0x0, DME_PEER },
		{ UIC_ARG_MIB(N_DEVICEID), 0x1, DME_PEER },
		{ UIC_ARG_MIB(N_DEVICEID_VALID), 0x1, DME_PEER },
		{ UIC_ARG_MIB(T_PEERDEVICEID), 0x0, DME_PEER },
		{ UIC_ARG_MIB(T_PEERCPORTID), 0x0, DME_PEER },
		{ UIC_ARG_MIB(T_TRAFFICCLASS), 0x0, DME_PEER },
		{ UIC_ARG_MIB(T_CPORTFLAGS), 0x6, DME_PEER },
		{ UIC_ARG_MIB(T_CPORTMODE), 0x1, DME_PEER },
		{ UIC_ARG_MIB(T_CONNECTIONSTATE), 0x1, DME_PEER }
	};
	u32 conn_state;
	int ret;

	if (!(host->caps & GCAP_SKIP_CPORT_SETUP))
		return ufshcd_google_dme_set_seq(hba, connection_attrs,
						 ARRAY_SIZE(connection_attrs));

	ret = read_poll_timeout(ufshcd_dme_peer_get, ret,
				!ret && conn_state == CPORT_CONNECTED,
				CPORT_CONNECTION_DELAY_US,
				CPORT_CONNECTION_TIMEOUT_US, false, hba,
				UIC_ARG_MIB(T_CONNECTIONSTATE), &conn_state);
	if (ret)
		dev_err(hba->dev, "CPORT connection failed (%d)\n", ret);

	return ret;
}

static void ufs_pm_up(struct ufs_google_host *host)
{
	guard(mutex)(&host->ufs_pm_lock);

	if (host->pm_request_active)
		return;

	if (host->caps & GCAP_RSC_IP_IDLE)
		google_update_ip_idle_status(host->ip_idle_index, STATE_BUSY);

	ufs_clk_switch(host, host->hsios_aux_clk, true, GCAP_RSC_AUX_CLK);

	if (host->caps & GCAP_RSC_CPU_TOP_CL)
		pm_runtime_get_sync(host->cpu_top_cl);

	host->pm_request_active = true;
}

static void ufs_pm_down(struct ufs_google_host *host)
{
	guard(mutex)(&host->ufs_pm_lock);

	if (!host->pm_request_active)
		return;

	if (host->caps & GCAP_RSC_IP_IDLE)
		google_update_ip_idle_status(host->ip_idle_index, STATE_IDLE);

	if (host->caps & GCAP_RSC_AUX_CLK)
		clk_disable_unprepare(host->hsios_aux_clk);

	if (host->caps & GCAP_RSC_CPU_TOP_CL)
		pm_runtime_put_sync(host->cpu_top_cl);

	host->pm_request_active = false;
}

static inline void ufs_google_config_pm_lvl(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	guard(spinlock_irqsave)(hba->host->host_lock);

	if (host->pm_set)
		return;

	/* config default pm lvl only once */
	hba->rpm_lvl = host->caps & GCAP_LOCAL_RPM ? UFS_PM_LVL_0 :
						     UFS_PM_LVL_3;
	hba->spm_lvl = UFS_PM_LVL_5;
	host->pm_set = true;
}

struct dme_attr_requirement {
	struct ufs_dme_attr attr;
	int min_gear;
};

static int ufs_google_update_tactivate(struct ufs_hba *hba)
{
	u32 tactivate;
	int ret = 0;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(UNIPRO_L15_PA_T_ACTIVATE),
			     &tactivate);
	if (ret) {
		dev_err(hba->dev, "unable to get T_ACTIVATE, err %d\n", ret);
		return ret;
	}

	/*
	 * Synopsys recommends to update the host T_ACTIVATE by 55us after
	 * negotiating with device. b/410823571
	 */
	tactivate += 1;
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(UNIPRO_L15_PA_T_ACTIVATE),
			     tactivate);
	if (ret)
		dev_err(hba->dev, "unable to set T_ACTIVATE %u, err %d\n",
			tactivate, ret);
	return ret;
}

/*
 * ufs_google_link_startup_notify - Google specific link startup setup
 * @hba: host controller instance
 * @status: callback status from core driver
 */
static int ufs_google_link_startup_notify(struct ufs_hba *hba,
					  enum ufs_notify_change_status status)
{
	static const unsigned int lss_post_change_delay_us = 500;
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int err;
	u32 reg_clear_mask = 0;

	switch (status) {
	case PRE_CHANGE:
		/* Program clock divider register */
		ufshcd_writel(hba,
			      (HCLKDIV_VALUE << HCLKDIV_OFFSET) & HCLKDIV_MASK,
			      REG_HCLKDIV);

		ufs_plat_config_vs(host);

		err = ufshcd_vops_phy_initialization(hba);
		if (err) {
			dev_err(hba->dev,
				"PHY initialization failed, error: %d\n", err);
			return err;
		}
		break;
	case POST_CHANGE:
		/* TODO: b/456375140 - CPORT error */
		usleep_range(lss_post_change_delay_us,
			     lss_post_change_delay_us + 100);

		/* 4b. Host TActivate update */
		err = ufs_google_update_tactivate(hba);
		if (err)
			return err;

		/* Apply asymmetric power mode change workaround if needed */
		if (host->caps & GCAP_MPHY_PMC_WAR) {
			err = ufs_google_apply_mphy_pmc_war(hba);
			if (err)
				return err;
			dev_info(hba->dev,
				 "asymmetric power mode change workaround applied\n");
		}

		err = ufs_cport_setup(hba);
		if (err) {
			dev_err(hba->dev, "CPORT setup failed, error: %d\n",
				err);
			return err;
		}

		/* Perform full initialization while coming out of suspend */
		ufs_google_config_pm_lvl(hba);

		if (!(host->caps & GCAP_HC_AH8_PG)) {
			reg_clear_mask |= LP_AH8_PGE_MASK;
		}

		if (!(host->caps & GCAP_HC_SWH8_PG)) {
			reg_clear_mask |= LP_PGE_MASK;
		}

		if (reg_clear_mask) {
			u32 reg = ufshcd_readl(hba, REG_BUSTHRTL);
			reg &= ~reg_clear_mask;
			ufshcd_writel(hba, reg, REG_BUSTHRTL);
		}

		ufs_pm_up(host);

		break;
	}

	return 0;
}

static int
ufs_google_pwr_change_notify(struct ufs_hba *hba,
			     enum ufs_notify_change_status status,
			     struct ufs_pa_layer_attr *dev_max_params,
			     struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	struct ufs_host_params host_params;
	int ret = 0;

	if (!dev_req_params) {
		dev_err(hba->dev, "%s: incoming dev_req_params is NULL\n", __func__);
		return -EINVAL;
	}

	switch (status) {
	case PRE_CHANGE:
		ufshcd_init_host_params(&host_params);

		host_params.hs_rx_gear = host->google_pwr_mode.gear_rx;
		host_params.hs_tx_gear = host->google_pwr_mode.gear_tx;
		host_params.rx_lanes = host->google_pwr_mode.lane_rx;
		host_params.tx_lanes = host->google_pwr_mode.lane_tx;
		host_params.rx_pwr_hs = host->google_pwr_mode.pwr_rx;
		host_params.tx_pwr_hs = host->google_pwr_mode.pwr_tx;
		host_params.hs_rate = host->google_pwr_mode.hs_rate;

		ret = ufshcd_negotiate_pwr_params(&host_params,
						  dev_max_params,
						  dev_req_params);
		if (ret) {
			dev_err(hba->dev,
				"%s: failed to determine capabilities (%d)\n",
				__func__, ret);
			return ret;
		}

		if (dev_req_params->pwr_rx != dev_req_params->pwr_tx ||
		    dev_req_params->gear_rx != dev_req_params->gear_tx) {
			dev_err(hba->dev,
				"%s: rx/tx must be symmetrical pwr: %u/%u gear: %u/%u\n",
				__func__, dev_req_params->pwr_rx,
				dev_req_params->pwr_tx, dev_req_params->gear_rx,
				dev_req_params->gear_tx);
			return -EINVAL;
		}

		dev_info(hba->dev, "hs_gear=%u hs_series=%u hs_rate=%u\n",
			 dev_req_params->gear_rx, dev_req_params->pwr_rx,
			 dev_req_params->hs_rate);

		/* Set Adapt */
		if ((dev_req_params->pwr_rx == FAST_MODE ||
		     dev_req_params->pwr_rx == FASTAUTO_MODE) &&
		    (dev_req_params->gear_rx >= 4)) {
			ret = ufshcd_dme_set(hba,
					   UIC_ARG_MIB(PA_TXHSADAPTTYPE),
					   PA_INITIAL_ADAPT);
		} else {
			ret = ufshcd_dme_set(hba,
					   UIC_ARG_MIB(PA_TXHSADAPTTYPE),
					   PA_NO_ADAPT);
		}

		if (ret)
			dev_err(hba->dev, "%s: failed to set ADAPT(%d)\n",
				__func__, ret);
		ret = pixel_ufs_crypto_resume(hba);

		break;
	case POST_CHANGE:
		/*
		 * We would like to override the default clock gate delay
		 * value, which is set to 150 ms during ufshcd_init().
		 * Unfortunately ufshcd_init() is called after
		 * ufshcd_vops_init(), and we are unable to set it there.
		 * We would also like to only set it once, so that we do not
		 * override a value that might have been set via the
		 * clkgate_delay_ms sysfs. Performing the operation here will
		 * update the value after the first transition to high speed
		 * gear.
		 */
		if (!host->clkgate_delay_set &&
		    !(host->caps & GCAP_LOCAL_RPM)) {
			ufshcd_clkgate_delay_set(hba->dev, LOW_POWER_DELAY_MS);
			host->clkgate_delay_set = true;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ufs_google_phy_setup_vreg(struct ufs_hba *hba, bool on)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	switch (hba->uic_link_state) {
	case UIC_LINK_ACTIVE_STATE:
	case UIC_LINK_BROKEN_STATE:
	case UIC_LINK_OFF_STATE:
		ret = ufs_google_toggle_vreg(hba, host->vdd1p2, on);
		fallthrough;
	case UIC_LINK_HIBERN8_STATE:
		/* toggle only 0.75v if link is in H8 */
		ret = ufs_google_toggle_vreg(hba, host->vdd0p75, on);
		break;
	}

	return ret;
}

static int ufs_google_pre_link_off(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	if (host->caps & GCAP_LOCAL_RPM && host->caps & GCAP_HC_SWH8_PG) {
		/*
		 * Despite disabling software hibern8 via the clock gating work
		 * queue, the core driver will still enter hibern8 internally
		 * prior to suspending the device. Disable power gating here to
		 * eliminate the risk of failure due to it.
		 */
		u32 val;
		int ret;

		ret = ufshcd_dme_set(hba, UIC_ARG_MIB(MPHY_G5_CBULPH8_OFFSET),
				     0);
		if (ret)
			return ret;

		ret = ufshcd_dme_set(hba, UIC_ARG_MIB(UNIPRO_DME_MPHY_CFG_UPD),
				     0x1);

		val = ufshcd_readl(hba, REG_BUSTHRTL);
		val &= ~LP_PGE_MASK;
		ufshcd_writel(hba, val, REG_BUSTHRTL);
	}

	return 0;
}

/*
 * Bring the UFS device and also the link between the host controller and the
 * UFS device to a lower power state. The @op argument indicates whether this
 * function is called from the context of system suspend or from the context of
 * runtime suspend.
 */
static int ufs_google_suspend(struct ufs_hba *hba, enum ufs_pm_op op,
			      enum ufs_notify_change_status status)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	switch (op) {
	case UFS_RUNTIME_PM:
		switch (status) {
		case PRE_CHANGE:
			if (hba->rpm_lvl == UFS_PM_LVL_5)
				ret = ufs_google_pre_link_off(hba);
			break;
		case POST_CHANGE:
			/* TODO: b/444072483 - remove refclk control to avoid PG timeout */
			ufs_google_phy_setup_vreg(hba, false);
			break;
		}
		break;
	case UFS_SHUTDOWN_PM:
		if (status == PRE_CHANGE) {
			ret = ufs_google_pre_link_off(hba);
		} else if (status == POST_CHANGE) {
			ret = ufs_google_set_device_off(host);
			if (ret) {
				dev_err(hba->dev, "set device off failed (%d)",
					ret);
				return ret;
			}
		}
		break;
	case UFS_SYSTEM_PM:
		switch (status) {
		case PRE_CHANGE:
			if (host->caps & GCAP_REF_CLK_ACG) {
				ret = ufs_google_set_acg(hba, false);
				if (ret) {
					dev_err(hba->dev,
						"set acg off failed (%d)", ret);
					return ret;
				}
			}

			ufs_clk_switch(host, host->hsios_cp_core_clk, false,
				       GCAP_RSC_CP_CORE_CLK);

			ret = ufs_google_pre_link_off(hba);
			break;
		case POST_CHANGE:
			ufs_google_phy_setup_vreg(hba, false);
#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
			pixel_ufs_notify_system_pm(hba, true);
#endif
			ufs_pm_down(host);
			ufs_google_pd_put_sync(host);

			ret = ufs_google_set_device_off(host);
			if (ret) {
				dev_err(hba->dev, "set device off failed (%d)",
					ret);
				/* fail instantly */
				return -EIO;
			}
			break;
		}
		break;
	}

	return ret;
}

/*
 * Power up the UFS device and also the link between the host controller and the
 * UFS device. The @op argument indicates whether this function is called from
 * the context of system resume or from the context of runtime resume.
 */
static int ufs_google_resume(struct ufs_hba *hba, enum ufs_pm_op op)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;

	ufs_google_phy_setup_vreg(hba, true);

	if (op == UFS_SYSTEM_PM) {
		ret = ufs_google_set_device_on(host);
		if (ret) {
			dev_err(hba->dev, "set device on failed (%d)", ret);
			/* fail instantly */
			return -EIO;
		}

		ufs_google_pd_get_sync(host);
#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
		pixel_ufs_notify_system_pm(hba, false);
#endif
		if (host->caps & GCAP_REF_CLK_ACG) {
			ret = ufs_google_set_acg(hba, true);
			if (ret) {
				dev_err(hba->dev, "set acg on failed (%d)",
					ret);
				return ret;
			}
		}

		ufs_clk_switch(host, host->hsios_cp_core_clk, true,
			       GCAP_RSC_CP_CORE_CLK);
	} else if (op == UFS_RUNTIME_PM) {
		if (host->caps & GCAP_LOCAL_RPM) {
			cancel_delayed_work(&host->ufs_sleep_work);
		}
		/* TODO: b/444072483 - remove refclk control to avoid PG timeout */
	}

	return 0;
}

static void ufs_hibern8_notify(struct ufs_hba *hba, enum uic_cmd_dme cmd,
			       enum ufs_notify_change_status status)
{
	switch (cmd) {
	case UIC_CMD_DME_HIBER_ENTER:
		switch (status) {
		case PRE_CHANGE:
			/*
			 * Synopsys recommends disabling AH8 before initiating SWH8 when using both
			 * AH8 and SWH8 together. Starting software hibernation without first
			 * disabling auto-hibernate may lead to unexpected behavior.
			 */
			ufs_auto_hibern8_update(hba, false);
			break;
		case POST_CHANGE:
			ufs_pm_down(ufshcd_get_variant(hba));
			break;
		default:
			break;
		}
		break;
	case UIC_CMD_DME_HIBER_EXIT:
		switch (status) {
		case PRE_CHANGE:
			ufs_pm_up(ufshcd_get_variant(hba));
			break;
		case POST_CHANGE:
			ufs_auto_hibern8_update(hba, true);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
	if (status == POST_CHANGE)
		pixel_ufs_record_hibern8(hba, cmd == UIC_CMD_DME_HIBER_ENTER);
#endif
}

static int ufs_google_get_hba_mac(struct ufs_hba *hba)
{
	return MAX_SUPP_MAC;
}

static const struct ufs_google_res_info ufs_res_info[] = {
	{.name = "ufs_mem",},
	{.name = "mcq",},
	/* Submission Queue Doorbell Address Offset */
	{.name = "mcq_sqd",},
	/* Submission Queue Interrupt Status */
	{.name = "mcq_sqis",},
	/* Completion Queue Doorbell Address Offset */
	{.name = "mcq_cqd",},
	/* Completion Queue Interrupt Status */
	{.name = "mcq_cqis",},
	/* MCQ vendor specific */
	{.name = "mcq_vs",},
};

static int ufs_google_map_memory_area_as_resource(struct ufs_hba *hba,
						  int res_info_index, uint32_t mem_offset)
{
	struct resource *res_mem, *res_mcq;
	struct platform_device *pdev = to_platform_device(hba->dev);
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	struct ufs_google_res_info *res = &host->res[res_info_index];
	static const u32 mem_offsets[] = {
		[UFS_RES_MCQ] = MCQ_QCFG_SIZE,
		[UFS_RES_MCQ_SQD] = MAX_REG_SQDAO,
		[UFS_RES_MCQ_SQIS] = MAX_REG_SQIS,
		[UFS_RES_MCQ_CQD] = MAX_REG_CQDAO,
		[UFS_RES_MCQ_CQIS] = MAX_REG_CQIS,
	};
	int ret = 0;

	res_mem = &pdev->resource[0];

	if (res_info_index == UFS_RES_UFS) {
		res->resource = res_mem;
		res->base = hba->mmio_base;
		goto out;
	}

	/* Explicitly allocate MCQ resource from ufs_mem */
	res_mcq = devm_kzalloc(hba->dev, sizeof(*res_mcq), GFP_KERNEL);
	if (!res_mcq) {
		ret = -ENOMEM;
		goto out;
	}
	res->resource = res_mcq;

	res_mcq->start = mem_offset;
	res_mcq->flags = res_mem->flags;
	res_mcq->name = ufs_res_info[res_info_index].name;
	WARN_ON_ONCE(!res_mcq->name);
	WARN_ON_ONCE(res_info_index >= ARRAY_SIZE(mem_offsets));
	WARN_ON_ONCE(mem_offsets[res_info_index] == 0);
	res_mcq->end = res_mcq->start +
		       hba->nr_hw_queues * mem_offsets[res_info_index] - 1;

	ret = insert_resource(&iomem_resource, res_mcq);
	if (ret) {
		dev_err(hba->dev, "Failed to insert MCQ resource, err=%d\n",
			ret);
		goto out;
	}

	res->base = devm_ioremap_resource(hba->dev, res_mcq);
	if (IS_ERR(res->base)) {
		dev_err(hba->dev, "MCQ %s registers mapping failed, err=%d\n",
			res->name,
			(int)PTR_ERR(res->base));
		ret = PTR_ERR(res->base);
		goto ioremap_err;
	}
	if (res_info_index == UFS_RES_MCQ)
		hba->mcq_base = res->base;
	goto out;

ioremap_err:
	res->base = NULL;
	remove_resource(res_mcq);
out:
	return ret;
}

static int ufs_google_mcq_config_resource(struct ufs_hba *hba)
{
	/* Setting up the memory backed registers for queue configuration. */
	struct platform_device *pdev = to_platform_device(hba->dev);
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	struct resource *res_mem;
	int ret;

	memcpy(host->res, ufs_res_info, sizeof(ufs_res_info));

	res_mem = &pdev->resource[0];

	ret = ufs_google_map_memory_area_as_resource(hba, UFS_RES_UFS, 0);
	if (ret)
		dev_err(hba->dev, "Failed to create UFS memory resource\n");
	ret = ufs_google_map_memory_area_as_resource(
		hba, UFS_RES_MCQ,
		res_mem->start + MCQ_SQATTR_OFFSET(hba->mcq_capabilities));
	if (ret)
		dev_err(hba->dev, "Failed to create MCQ queue memory resource\n");
	ret = ufs_google_map_memory_area_as_resource(hba, UFS_RES_MCQ_SQD,
						     res_mem->start +
							     MCQ_SQDAO);
	if (ret)
		dev_err(hba->dev, "Failed to create MCQ SQOPS memory resource\n");
	ret = ufs_google_map_memory_area_as_resource(hba, UFS_RES_MCQ_SQIS,
						     res_mem->start +
							     MCQ_SQINT);
	if (ret)
		dev_err(hba->dev, "Failed to create MCQ SQINT memory resource\n");
	ret = ufs_google_map_memory_area_as_resource(hba, UFS_RES_MCQ_CQD,
						     res_mem->start +
							     MCQ_CQDAO);
	if (ret)
		dev_err(hba->dev, "Failed to create MCQ CQOPS memory resource\n");
	ret = ufs_google_map_memory_area_as_resource(hba, UFS_RES_MCQ_CQIS,
						     res_mem->start +
							     MCQ_CQINT);
	if (ret)
		dev_err(hba->dev, "Failed to create MCQ CQINT memory resource\n");

	return ret;
}

static int ufs_google_op_runtime_config(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	struct ufs_google_res_info *mem_res, *mcq_res;
	struct ufshcd_mcq_opr_info_t *opr;
	unsigned long long mem_base_address;
	int res_index;
	static const unsigned long strides[] = {
		MAX_REG_SQDAO, MAX_REG_SQIS, MAX_REG_CQDAO, MAX_REG_CQIS
	};

	mem_res = &host->res[UFS_RES_UFS];
	if (!mem_res->base)
		return -EINVAL;
	mem_base_address = mem_res->resource->start;

	/*
	 * Stride should take us from queue 0 to queue 1
	 * Offset is the start of the memory area in relation to the ufs base.
	 * example: reg = mcq_opr_base(hba, OPR_SQD, id) + REG_SQRTS;
	 *          return opr->base + (opr->stride * i);
	 * array size is nr_hw_queues in size
	 */
	for (res_index = UFS_RES_MCQ_SQD; res_index <= UFS_RES_MCQ_CQIS;
	     ++res_index) {
		mcq_res = &host->res[res_index];
		if (!mcq_res->base)
			return -EINVAL;
		opr = &hba->mcq_opr[OPR_SQD + res_index - UFS_RES_MCQ_SQD];
		opr->offset = mcq_res->resource->start - mem_base_address;
		opr->stride = strides[res_index - UFS_RES_MCQ_SQD];
		opr->base = mcq_res->base;
	}

	return 0;
}

static void __iomem *mcq_opr_base(struct ufs_hba *hba,
				  enum ufshcd_mcq_opr n, int i)
{
	struct ufshcd_mcq_opr_info_t *opr = &hba->mcq_opr[n];

	return opr->base + opr->stride * i;
}

/**
 * ufs_google_mcq_intr - multi interrupt service routine
 * @irq: irq number
 * @__host: pointer to host instance
 *
 * Return:
 *  IRQ_HANDLED - If interrupt is valid
 *  IRQ_NONE    - If invalid interrupt
 */
static irqreturn_t ufs_google_mcq_intr(int irq, void *desc_p)
{
	struct cq_irq_desc *desc = desc_p;
	struct ufs_google_host *host = desc->host;
	unsigned int qid = desc->qid;
	struct ufs_hba *hba = host->hba;
	struct ufs_hw_queue *hwq;
	u32 events;

	hwq = &hba->uhq[qid];

	/* clear CQIS */
	events = ufshcd_mcq_read_cqis(hba, qid);
	if (events)
		ufshcd_mcq_write_cqis(hba, events, qid);

	/* clear IAG counter by writing 1 to MCQIACRy.CTR */
	writel(FIELD_PREP(IACTH, 1) | CTR | IAPWEN | IAEN,
	       mcq_opr_base(hba, OPR_CQIS, qid) + REG_MCQIACR);

	/* process CQy and update CQ head pointer */
	if (events & UFSHCD_MCQ_CQIS_TAIL_ENT_PUSH_STS)
		ufshcd_mcq_poll_cqe_lock(hba, hwq);

	return IRQ_HANDLED;
}

static int ufs_google_config_multi_interrupt(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	struct platform_device *pdev = to_platform_device(hba->dev);
	u32 nr_irqs, maxq, set, legacy_irq;
	irq_handler_t ufshcd_intr = NULL;
	irq_handler_t ufshcd_threaded_intr = NULL;
	int ret, i;

	if (device_property_read_bool(hba->dev, "google,skip-mcq-multi-intr"))
		return -EOPNOTSUPP;

	/* Poll queues use do not require multi interrupt */
	maxq = FIELD_GET(MAX_QUEUE_SUP, hba->mcq_capabilities) + 1;
	nr_irqs = hba->nr_hw_queues - hba->nr_queues[HCTX_TYPE_POLL];
	if (nr_irqs > maxq)
		return -EINVAL;

	/* Disable CQEE(using quirk) and IAGEE */
	set = ufshcd_readl(hba, REG_INTERRUPT_ENABLE);
	set &= ~MCQ_IAG_EVENT_STATUS;
	ufshcd_writel(hba, set, REG_INTERRUPT_ENABLE);

	for (i = 0; i < maxq; ++i) {
		/* Completion Queue Configuration */
		ufsmcq_writel(hba, IAGVLD | FIELD_PREP(IAG_MASK, i),
			      MCQ_CFG_n(REG_CQCFG, i));

		/* Completion Queue Interrupt Status Address Offset */
		ufsmcq_writelx(hba, MCQ_OPR_OFFSET_n(OPR_CQIS, i),
			       MCQ_CFG_n(REG_CQISAO, i));

		/* Interrupt Aggregation Configuration */
		writel(FIELD_PREP(IACTH, 1) | CTR | IAPWEN | IAEN,
		       mcq_opr_base(hba, OPR_CQIS, i) + REG_MCQIACR);
	}

	if (host->multi_intr_enabled)
		return 0;

	/*
	 * Hack: save the legacy interrupt handler pointer and free the legacy
	 * interrupt. Without this, devm_platform_get_irqs_affinity() fails with
	 * -EBUSY. TODO(b/359673199): remove this hack.
	 */
	legacy_irq = platform_get_irq(pdev, 0);
	struct irq_desc *desc = irq_to_desc(legacy_irq);
	if (desc && desc->action) {
		ufshcd_intr = desc->action->handler;
		ufshcd_threaded_intr = desc->action->thread_fn;
		devm_free_irq(hba->dev, legacy_irq, hba);
	}

	struct irq_affinity affd = {
		.pre_vectors = 1,
	};
	int *irqs;

	ret = devm_platform_get_irqs_affinity(pdev, &affd, /*minvec=*/1,
					      /*maxvec=*/nr_irqs + 1, &irqs);
	if (ufshcd_intr) {
		int err;

		/*
		 * Hack: restore the legacy interrupt handler. The code below
		 * is a duplicate of code in the UFS core driver and must be
		 * kept in sync with the UFS core driver.
		 */
		if (ufshcd_threaded_intr) {
			err = devm_request_threaded_irq(hba->dev, legacy_irq,
				ufshcd_intr, ufshcd_threaded_intr,
				IRQF_ONESHOT | IRQF_SHARED, UFSHCD, hba);
		} else {
			err = devm_request_irq(hba->dev, legacy_irq,
					       ufshcd_intr, IRQF_SHARED, UFSHCD,
					       hba);
		}
		if (err) {
			pr_crit("devm_request_irq() failed: %d\n", err);
			BUG();
		}
		irq_set_status_flags(hba->irq, IRQ_DISABLE_UNLAZY);
	}

	if (ret < nr_irqs + 1) {
		WARN_ONCE(true, "ret (%d) < nr_irqs + 1 (%d)\n", ret,
			  nr_irqs + 1);
		goto out;
	}

	for (i = 0; i < nr_irqs; ++i) {
		struct cq_irq_desc *desc = &host->cq_desc[i];
		*desc = (typeof(*desc)){ .host = host, .qid = i };
		ret = devm_request_irq(hba->dev, irqs[i + 1],
				       ufs_google_mcq_intr, IRQF_SHARED,
				       "ufshcd-cq", desc);
		if (ret) {
			dev_err(hba->dev, "%i request irq failed\n", i);
			goto out;
		}
	}

	host->multi_intr_enabled = true;
out:
	return ret;
}

static int ufs_google_device_reset(struct ufs_hba *hba)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	/*
	 * In prior to getting this, GENPD_NOTIFY_PRE_ON and
	 * GENPD_NOTIFY_ON were called by generic resume callback.
	 * Hence, returning zero makes ufshcd_set_ufs_dev_active()
	 * which avoids link_startup_again = true in ufshcd_link_startup.
	 */
	/*
	 * Limit physical reset to error handling and Micron devices if
	 * GCAP_LIMIT_RESET_TO_EH is set (e.g. LGA).
	 *
	 * Background:
	 * Originally, the physical reset was executed unconditionally. However,
	 * resetting during normal suspend/resume or initialization caused stability
	 * issues on other vendor devices (Samsung/Kioxia) and Micron devices.
	 *
	 * To prevent these issues, on platforms with GCAP_LIMIT_RESET_TO_EH, we
	 * restrict the physical reset to Micron UFS devices during active error
	 * recovery (under ufshcd_eh_in_progress).
	 *
	 * If GCAP_LIMIT_RESET_TO_EH is not set (e.g. MBU), it defaults to the
	 * legacy behaviour of always resetting unconditionally.
	 */
	if (!(host->caps & GCAP_LIMIT_RESET_TO_EH) ||
	    (hba->dev_info.wmanufacturerid == UFS_VENDOR_MICRON &&
	     ufshcd_eh_in_progress(hba))) {
		gpiod_set_value(host->resetb, 0);
		usleep_range(RESETB_DELAY_MIN_US, RESETB_DELAY_MAX_US);
		gpiod_set_value(host->resetb, 1);
	}

	return 0;
}

static void ufs_google_event_notify(struct ufs_hba *hba,
				    enum ufs_event_type evt, void *data)
{
	if (trace_ufs_google_event_enabled()) {
		u32 val = data ? *((u32 *)data) : 0;
		const char *evt_str;

		if (evt >= 0 && evt < ARRAY_SIZE(ufs_event_type_str) &&
		    ufs_event_type_str[evt]) {
			evt_str = ufs_event_type_str[evt];
		} else {
			evt_str = "UFS_EVT_UNKNOWN";
		}

		trace_ufs_google_event(PERFETTO_EVENT_INSTANT, evt_str, val);
	}

#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
	struct pixel_ufs *ufs = to_pixel_ufs(hba);

	if (!ufs->enable_uic_err_dbg)
		return;

	const unsigned long uic_err_mask =
		BIT(UFS_EVT_PA_ERR) | BIT(UFS_EVT_DL_ERR) |
		BIT(UFS_EVT_NL_ERR) | BIT(UFS_EVT_TL_ERR) |
		BIT(UFS_EVT_DME_ERR);

	/* Detect UIC error only*/
	if (evt < BITS_PER_LONG && (BIT(evt) & uic_err_mask))
		panic("ufshcd has received an UIC error(type: %u).", evt);
#endif
}

static void ufs_google_config_scsi_dev(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct ufs_hba *hba = shost_priv(shost);
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int err;

	/* do not use slow FUA */
	sdev->broken_fua = 1;

	if (hba->luns_avail == 1) {
		/*
		 * The Auto Clock Gating (ACG) feature requires the clk_gating_wait_us
		 * device parameter which is read in ufshcd_device_params_init().
		 * To avoid using a default value (0xFF) that causes high latency,
		 * we enabled ACG after parameter is read from device.
		 */
		if (host->caps & GCAP_REF_CLK_ACG) {
			ufs_plat_set_refclk_control(host, true);

			err = ufs_google_set_acg(hba, true);
			if (err)
				dev_err(hba->dev, "set acg on failed (%d)",
					err);
		}
	}
}

static int ufs_google_hce_enable_notify(struct ufs_hba *hba,
					enum ufs_notify_change_status status)
{
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;

	if (status != PRE_CHANGE || !ufshcd_eh_in_progress(hba))
		return 0;

	/* avoid sleep work calling on ufshcd_rpm_get_sync() */
	cancel_delayed_work_sync(&host->ufs_sleep_work);

	if (host->caps & GCAP_REF_CLK_ACG) {
		ret = ufs_google_set_acg(hba, false);
		if (ret) {
			dev_err(hba->dev, "set acg off failed (%d)", ret);
			return ret;
		}
	}

	/* power cycle device and host controller */
	ufs_google_set_device_off(host);
	ufs_google_pd_put_sync(host);
	/* TODO: b/458531873 - CPM missing state polling */
	usleep_range(PD_RESET_DELAY_US, PD_RESET_DELAY_US + 100);
	ufs_google_pd_get_sync(host);
	ufs_google_set_device_on(host);

	if (host->caps & GCAP_REF_CLK_ACG) {
		ret = ufs_google_set_acg(hba, true);
		if (ret) {
			dev_err(hba->dev, "set acg on failed (%d)", ret);
			return ret;
		}
	}
	dev_info(hba->dev, "full reset done");

	return 0;
}

static const struct ufs_hba_variant_ops ufs_hba_google_vops = {
	.name = "google-ufs",
	.init = ufs_google_init,
	.set_dma_mask = ufs_google_lga_set_dma_mask,
	.apply_dev_quirks = ufs_google_apply_dev_quirks,
	.link_startup_notify = ufs_google_link_startup_notify,
	.phy_initialization = ufs_google_phy_initialization,
	.pwr_change_notify = ufs_google_pwr_change_notify,
	.suspend = ufs_google_suspend,
	.resume = ufs_google_resume,
	.hibern8_notify = ufs_hibern8_notify,
	.get_hba_mac = ufs_google_get_hba_mac,
	.mcq_config_resource = ufs_google_mcq_config_resource,
	.op_runtime_config = ufs_google_op_runtime_config,
	.config_esi = ufs_google_config_multi_interrupt,
	.device_reset = ufs_google_device_reset,
	.event_notify = ufs_google_event_notify,
	.config_scsi_dev = ufs_google_config_scsi_dev,
	.hce_enable_notify = ufs_google_hce_enable_notify,
};

static const struct of_device_id ufs_google_of_match[] = {
	{ .compatible = "google,ufshc", .data = &ufs_hba_google_vops },
	{},
};
MODULE_DEVICE_TABLE(of, ufs_google_of_match);

static int ufs_google_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct ufs_hba *hba;

	match = of_match_node(ufs_google_of_match, dev->of_node);
	err = ufshcd_pltfrm_init(pdev, match->data);
	if (err) {
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);
		return err;
	}

	hba = platform_get_drvdata(pdev);

	ufs_google_init_debugfs(hba);

	/* TODO(b/294685860): check requirement for lazy irq disable */
	irq_set_status_flags(hba->irq, IRQ_DISABLE_UNLAZY);

	return 0;
}

static void ufs_google_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba = platform_get_drvdata(pdev);
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	cancel_delayed_work_sync(&host->ufs_sleep_work);
	atomic_dec(&ufs_host_index);

	ufs_google_remove_debugfs(hba);
	ufs_google_remove_dbg(hba);
	ufshcd_remove(hba);

	/* turn off power domain after host is disabled */
	ufs_google_pd_put_sync(host);
	dev_pm_genpd_remove_notifier(host->ufs_pd);
	ufs_google_detach_power_domains(host);

	ufs_google_set_device_off(host);
	ufs_google_phy_setup_vreg(hba, false);
}

static int ufs_google_psm_wait_for_state(struct ufs_google_host *host,
					 void __iomem *status_reg_mmio,
					 u32 target_state, const char *id)
{
	u32 val;

	/* Poll the status register until the target state is reached */
	if (readl_poll_timeout(status_reg_mmio, val,
			       (val & PSM_STATUS_VALID_STATE_MASK) ==
				       target_state,
			       PSM_POLL_DELAY_US, PSM_POLL_TIMEOUT_US)) {
		dev_err(host->hba->dev,
			"timed out waiting for %s PSM to transition to %#x, status=0x%08x\n",
			id, target_state, val);
		return -ETIMEDOUT;
	}

	return 0;
}

static int ufs_google_uic_hibern8_enter(struct ufs_google_host *host)
{
	struct uic_command uic_cmd = {
		.command = UIC_CMD_DME_HIBER_ENTER,
	};
	struct ufs_hba *hba = host->hba;
	u32 val;
	int ret;

	/* Let's fail early in case our expectations are wrong. */
	if (!ufshcd_is_link_active(hba)) {
		dev_err(hba->dev, "uic link in invalid state (%d)\n",
			hba->uic_link_state);
		return -EINVAL;
	}

	/*
	 * We follow the HPG's section "5a. Entering SW-driven Hibernate".
	 * We skip updating BUSTHRTL per hibern8 entry as it is done within
	 * link startup notifier.
	 * We skip updating CBULPH8 as it is already done during phy
	 * initialization.
	 * We issue UICCMD DME_HIBERNATE_ENTER per HPG. The READ UNTIL
	 * requirement for UCCS=1 and WRITE to clear is fulfilled within
	 * ufs_google_send_uic_cmd().
	 */
	ret = ufs_google_send_uic_cmd(hba, &uic_cmd);
	if (ret) {
		dev_err(hba->dev, "failed to hibern8 enter (%d)\n", ret);
		return ret;
	}

	/* READ UNTIL UHES=1 */
	if (read_poll_timeout(ufshcd_readl, val, val & UIC_HIBERNATE_ENTER,
			      UIC_COMMAND_DELAY_US, UIC_COMMAND_POLL_TIMEOUT_US,
			      false, hba, REG_INTERRUPT_STATUS)) {
		dev_err(hba->dev, "h8 enter uic completion timeout val=%08x\n",
			val);
		return -ETIMEDOUT;
	}
	/* WRITE to clear UHES */
	ufshcd_writel(hba, UIC_HIBERNATE_ENTER, REG_INTERRUPT_STATUS);

	ufshcd_set_link_hibern8(hba);

	/* READ HCS.UPMCRS (bits 10:08) - confirm PWR_LOCAL */
	val = (ufshcd_readl(hba, REG_CONTROLLER_STATUS) >> 8) & 0x7;
	if (val != PWR_LOCAL) {
		dev_err(hba->dev, "hibern8 entry failed HCS.UPMCRS %#x\n", val);
		return -EINVAL;
	}

	/* The PSM will only move to PG if enabled*/
	if (host->caps & GCAP_HC_SWH8_PG) {
		ret = ufs_google_psm_wait_for_state(host,
						    host->ufs_hc_psm_status_mmio,
						    PSM_STATUS_VALID_STATE_PG,
						    "HC");
		if (ret)
			return ret;

		ret = ufs_google_psm_wait_for_state(host,
						    host->ufs_phy_psm_status_mmio,
						    PSM_STATUS_VALID_STATE_PG,
						    "PHY");
		if (ret)
			return ret;
	}
#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
	pixel_ufs_record_hibern8(hba, true);
#endif
	return 0;
}

static int ufs_google_uic_hibern8_exit(struct ufs_google_host *host)
{
	struct uic_command uic_cmd = {
		.command = UIC_CMD_DME_HIBER_EXIT,
	};
	struct ufs_hba *hba = host->hba;
	u32 val;
	int ret;

	/* Let's fail early in case our expectations are wrong. */
	if (!ufshcd_is_link_hibern8(hba)) {
		dev_err(hba->dev, "uic link in invalid state (%d)\n",
			hba->uic_link_state);
		return -EINVAL;
	}

	/*
	 * We follow the HPG's section "5b. Exiting SW-driven Hibernate".
	 * We issue UICCMD DME_HIBERNATE_EXIT per HPG. Although the HPG makes
	 * clearing the UIC argument registers optional, clear these anyway such
	 * that we can use an existing function for submitting the DME command.
	 */
	ret = ufs_google_send_uic_cmd(hba, &uic_cmd);
	if (ret) {
		dev_err(hba->dev, "failed to hibern8 enter (%d)\n", ret);
		return ret;
	}

	/* READ UNTIL UHXS=1 */
	if (read_poll_timeout(ufshcd_readl, val, val & UIC_HIBERNATE_EXIT,
			      UIC_COMMAND_DELAY_US, UIC_COMMAND_POLL_TIMEOUT_US,
			      false, hba, REG_INTERRUPT_STATUS)) {
		dev_err(hba->dev, "h8 exit uic completion timeout val=%08x\n",
			val);
		return -ETIMEDOUT;
	}
	/* WRITE to clear UHXS */
	ufshcd_writel(hba, UIC_HIBERNATE_EXIT, REG_INTERRUPT_STATUS);

	ufshcd_set_link_active(hba);

	/* READ HCS.UPMCRS (bits 10:08) - confirm PWR_LOCAL */
	val = (ufshcd_readl(hba, REG_CONTROLLER_STATUS) >> 8) & 0x7;
	if (val != PWR_LOCAL) {
		dev_err(hba->dev, "hibern8 exit failed HCS.UPMCRS %#x\n", val);
		return -EINVAL;
	}

	ret = ufs_google_psm_wait_for_state(host, host->ufs_hc_psm_status_mmio,
					    PSM_STATUS_VALID_STATE_ON, "HC");
	if (ret)
		return ret;

	ret = ufs_google_psm_wait_for_state(host, host->ufs_phy_psm_status_mmio,
					    PSM_STATUS_VALID_STATE_ON, "PHY");
	if (ret)
		return ret;
#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
	pixel_ufs_record_hibern8(hba, false);
#endif
	return 0;
}

/* Perform runtime suspend of the UFS host controller. */
static int ufs_google_runtime_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	bool enter_deep_h8 = false;
	bool reenable_irq = false;
	int retries;
	int ret;

	if (!(host->caps & GCAP_LOCAL_RPM))
		return ufshcd_runtime_suspend(dev);

	/*
	 * We only support these pm levels:
	 * UFS_PM_LVL_0: UFS_ACTIVE_PWR_MODE & UIC_LINK_ACTIVE_STATE
	 * UFS_PM_LVL_2: UFS_SLEEP_PWR_MODE & UIC_LINK_ACTIVE_STATE
	 * UFS_PM_LVL_5: UFS_POWERDOWN_PWR_MODE & UIC_LINK_OFF_STATE
	 *
	 * google runtime suspend use cases:
	 * 1. Low latency / high frequency state which replaces clk_gating work
	 *    in the core driver. Here we want to achieve two things:
	 *    a) Allow the link to enter hibern8 either via the local software
	 *       hibern8 or auto hibern8.
	 *    b) Call ufs_pm_down() to vote ourselves out of system
	 *        dependencies.
	 *    This is the default state, and is called with UFS_PM_LVL_0.
	 * 2. Higher Latency / Lower frequency state which adds UFS_SLEEP. Due
	 *    to the higher latency, we are setting a longer timeout to reach
	 *    here. This is called with UFS_PM_LVL_2, where the core driver
	 *    will put the device into SLEEP power state, but keep the link
	 *    active. We will then put the link into hibern8 via the local
	 *    hibern8 or auto hibern8, and call ufs_pm_down as before.
	 *    The state is meant to be set and reached after a certain amount
	 *    of inactivity higher than the runtime_pm autosuspend timeout.
	 * 3. Major latency / low frequency state, where the ufs device and the
	 *    link are powered off entirely. This is called with UFS_PM_LVL_5
	 *    which is set externally for Pixel specific use cases.
	 */
	switch (hba->rpm_lvl) {
	case UFS_PM_LVL_0:
	case UFS_PM_LVL_2:
		break;
	case UFS_PM_LVL_5:
		return ufshcd_runtime_suspend(dev);
	default:
		dev_err(hba->dev, "unsupported rpm_lvl=%d\n", hba->rpm_lvl);
		return -EINVAL;
	}

	if (host->caps & GCAP_LOCAL_SWH8) {
		ufs_auto_hibern8_update(hba, false);

		if (hba->is_irq_enabled) {
			disable_irq(hba->irq);
			hba->is_irq_enabled = false;
			reenable_irq = true;
		}

		for (retries = 0; retries < SW_H8_ENTRY_ATTEMPTS; retries++) {
			ret = ufs_google_uic_hibern8_enter(host);
			if (!ret) {
				if (host->caps & GCAP_HC_SWH8_PG)
					enter_deep_h8 = true;
				break;
			}
			ufs_google_uic_hibern8_exit(host);
		}

		if (reenable_irq) {
			enable_irq(hba->irq);
			hba->is_irq_enabled = true;
		}

		if (ret)
			return ret;
	} else if (ufshcd_is_auto_hibern8_supported(hba)) {
		ret = ufs_google_psm_wait_for_state(host,
						    host->ufs_hc_psm_status_mmio,
						    PSM_STATUS_VALID_STATE_PG,
						    "HC");
		if (ret)
			return ret;

		ret = ufs_google_psm_wait_for_state(host,
						    host->ufs_phy_psm_status_mmio,
						    PSM_STATUS_VALID_STATE_PG,
						    "PHY");
		if (ret)
			return ret;

		if (host->caps & GCAP_HC_AH8_PG)
			enter_deep_h8 = true;
	}

	if (enter_deep_h8)
		ufs_clk_switch(host, host->deep_h8_clk, false,
			       GCAP_RSC_DEEP_H8_CLK);

	ufs_pm_down(host);
	if (hba->rpm_lvl == UFS_PM_LVL_0)
		schedule_delayed_work(&host->ufs_sleep_work,
				      msecs_to_jiffies(UFS_SLEEP_DELAY_MS));

	ufs_clk_switch(host, host->hsios_cp_core_clk, false,
		       GCAP_RSC_CP_CORE_CLK);

#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
	pixel_update_power_event(hba, PE_H8_ENTER);
#endif

	return ufshcd_runtime_suspend(dev);
}

/* Perform runtime resume of the UFS host controller. */
static int ufs_google_runtime_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	bool reenable_irq = false;
	int ret;

	ret = ufshcd_runtime_resume(dev);
	if (!(host->caps & GCAP_LOCAL_RPM) || hba->rpm_lvl == UFS_PM_LVL_5 ||
	    ret)
		return ret;

	ufs_clk_switch(host, host->hsios_cp_core_clk, true,
		       GCAP_RSC_CP_CORE_CLK);

	/* In case of UFS_PM_LVL_5, ufs_pm_up is called from LSS notify */
	ufs_pm_up(host);

	ufs_clk_switch(host, host->deep_h8_clk, true, GCAP_RSC_DEEP_H8_CLK);

	if (host->caps & GCAP_LOCAL_SWH8) {
		if (hba->is_irq_enabled) {
			disable_irq(hba->irq);
			hba->is_irq_enabled = false;
			reenable_irq = true;
		}

		ret = ufs_google_uic_hibern8_exit(host);

		if (reenable_irq) {
			enable_irq(hba->irq);
			hba->is_irq_enabled = true;
		}

		if (!ret)
			ufs_auto_hibern8_update(hba, true);
	}

#if IS_ENABLED(CONFIG_UFS_PIXEL_FEATURES)
	pixel_update_power_event(hba, PE_H8_EXIT);
#endif

	scoped_guard(spinlock_irqsave, hba->host->host_lock)
		hba->rpm_lvl = UFS_PM_LVL_0;

	return ret;
}

static void ufs_google_sleep_handler(struct work_struct *work)
{
	struct ufs_google_host *host =
		container_of(work, struct ufs_google_host, ufs_sleep_work.work);
	struct ufs_hba *hba = host->hba;

	ufshcd_rpm_get_sync(hba);
	scoped_guard(spinlock_irqsave, hba->host->host_lock)
		hba->rpm_lvl = UFS_PM_LVL_2;
	ufshcd_rpm_put_sync(hba);
}

/*
 * ufs_google_system_suspend and ufs_google_system_resume support host
 * controller RPM. They manage the UFS device and link state directly
 * because this logic does not fit within standard RPM callbacks.
 */
static int ufs_google_system_suspend(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_google_host *host = ufshcd_get_variant(hba);
	int ret;

	ret = ufshcd_system_suspend(dev);
	if (ret)
		return ret;

	if (host->caps & GCAP_DECOUPLE_UFS_EN)
		ufs_google_mark_device_off(host);

	return 0;
}

static int ufs_google_system_resume(struct device *dev)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_google_host *host = ufshcd_get_variant(hba);

	/*
	 * If GCAP_DECOUPLE_UFS_EN is set, VCC and VCCQ will be turned on
	 * in ufshcd_system_resume() instead of ufs_google_set_device_on(),
	 * thus add an extra wait here.
	 */
	if (host->caps & GCAP_DECOUPLE_UFS_EN)
		ufs_google_wait_device_ready(host);

	return ufshcd_system_resume(dev);
}

static const struct dev_pm_ops ufs_google_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufs_google_system_suspend, ufs_google_system_resume)
	.runtime_suspend = ufs_google_runtime_suspend,
	.runtime_resume = ufs_google_runtime_resume,
	.prepare = ufshcd_suspend_prepare,
	.complete = ufshcd_resume_complete,
};

static struct platform_driver ufs_google_pltform = {
	.probe = ufs_google_probe,
	.remove = ufs_google_remove,
	.driver = {
		.name = "google-ufshcd",
		.pm = &ufs_google_pm_ops,
		.of_match_table = of_match_ptr(ufs_google_of_match),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(ufs_google_pltform);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google UFS Controller Driver");
MODULE_LICENSE("GPL");
