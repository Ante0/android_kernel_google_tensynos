// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU functions for "Destiny" SoCs.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

#if IS_ENABLED(CONFIG_GOOGLE_MEM_QOS)
#include <mem-qos/google_qos_box_reg_api.h>
#endif

#include "edgetpu-config.h"
#include "edgetpu-device-group.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pixel-trim.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "mobile-soc-destiny.h"

#if EDGETPU_USE_HW_WDT
/* WDT_CONTROL CSR bits */
#define WDT_CTRL_ENABLED	0
#define WDT_CTRL_KEY_ENABLED	2
#define WDT_CTRL_INT_CLEAR	5

/* WDT KEY PIN */
#define WDT_KEY_PIN		0xA55AA55A
#endif /* EDGETPU_USE_HW_WDT */

#define SHUTDOWN_DELAY_US_MIN 200
#define SHUTDOWN_DELAY_US_MAX 200
#define SHUTDOWN_MAX_DELAY_COUNT 100

/* LPB_SSWRP_TPU_CSRS register offsets and fields and values. */
#define LPB_TPU_INT_STATUS_FIELD	GENMASK(1, 0)
#define LPB_TPU_INT_STATUS_OFF		0
#define LPB_TPU_INT_STATUS_BUSY_ON	1
#define LPB_TPU_INT_STATUS_ON		2
#define LPB_TPU_INT_STATUS_BUSY_OFF	3

/* LPM_CORE_CSR fields */
#define LPM_CORE_PWR_STATE		BIT(0)
#define LPM_CORE_CURR_PCR_STATE		GENMASK(17, 13)

/*
 * If chip headers don't define LPB CSR defaults then use these defaults, will skip LPB access if
 * device tree doesn't define or if emulation/tests system without LPB.
 */
#ifndef LPB_SSWRP_DEFAULT_TPU_CSRS
#define LPB_SSWRP_DEFAULT_TPU_CSRS 0
#define LPB_SSWRP_DEFAULT_TPU_CSRS_SIZE 0
#endif

#if EDGETPU_USE_HW_WDT
static void edgetpu_wdt_set(struct edgetpu_dev *etdev, uint core, bool enable)
{
	unsigned int wdt_ctrl_csr;
	unsigned int wdt_key_csr;
	u32 ctrlval;

#if EDGETPU_NUM_CORES == 2
	wdt_ctrl_csr = core ?  EDGETPU_REG_WDT1_CONTROL : EDGETPU_REG_WDT0_CONTROL;
	wdt_key_csr = core ?  EDGETPU_REG_WDT1_KEY : EDGETPU_REG_WDT0_KEY;
#else
	wdt_ctrl_csr = EDGETPU_REG_WDT0_CONTROL;
	wdt_key_csr = EDGETPU_REG_WDT0_KEY;
#endif

	/* Unlock WDT */
	edgetpu_dev_write_32(etdev, wdt_key_csr, WDT_KEY_PIN);
	ctrlval = edgetpu_dev_read_32(etdev, wdt_ctrl_csr);
	etdev_dbg(etdev, "%s: core %u wdt_ctrl=%#x\n", __func__, core, ctrlval);
	if (!(ctrlval & BIT(WDT_CTRL_KEY_ENABLED)))
		etdev_err_ratelimited(etdev, "wdt for core %u not unlocked", core);

	if (enable) {
		ctrlval |= BIT(WDT_CTRL_ENABLED);
	} else {
		ctrlval &= ~(BIT(WDT_CTRL_ENABLED));
		ctrlval |= BIT(WDT_CTRL_INT_CLEAR);
	}

	edgetpu_dev_write_32(etdev, wdt_ctrl_csr, ctrlval);
	/* Relock WDT */
	edgetpu_dev_write_32(etdev, wdt_key_csr, 0x0);
}

