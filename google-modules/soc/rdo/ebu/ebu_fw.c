// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */
#include <linux/firmware.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include "ebu_fw.h"
#include <soc/google/goog-mba-ebu-iface.h>
#include <soc/google/goog_ebu_service_ids.h>

static int google_ebu_load_auth_image(struct google_ebu *gebu,
				      const struct firmware *fw)
{
	u32 *fw_data = (u32 *)fw->data;
	u32 *fw_start = (u32 *)fw->data;
	u32 fw_size = fw->size;
	u32 *fw_end = fw_data + (fw_size / sizeof(u32));

	for (; fw_data < fw_end; fw_data++) {
		writel(*fw_data,
		       gebu->m0p_sram_base + (fw_data - fw_start) * 4);
	}
	return 0;
}

int load_and_release_firmware(struct google_ebu *gebu, const char *firmware_name)
{
	struct device *dev = gebu->dev;
	const struct firmware *fw;
	int ret;

	request_firmware(&fw, firmware_name, dev);
	if (!fw) {
		dev_err(dev, "Failed to request firmware %s\n", firmware_name);
		return -ENOENT;
	}

	ret = clk_prepare_enable(gebu->ebu_core_clk);
	if (ret) {
		dev_err(gebu->dev, "ebu core clock on failed:%d\n",
			ret);
		goto release_fw;
	}

	ret = reset_control_deassert(gebu->ebu_core_rst);
	if (ret) {
		dev_err(gebu->dev,
			"ebu core reset deassert failed:%d\n", ret);
		goto clk_disable;
	}

	ret = google_ebu_load_auth_image(gebu, fw);
	if (ret) {
		dev_err(dev, "Failed to load firmware %s\n", firmware_name);
		goto reset_assert;
	}

	writel(0, gebu->secure_csr_base);
	// Wait for firmware to be loaded
	// TODO(mnkumar): Remove this once we can add retries to ebu_ping with low
	// timeout
	mdelay(10);
	ret = ebu_ping(gebu->mba_client);
	if (ret) {
		dev_err(gebu->dev, "Failed to ping ebu mailbox\n");
		goto reset_assert;
	}
	release_firmware(fw);
	gebu->ebu_fw_loaded = true;
	return 0;

reset_assert:
	reset_control_assert(gebu->ebu_core_rst);
clk_disable:
	clk_disable_unprepare(gebu->ebu_core_clk);
release_fw:
	release_firmware(fw);
	return ret;
}

void stop_firmware(struct google_ebu *gebu)
{
	if (!gebu->ebu_fw_loaded)
		return;

	reset_control_assert(gebu->ebu_core_rst);
	clk_disable_unprepare(gebu->ebu_core_clk);
	writel(1, gebu->secure_csr_base);
	gebu->ebu_fw_loaded = false;
}
