// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2026 Google LLC.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/overflow.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <drm/bridge/dw_mipi_dsi2h.h>
#include <drm/phy/dw_mipi_cdphy_sscd.h>
#include <gs_drm/gs_reg_dump.h>
#include <gs_drm/gs_sscd.h>

#include "dw-mipi-dsi2h.h"
#include "dw-mipi-dsi2h-sscd.h"

static ssize_t _get_dsi_sscd_regdump(struct dw_mipi_dsi2h *dsi2h, char *buffer, ssize_t count)
{
	struct drm_print_iterator iter;
	struct drm_printer p;
	char desc[5];

	iter.data = buffer;
	iter.start = 0;
	iter.remain = count;

	p = drm_coredump_printer(&iter);

	scnprintf(desc, sizeof(desc), "DSI%u", dsi2h->plat_data->mux_id);
	gs_reg_dump_with_skips(desc, dsi2h->base, 0, dsi2h->reg_size, &p, dsi2h->reg_table);

	return count - iter.remain;
}

static ssize_t get_dsi_sscd_regdump(struct dw_mipi_dsi2h *dsi2h, char **regdump_buf)
{
	ssize_t count;

	count = _get_dsi_sscd_regdump(dsi2h, NULL, INT_MAX);
	*regdump_buf = kvmalloc(count, GFP_KERNEL);
	if (!*regdump_buf)
		return -ENOMEM;

	_get_dsi_sscd_regdump(dsi2h, *regdump_buf, count);

	return count;
}

static ssize_t prepare_dsi_sscd_regdump(struct dw_mipi_dsi2h *dsi2h,
					struct display_sscd_section_config *out_cfg)
{
	char *regdump_buf;
	ssize_t regdump_buf_size;
	int ret;

	ret = pm_runtime_get_if_in_use(dsi2h->dev);
	if (ret <= 0) {
		if (ret < 0)
			dev_err(dsi2h->dev, "Failed to power ON for SSCD, ret %d\n", ret);
		return ret;
	}

	regdump_buf_size = get_dsi_sscd_regdump(dsi2h, &regdump_buf);

	pm_runtime_put_sync(dsi2h->dev);

	if (regdump_buf_size > 0) {
		display_sscd_configure_phys_hexdump_section(out_cfg, "dsi register dump",
							    (void *)regdump_buf, dsi2h->phys_addr,
							    regdump_buf_size);
	}

	return regdump_buf_size;
}

static struct dsi_sscd_payload_container *dsi_sscd_create_container(struct sscd_payload *dsi,
								    struct sscd_payload *phy)
{
	struct dsi_sscd_payload_container *ctx;
	int total = dsi->num_cfgs + phy->num_cfgs;
	int idx = 0;

	if (total == 0)
		return ERR_PTR(-EINVAL);

	ctx = kzalloc(struct_size(ctx, cfgs, total), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->phy_orig = *phy;
	ctx->num_dsi_cfgs = dsi->num_cfgs;

	if (dsi->num_cfgs > 0 && dsi->cfgs) {
		memcpy(&ctx->cfgs[idx], dsi->cfgs, size_mul(sizeof(ctx->cfgs[0]), dsi->num_cfgs));
		idx += dsi->num_cfgs;
	}
	if (phy->num_cfgs > 0 && phy->cfgs)
		memcpy(&ctx->cfgs[idx], phy->cfgs, size_mul(sizeof(ctx->cfgs[0]), phy->num_cfgs));

	return ctx;
}

int dsi_sscd_payload_prepare(struct device *dev, struct sscd_payload *payload)
{
	struct dw_mipi_dsi2h *dsi2h = dev_get_drvdata(dev);
	struct sscd_payload dsi = { 0 };
	struct sscd_payload phy = { 0 };
	struct display_sscd_section_config dsi_cfg = { 0 };
	struct dsi_sscd_payload_container *ctx;
	int ret;

	dev_dbg(dev, "dsi sscd payload prepare\n");

	ret = prepare_dsi_sscd_regdump(dsi2h, &dsi_cfg);
	if (ret > 0) {
		dsi.cfgs = &dsi_cfg;
		dsi.num_cfgs = 1;
	} else {
		dev_warn(dev, "Failed to prepare DSI regdump: %d\n", ret);
	}

	if (dsi2h->phy) {
		ret = dw_mipi_cdphy_sscd_prepare(dsi2h->phy, &phy);

		if (ret < 0)
			dev_warn(dev, "Failed to prepare PHY regdump: %d\n", ret);
	}

	if (dsi.num_cfgs == 0 && phy.num_cfgs == 0)
		return 0;

	ctx = dsi_sscd_create_container(&dsi, &phy);
	if (IS_ERR(ctx)) {
		if (dsi.num_cfgs)
			kvfree(dsi_cfg.virt_addr);
		if (dsi2h->phy)
			dw_mipi_cdphy_sscd_destroy(dsi2h->phy, &phy);
		return PTR_ERR(ctx);
	}

	payload->cfgs = ctx->cfgs;
	payload->num_cfgs = dsi.num_cfgs + phy.num_cfgs;
	return 0;
}

int dsi_sscd_payload_destroy(struct device *dev, struct sscd_payload *payload)
{
	struct dw_mipi_dsi2h *dsi2h = dev_get_drvdata(dev);
	struct dsi_sscd_payload_container *ctx;
	int i;

	dev_dbg(dev, "dsi sscd payload destroy\n");

	if (!payload || !payload->cfgs)
		return 0;

	ctx = container_of(payload->cfgs, struct dsi_sscd_payload_container, cfgs[0]);

	/* Unwind PHY resources using the exact payload it originated */
	if (dsi2h->phy)
		dw_mipi_cdphy_sscd_destroy(dsi2h->phy, &ctx->phy_orig);

	/* Unwind DSI internal resources */
	for (i = 0; i < ctx->num_dsi_cfgs; i++)
		kvfree(ctx->cfgs[i].virt_addr);

	kfree(ctx);
	payload->cfgs = NULL;
	payload->num_cfgs = 0;
	return 0;
}