/* Handle a watchdog timer interrupt. */
static irqreturn_t edgetpu_soc_wdt_irq_handler(int irq, void *arg)
{
	struct edgetpu_dev *etdev = arg;
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	uint core;

	if (!soc_data->wdt_irq)
		return IRQ_NONE;
	for (core = 0; core < etdev->num_cores; core++) {
		if (irq == soc_data->wdt_irq[core])
			break;
	}
	if (core == etdev->num_cores)
		return IRQ_NONE;

	etdev_err(etdev, "core %u watchdog timeout interrupt\n", core);
	/* Clear the WDT interrupt (on at least 1 core) and disable WDT on all cores. */
	for (core = 0; core < etdev->num_cores; core++)
		edgetpu_wdt_set(etdev, core, false);

	/* Restart control cluster */
	edgetpu_watchdog_bite(etdev);
	return IRQ_HANDLED;
}
#endif /* EDGETPU_USE_HW_WDT */

int edgetpu_soc_prepare_firmware(struct edgetpu_dev *etdev)
{
	return 0;
}

static int bcl_mitigation_send(struct edgetpu_dev *etdev)
{
	int ret;

	if (!etdev->soc_data->bcl_mitigation_valid)
		return 0;
	ret = edgetpu_kci_bcl_mitigation(etdev->etkci, &etdev->soc_data->bcl_mitigation);
	return ret ? -EIO : 0;
}

static int bcl_mitigation_send_if_powered(struct edgetpu_dev *etdev)
{
	int ret;

	ret = edgetpu_pm_get_if_powered(etdev, true);
	if (ret)
		return 0;
	ret = bcl_mitigation_send(etdev);
	edgetpu_pm_put(etdev);
	return ret;
}


void edgetpu_soc_pm_post_fw_start(struct edgetpu_dev *etdev)
{
	bcl_mitigation_send(etdev);
}

int edgetpu_soc_handle_reverse_kci(struct edgetpu_dev *etdev,
				   struct gcip_kci_response_element *resp)
{
	return -EOPNOTSUPP;
}

long edgetpu_soc_pm_get_rate(struct edgetpu_dev *etdev, int flags)
{
	u32 regval, pfstate;

	if (IS_ENABLED(CONFIG_EDGETPU_TEST))
		return 0;
	/* TPU block must be powered, if not cur_freq is zero. */
	if (edgetpu_pm_get_if_powered(etdev, true))
		return 0;
	regval = edgetpu_dev_read_32(etdev, EDGETPU_LPM_CORE_CSR);
	edgetpu_pm_put(etdev);
	/* If LpmCoreCsr.lpmCorePwrState is zero lpm_core PSM is power gated. */
	if (!(regval & LPM_CORE_PWR_STATE))
		return 0;
	pfstate = FIELD_GET(LPM_CORE_CURR_PCR_STATE, regval);
	if (pfstate > etdev->num_active_states) {
		etdev_warn_ratelimited(etdev, "LpmCore pfstate %u outside state table\n", pfstate);
		return etdev->max_active_state;
	}
	return etdev->active_states[pfstate];
}

void edgetpu_soc_pm_power_down(struct edgetpu_dev *etdev)
{
}

bool edgetpu_soc_pm_is_block_off(struct edgetpu_dev *etdev)
{
	u32 internal_status;

	if (edgetpu_pm_always_on(etdev))
		return false;

	internal_status = readl(etdev->soc_data->lpb_sswrp_csrs + LPB_TPU_INT_STATUS);
	etdev_dbg(etdev, "lpb int status=%u rail status=%u\n",
		  internal_status, readl(etdev->soc_data->lpb_sswrp_csrs + LPB_TPU_RAIL_STATUS));
	return FIELD_GET(LPB_TPU_INT_STATUS_FIELD, internal_status) == LPB_TPU_INT_STATUS_OFF;
}

void edgetpu_soc_pm_lpm_down(struct edgetpu_dev *etdev)
{
	int timeout_cnt = 0;
	u32 val;

	/*
	 * Poll for LpmControlCsr.lpmCtlPwrState = 0, indicating control cluster is power gated
	 * (and firmware has executed its shutdown sequence).
	 */
	do {
		usleep_range(SHUTDOWN_DELAY_US_MIN, SHUTDOWN_DELAY_US_MAX);
		val = edgetpu_dev_read_32_sync(etdev, EDGETPU_REG_LPM_CONTROL);
		if (!(val & LPM_CTRL_LPMCTLPWRSTATE))
			break;
		timeout_cnt++;
	} while (timeout_cnt < SHUTDOWN_MAX_DELAY_COUNT);
	if (timeout_cnt == SHUTDOWN_MAX_DELAY_COUNT) {
		/* Warn and continue to attempt block shutdown. */
		etdev_warn(etdev, "control cluster shutdown timeout lpmctrl=%#x lpmcore=%#x\n",
			   val, edgetpu_dev_read_32(etdev, EDGETPU_LPM_CORE_CSR));
		edgetpu_soc_pm_dump_block_state(etdev);
	} else {
		etdev_dbg(etdev, "control cluster shutdown success lpmctrl=%#x\n", val);
	}
}

