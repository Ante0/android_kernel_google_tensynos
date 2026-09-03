// SPDX-License-Identifier: GPL-2.0-only
/*
 * SLC operations for Codec3P
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Jerry Huang <huangjerry@google.com>
 */


#include <linux/module.h>

#include "vpu_priv.h"
#include "vpu_coredump.h"
#include "wave6_regdefine.h"

#define VPU_CORE_DUMP_SIZE (2 * 1024 * 1024)

static void vpu_core_sscd_release(struct device *dev) {}

int vpu_sscd_dev_register(struct vpu_core *core)
{
	int res;

	struct platform_device *pdev = &core->sscd_pdev;

	pdev->name = VPU_CORE_NAME;
	pdev->id = PLATFORM_DEVID_NONE;
	pdev->driver_override = SSCD_NAME;
	pdev->dev.platform_data = &core->vpu_core_sscd_platdata;
	pdev->dev.release = vpu_core_sscd_release;

	res = platform_device_register(&core->sscd_pdev);

	return res;
}

void vpu_sscd_dev_unregister(struct vpu_core *core)
{
	platform_device_unregister(&core->sscd_pdev);
}

int vpu_do_sscoredump(struct vpu_core *core, const struct vpu_dump_info *dbg_info)
{
	struct sscd_platform_data *sscd_platdata;
	struct sscd_segment seg;
	int ret;

	dev_dbg(core->dev, "VPU core dump\n");
	sscd_platdata = dev_get_platdata(&core->sscd_pdev.dev);
	if (!sscd_platdata->sscd_report) {
		dev_dbg(core->dev, "No sscd_report\n");
		return -EINVAL;
	}

	if (!dbg_info) {
		dev_err(core->dev, "dbg_info is invalid\n");
		return -EINVAL;
	}

	memset(&seg, 0, sizeof(seg));
	seg.addr = dbg_info->addr;
	seg.size = dbg_info->size;
	if (dbg_info->crash_info[0] == '\0')
		snprintf(dbg_info->crash_info, VPU_CRASH_INFO_LEN, "VPU crash");

	ret = sscd_platdata->sscd_report(&core->sscd_pdev, &seg, 1,
		SSCD_FLAGS_ELFARM64HDR, dbg_info->crash_info);
	if (ret)
		dev_warn(core->dev, "sscd_report failed (ret=%d)\n", ret);

	return ret;
}

static void vpu_dump_fw_debug_buffer(struct vpu_core *core, char *coredump_buf, int size)
{
	uint32_t wr_ptr, wr_offset, offset;
	struct iosys_map vmap;

	if (!core->fw_debug_buf) {
		snprintf(coredump_buf, size, "\n-----------no VPU FW buffer-----------\n");
		return;
	}

	offset = snprintf(coredump_buf, size, "\n-----------dumping VPU FW logs-----------\n");

	if (size < core->fw_debug_buf->size + offset) {
		dev_warn(core->dev,
			"coredump buf %d is smaller than fw debug buf size %zu + offset %d\n",
			size, core->fw_debug_buf->size, offset);
			return;
	}

	wr_ptr = READ_VPU_REGISTER(core, CMD_COMMON_RET_MEM_DEBUG_WR_PTR);
	wr_offset = wr_ptr - core->fw_debug_buf->iova;
	if (wr_offset > core->fw_debug_buf->size) {
		dev_warn(core->dev, "wr_offset %d is larger than fw debug buf size %zu\n",
			wr_offset, core->fw_debug_buf->size);
			return;
	}

	if (dma_buf_vmap_unlocked(core->fw_debug_buf->dma_buf, &vmap)) {
		dev_warn(core->dev, "Failed to get kernel virtual address for fw debug buf\n");
		return;
	}

	memcpy(coredump_buf + offset, (char *)vmap.vaddr + wr_offset,
		core->fw_debug_buf->size - wr_offset);
	offset += (core->fw_debug_buf->size - wr_offset);
	memcpy(coredump_buf + offset, vmap.vaddr, wr_offset);
	dma_buf_vunmap_unlocked(core->fw_debug_buf->dma_buf, &vmap);
}

void vpu_internal_coredump(struct vpu_core *core, char *crash_info)
{
	char *coredump_buf = NULL;
	int size = 0;
	struct vpu_dump_info dbg_info;

	if (core->need_reload_fw) {
		dev_info(core->dev, "skip as coredump has already been triggered");
		return;
	}
	size = VPU_CORE_DUMP_SIZE;

	coredump_buf = kzalloc(size, GFP_KERNEL);
	if (coredump_buf)
		vpu_dump_fw_debug_buffer(core, coredump_buf, size);
	else {
		dev_warn(core->dev, "failed to allocate coredump_buf\n");
		return;
	}

	memset(&dbg_info, 0, sizeof(dbg_info));
	dbg_info.size = size;
	dbg_info.addr = coredump_buf;
	dbg_info.crash_info = crash_info;
	vpu_do_sscoredump(core, &dbg_info);

	kfree(coredump_buf);
}
