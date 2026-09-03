// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP generic event monitor interface.
 *
 * Copyright (C) 2024 Google LLC
 */

#include <gcip/gcip-memory.h>

#include "gxp-config.h"
#include "gxp-gem.h"
#include "mobile-soc-destiny.h"

#define GEM_CNTR_CONFIG_STRIDE 0x4
#define GEM_SNAPSHOT_CNTR_STRIDE 0x4

#define GEM_CONFIG_ARMED_MASK 0x1
#define GEM_COUNT_EN_MASK 0x1
#define GEM_EVENT_SHIFT 2

int gxp_gem_set_reg_resources(struct gxp_dev *gxp)
{
	struct platform_device *pdev = to_platform_device(gxp->dev);
	struct resource *r;
	void *vaddr;
	int ret = 0;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gem_mcu");
	if (!r) {
		dev_dbg(gxp->dev, "Failed to get GEM resource");
		return -ENODEV;
	}

	gxp->soc_data->gem_regs.phys_addr = r->start;
	gxp->soc_data->gem_regs.size = resource_size(r);

	vaddr = devm_ioremap_resource(gxp->dev, r);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		dev_dbg(gxp->dev, "Failed to map GEM registers (ret=%d)", ret);
		return ret;
	}

	gxp->soc_data->gem_regs.virt_addr = vaddr;

	return ret;
}

void gxp_gem_set_config(struct gxp_dev *gxp, bool armed)
{
	u32 value = armed ? GEM_CONFIG_ARMED_MASK : 0;

	if (!gxp->soc_data->gem_regs.virt_addr)
		return;

	writel(value, gxp->soc_data->gem_regs.virt_addr + GXP_REG_GEM_CONFIG_OFFSET);
}

void gxp_gem_set_counter_config(struct gxp_dev *gxp, int count_id, bool count_enable, u32 event)
{
	u32 count_en_mask = count_enable ? GEM_COUNT_EN_MASK : 0;
	u32 event_mask = event << GEM_EVENT_SHIFT;
	u32 value = count_en_mask | event_mask;

	if (!gxp->soc_data->gem_regs.virt_addr)
		return;

	writel(value, gxp->soc_data->gem_regs.virt_addr + GXP_REG_GEM_CNTR_CONFIG_OFFSET +
			      GEM_CNTR_CONFIG_STRIDE * count_id);
}

u32 gxp_gem_get_counter_snapshot(struct gxp_dev *gxp, int count_id)
{
	if (!gxp->soc_data->gem_regs.virt_addr)
		return 0;

	return readl(gxp->soc_data->gem_regs.virt_addr + GXP_REG_GEM_SNAPSHOT_CNTR_OFFSET +
		     GEM_SNAPSHOT_CNTR_STRIDE * count_id);
}