int edgetpu_soc_pm_lpm_up(struct edgetpu_dev *etdev)
{
	return 0;
}

/* Log TPU block power state for debugging. The block is not required to be powered up. */
void edgetpu_soc_pm_dump_block_state(struct edgetpu_dev *etdev)
{
	uint32_t psm0, psm1, psm2;

	if (edgetpu_pm_always_on(etdev))
		return;

	etdev_warn(etdev, "lpb int status=%u rail status=%u\n",
		   readl(etdev->soc_data->lpb_sswrp_csrs + LPB_TPU_INT_STATUS),
		   readl(etdev->soc_data->lpb_sswrp_csrs + LPB_TPU_RAIL_STATUS));

	if (!etdev->soc_data->lpcm_lpm_csrs)
		return;
	if (pm_runtime_get_if_active(etdev->dev) <= 0) {
		etdev_info(etdev, "pm_runtime not active, skip PSM state dump.");
		return;
	}

	psm0 = readl(etdev->soc_data->lpcm_lpm_csrs + LPCM_LPM_TPU_PSM0_STATUS);
	psm1 = readl(etdev->soc_data->lpcm_lpm_csrs + LPCM_LPM_TPU_PSM1_STATUS);
	psm2 = readl(etdev->soc_data->lpcm_lpm_csrs + LPCM_LPM_TPU_PSM2_STATUS);
	pm_runtime_put(etdev->dev);
	etdev_warn(etdev, "psm0=%#x psm1=%#x psm2=%#x\n", psm0, psm1, psm2);
}

static int mitigation_response_en_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	*val = etdev->soc_data->bcl_mitigation.mitigation_response_en;
	return 0;
}

static int mitigation_response_en_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;

	etdev->soc_data->bcl_mitigation.mitigation_response_en = val;
	etdev_info(etdev, "Set MITIGATION_RESPONSE_EN=%#llx\n", val);
	etdev->soc_data->bcl_mitigation_valid = true;
	return bcl_mitigation_send_if_powered(etdev);
}

static int mitigation_response_type_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	*val = etdev->soc_data->bcl_mitigation.mitigation_response_type;
	return 0;
}

static int mitigation_response_type_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;

	etdev->soc_data->bcl_mitigation.mitigation_response_type = val;
	etdev_info(etdev, "Set MITIGATION_RESPONSE_TYPE=%#llx\n", val);
	etdev->soc_data->bcl_mitigation_valid = true;
	return bcl_mitigation_send_if_powered(etdev);
}

static int mitigation_response_hyst_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	*val = etdev->soc_data->bcl_mitigation.mitigation_response_hyst;
	return 0;
}

static int mitigation_response_hyst_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;

	etdev->soc_data->bcl_mitigation.mitigation_response_hyst = val;
	etdev_info(etdev, "Set MITIGATION_RESPONSE_HYST=%#llx\n", val);
	etdev->soc_data->bcl_mitigation_valid = true;
	return bcl_mitigation_send_if_powered(etdev);
}

static int mitigation_div_2_ratio_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	*val = etdev->soc_data->bcl_mitigation.div_2_ratio;
	return 0;
}

static int mitigation_div_2_ratio_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;

	etdev->soc_data->bcl_mitigation.div_2_ratio = val;
	etdev_info(etdev, "Set DIV_2_RATIO=%#llx\n", val);
	etdev->soc_data->bcl_mitigation_valid = true;
	return bcl_mitigation_send_if_powered(etdev);
}

static int mitigation_div_4_ratio_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	*val = etdev->soc_data->bcl_mitigation.div_4_ratio;
	return 0;
}

static int mitigation_div_4_ratio_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;

	etdev->soc_data->bcl_mitigation.div_4_ratio = val;
	etdev_info(etdev, "Set DIV_4_RATIO=%#llx\n", val);
	etdev->soc_data->bcl_mitigation_valid = true;
	return bcl_mitigation_send_if_powered(etdev);
}

static int mitigation_fll_step_down_mux_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	*val = etdev->soc_data->bcl_mitigation.mitigation_fll_step_down_mux;
	return 0;
}

static int mitigation_fll_step_down_mux_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;

	etdev->soc_data->bcl_mitigation.mitigation_fll_step_down_mux = val;
	etdev_info(etdev, "Set MITIGATION_FLL_STEP_DOWN_MUX=%#llx\n", val);
	etdev->soc_data->bcl_mitigation_valid = true;
	return bcl_mitigation_send_if_powered(etdev);
}

static int thermal_heavy_mitigation_div_ratio_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;

	*val = etdev->soc_data->bcl_mitigation.thermal_heavy_mitigation_div_ratio;
	return 0;
}

static int thermal_heavy_mitigation_div_ratio_set(void *data, u64 val)
{
	struct edgetpu_dev *etdev = data;

	etdev->soc_data->bcl_mitigation.thermal_heavy_mitigation_div_ratio = val;
	etdev_info(etdev, "Set THERMAL_HEAVY_MITIGATION_DIV_RATIO=%#llx\n", val);
	etdev->soc_data->bcl_mitigation_valid = true;
	return bcl_mitigation_send_if_powered(etdev);
}

DEFINE_DEBUGFS_ATTRIBUTE(mitigation_response_en_fops, mitigation_response_en_get,
			 mitigation_response_en_set, "0x%llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(mitigation_response_type_fops, mitigation_response_type_get,
			 mitigation_response_type_set, "0x%llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(mitigation_response_hyst_fops, mitigation_response_hyst_get,
			 mitigation_response_hyst_set, "0x%llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(mitigation_div_2_ratio_fops, mitigation_div_2_ratio_get,
			 mitigation_div_2_ratio_set, "0x%llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(mitigation_div_4_ratio_fops, mitigation_div_4_ratio_get,
			 mitigation_div_4_ratio_set, "0x%llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(mitigation_fll_step_down_mux_fops, mitigation_fll_step_down_mux_get,
			 mitigation_fll_step_down_mux_set, "0x%llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(thermal_heavy_mitigation_div_ratio_fops,
			 thermal_heavy_mitigation_div_ratio_get,
			 thermal_heavy_mitigation_div_ratio_set, "0x%llx\n");

static int sswrp_power_state_get(void *data, u64 *val)
{
	struct edgetpu_dev *etdev = data;
	u32 internal_status;

	if (edgetpu_pm_always_on(etdev)) {
		*val = LPB_TPU_INT_STATUS_ON;
		etdev_info(etdev, "always on is enabled, return status ON and skip LPB read\n");
		return 0;
	}

	internal_status = readl(etdev->soc_data->lpb_sswrp_csrs + LPB_TPU_INT_STATUS);
	*val = FIELD_GET(LPB_TPU_INT_STATUS_FIELD, internal_status);
	etdev_info(etdev, "lpb int status=%u rail status=%u\n",
		   internal_status, readl(etdev->soc_data->lpb_sswrp_csrs + LPB_TPU_RAIL_STATUS));
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(sswrp_power_state_fops, sswrp_power_state_get, NULL, "%llu\n");

int edgetpu_soc_pm_init(struct edgetpu_dev *etdev)
{
	/* Setup BCL mitigation config default values and debugfs override interface. */
	/* Assume firmware also has copies of the default values and doesn't need those sent. */
	etdev->soc_data->bcl_mitigation_valid = false;
	etdev->soc_data->bcl_mitigation.version = BCL_MITIGATION_CONFIG_VERSION;
	etdev->soc_data->bcl_mitigation.mitigation_response_en = MITIGATION_RESPONSE_EN_DEFAULT;
	etdev->soc_data->bcl_mitigation.mitigation_response_type = MITIGATION_RESPONSE_TYPE_DEFAULT;
	etdev->soc_data->bcl_mitigation.mitigation_response_hyst = MITIGATION_RESPONSE_HYST_DEFAULT;
	etdev->soc_data->bcl_mitigation.div_2_ratio = LIGHT_MITIGATION_DIV_RATIO_DEFAULT;
	etdev->soc_data->bcl_mitigation.div_4_ratio = HEAVY_MITIGATION_DIV_RATIO_DEFAULT;
	etdev->soc_data->bcl_mitigation.thermal_heavy_mitigation_div_ratio =
		THERMAL_HEAVY_MITIGATION_DIV_RATIO_DEFAULT;
	etdev->soc_data->bcl_mitigation.mitigation_fll_step_down_mux =
		MITIGATION_FLL_STEP_DOWN_DEFAULT;
	debugfs_create_file("mitigation_response_en", 0660, etdev->d_entry, etdev,
			    &mitigation_response_en_fops);
	debugfs_create_file("mitigation_response_type", 0660, etdev->d_entry, etdev,
			    &mitigation_response_type_fops);
	debugfs_create_file("mitigation_response_hyst", 0660, etdev->d_entry, etdev,
			    &mitigation_response_hyst_fops);
	debugfs_create_file("mitigation_div_2_ratio", 0660, etdev->d_entry, etdev,
			    &mitigation_div_2_ratio_fops);
	debugfs_create_file("mitigation_div_4_ratio", 0660, etdev->d_entry, etdev,
			    &mitigation_div_4_ratio_fops);
	debugfs_create_file("mitigation_fll_step_down_mux", 0660, etdev->d_entry, etdev,
			    &mitigation_fll_step_down_mux_fops);
	debugfs_create_file("thermal_heavy_mitigation_div_ratio", 0660, etdev->d_entry, etdev,
			    &thermal_heavy_mitigation_div_ratio_fops);

	/* Destiny SoC family-specific power/ attrs. */
	debugfs_create_file("sswrp_power_state", 0440, etdev->pm->debugfs_dir, etdev,
			    &sswrp_power_state_fops);

	/*
	 * If ALWAYS_ON isn't enabled but device tree has no LPB specified,
	 * enable ALWAYS_ON.
	 */
	if (!etdev->pm->always_on && !etdev->soc_data->lpb_sswrp_csrs) {
		etdev_warn(etdev, "no lpb csr base in device tree, disable power management");
		etdev->pm->always_on = true;
	}
	return 0;
}

void edgetpu_soc_pm_exit(struct edgetpu_dev *etdev)
{
}

void edgetpu_soc_thermal_init(struct edgetpu_dev *etdev)
{
}

void edgetpu_soc_thermal_exit(struct edgetpu_dev *etdev)
{
}

int edgetpu_soc_check_supplier_devices(struct device *dev)
{
#if IS_ENABLED(CONFIG_GOOGLE_MEM_QOS)
	int num_qos_boxes;
	int i;
	int ret;
	struct qos_box_dev *qos_box;
	u32 vc_map_cfg_val = 0;

	/*
	 * The QoS box driver will program virtual channel mappings, declared in the device-tree.
	 * The TPU driver cannot probe unless all TPU QoS box devices have finished probing, or the
	 * VC mappings may not yet be ready.
	 */
	num_qos_boxes = of_property_count_strings(dev->of_node, "google,qos_box_dev_names");
	if (num_qos_boxes <= 0) {
		dev_dbg(dev, "No QoS boxes found in device tree (%d)", num_qos_boxes);
		return 0;
	}

	for (i = 0; i < num_qos_boxes; i++) {
		const char *qos_box_name;

		ret = of_property_read_string_index(dev->of_node, "google,qos_box_dev_names", i,
						    &qos_box_name);
		if (ret) {
			dev_err(dev, "Failed to read QoS box %d's name (%d)", i, ret);
			return ret;
		}

		qos_box = get_qos_box_dev_by_name(dev, qos_box_name);
		if (IS_ERR(qos_box)) {
			ret = PTR_ERR(qos_box);
			if (ret == -EPROBE_DEFER)
				dev_dbg(dev, "QoS Box %s not yet available", qos_box_name);
			else
				dev_err(dev, "Failed to find QoS box %s (%d)", qos_box_name, ret);
			return ret;
		}
		ret = google_qos_box_vc_map_cfg_read(qos_box, &vc_map_cfg_val);
		dev_dbg(dev, "QoS box %s cfg = %#0x (ret=%d)", qos_box_name, vc_map_cfg_val, ret);
	}
#endif /* IS_ENABLED(CONFIG_GOOGLE_MEM_QOS) */

	return 0;
}

#if IS_ENABLED(CONFIG_PIXEL_TRIM)
static unsigned long edgetpu_pixel_trim(void *private)
{
	struct edgetpu_dev *etdev = private;

	edgetpu_trim_buffers(etdev);
	return 0;
}

void edgetpu_pixel_trim_register(struct edgetpu_dev *etdev)
{
	int ret;

	etdev->soc_data->edgetpu_pixel_trim.pixel_trim.name = "edgetpu";
	etdev->soc_data->edgetpu_pixel_trim.pixel_trim.trim = edgetpu_pixel_trim;
	etdev->soc_data->edgetpu_pixel_trim.pixel_trim.private = etdev;
	INIT_LIST_HEAD(&etdev->soc_data->edgetpu_pixel_trim.pixel_trim.list);
	ret = register_trim(&etdev->soc_data->edgetpu_pixel_trim.pixel_trim);
	if (ret)
		etdev_warn(etdev, "pixel trim register returns %d\n", ret);
	else
		etdev->soc_data->edgetpu_pixel_trim.registered = true;
}

void edgetpu_pixel_trim_unregister(struct edgetpu_dev *etdev)
{
	if (etdev->soc_data->edgetpu_pixel_trim.registered)
		unregister_trim(&etdev->soc_data->edgetpu_pixel_trim.pixel_trim);
}
#endif

int edgetpu_soc_early_init(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev = to_platform_device(etdev->dev);
	struct device_node *np = etdev->dev->of_node;
	struct resource *res;
	u32 lpb_sswrp_base = LPB_SSWRP_DEFAULT_TPU_CSRS,
	    lpb_sswrp_size = LPB_SSWRP_DEFAULT_TPU_CSRS_SIZE;
	u32 val;
	u32 of_data_array[256];
	u32 num_columns, table_size;
	int i;

	etdev->soc_data = devm_kzalloc(&pdev->dev, sizeof(*etdev->soc_data), GFP_KERNEL);
	if (!etdev->soc_data)
		return -ENOMEM;

	if (of_property_read_u32_array(np, "gcip-dvfs-table-size", of_data_array, 2)) {
		etdev_err(etdev, "gcip-dvfs-table-size property missing from device tree");
		return -EINVAL;
	}

	/* TODO document table format. */
	etdev->num_active_states = of_data_array[0];
	num_columns = of_data_array[1];
	table_size = etdev->num_active_states * num_columns;
	if (table_size > 256) {
		etdev_err(etdev, "Too many dvfs states in device-tree");
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, "gcip-dvfs-table", of_data_array, table_size)) {
		etdev_err(etdev, "gcip-dvfs-table property missing from device tree");
		return -EINVAL;
	}

	etdev->active_states =
		devm_kcalloc(&pdev->dev, etdev->num_active_states, sizeof(u32), GFP_KERNEL);
	if (!etdev->active_states)
		return -ENOMEM;
	etdev->max_active_state = 0;
	for (i = 0; i < etdev->num_active_states; i++) {
		etdev->active_states[i] = of_data_array[num_columns * i];
		if (etdev->active_states[i] > etdev->max_active_state)
			etdev->max_active_state = etdev->active_states[i];
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpcm");
	if (res) {
		void __iomem *lpcm_base = devm_ioremap_resource(&pdev->dev, res);

		if (IS_ERR(lpcm_base)) {
			etdev_warn(etdev, "Failed to map LPCM register base: %ld",
				   PTR_ERR(lpcm_base));
			return PTR_ERR(lpcm_base);
		}

		etdev->soc_data->lpcm_lpm_csrs = lpcm_base;
	}

	if (of_find_property(np, "lpb-sswrp-base", NULL) &&
	    !of_property_read_u32_index(np, "lpb-sswrp-base", 0, &val))
		lpb_sswrp_base = val;
	if (of_find_property(np, "lpb-sswrp-size", NULL) &&
	    !of_property_read_u32_index(np, "lpb-sswrp-size", 0, &val))
		lpb_sswrp_size = val;

	if (lpb_sswrp_base)
		etdev->soc_data->lpb_sswrp_csrs = ioremap(lpb_sswrp_base, lpb_sswrp_size);

	edgetpu_pixel_trim_register(etdev);
	return 0;
}

int edgetpu_soc_post_power_on_init(struct edgetpu_dev *etdev)
{
	return 0;
}

void edgetpu_soc_exit(struct edgetpu_dev *etdev)
{
	edgetpu_pixel_trim_unregister(etdev);

	if (etdev->soc_data->lpb_sswrp_csrs)
		iounmap(etdev->soc_data->lpb_sswrp_csrs);
}

int edgetpu_soc_activate_context(struct edgetpu_dev *etdev, int pasid)
{
	/* Nothing need to be done, SMMU has all contexts always activated. */
	return 0;
}
void edgetpu_soc_deactivate_context(struct edgetpu_dev *etdev, int pasid)
{
}

void edgetpu_soc_set_tpu_cpu_security(struct edgetpu_dev *etdev)
{
#ifdef EDGETPU_REG_INSTRUCTION_REMAP_AXIUSER_CORE0
	const u32 mailboxId = 0, ssid = 0, ssidValid = 0, ipInitId = 0;
	/* Leave tpuContextId alone; set rai, raci, vc, pid to default value 0 */
	const u32 axiuser =
		(mailboxId << 28) | (ssid << 20) | (ssidValid << 19) | (ipInitId << 9);
	int i;

	for (i = 0; i < etdev->num_cores; i++) {
		edgetpu_dev_write_32(etdev, EDGETPU_REG_INSTRUCTION_REMAP_AXIUSER_CORE0 + 8 * i,
				     axiuser);
		edgetpu_dev_write_32(etdev, EDGETPU_REG_AXIUSER_CORE0 + 8 * i, axiuser);
	}
#endif
}

int edgetpu_soc_setup_irqs(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev = to_platform_device(etdev->dev);
	struct edgetpu_soc_data *soc_data = etdev->soc_data;
	int n = platform_irq_count(pdev);
	int core;
	static const char *wdt_interrupt_names[] = {"wdt_0", "wdt_1"};
	int wdt_irq_count_max = min_t(int, etdev->num_cores, ARRAY_SIZE(wdt_interrupt_names));
#if EDGETPU_USE_HW_WDT
	int ret;
#endif

	if (n < 0) {
		dev_err(etdev->dev, "Error retrieving IRQ count: %d\n", n);
		return n;
	}

	/* Parse wdt0, wdt1 IRQs first, all others are mbox. */
	soc_data->wdt_irq = devm_kcalloc(etdev->dev, etdev->num_cores,
					 sizeof(*soc_data->wdt_irq), GFP_KERNEL);
	if (!soc_data->wdt_irq)
		return -ENOMEM;

	for (core = 0; core < wdt_irq_count_max; core++) {
		soc_data->wdt_irq[core] =
			platform_get_irq_byname_optional(pdev, wdt_interrupt_names[core]);
		if (soc_data->wdt_irq[core] > 0) {
#if EDGETPU_USE_HW_WDT
			ret = devm_request_irq(etdev->dev, soc_data->wdt_irq[core],
					       edgetpu_soc_wdt_irq_handler, 0, etdev->dev_name,
					       etdev);
			if (ret) {
				dev_err(etdev->dev, "%s: failed to request WDT IRQ %d: %d\n",
					etdev->dev_name, soc_data->wdt_irq[core], ret);
				return ret;
			}
			/* Disable WDT IRQs (b/346649094). */
			disable_irq(soc_data->wdt_irq[core]);
#endif
		} else {
			soc_data->wdt_irq[core] = 0;
		}
	}

	return 0;
}
