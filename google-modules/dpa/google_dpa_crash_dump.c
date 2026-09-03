// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/platform_data/sscoredump.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>

#include <soc/google/goog_gdmc_regdump_service.h>
#include <soc/google/google_dpa_crash_dump.h>

#include "google_dpa_crash_dump_internal.h"
#include "google_dpa_internal.h"
#include "google_dpa_io.h"

// TODO(b/381220732): This is a tentative structure to store data that are not
//                    tied with any memory addresses. SSCD makes ELF segments to
//                    store memory dumps and each segments has an associated
//                    memory address. Data like register dump does not have any
//                    associated memory address. DPA driver create a segment for
//                    it and call it a NOTE segment just like a NOTE section in
//                    coredump.
struct google_dpa_sscd_note_segment_data {
	struct gdmc_mba_cortex_m_register_dump ncp_regs;
	struct gdmc_mba_cortex_m_register_dump nep_regs;
} __packed;

#define GOOGLE_DPA_STATIC_SSCD_SEGMENTS 10 // (ITCM/DTCM/DRAM_CODE/DRAM_BUFFER) * 2 + SRAM + NOTE
#define GOOGLE_DPA_SSCD_NOTE_SEG_ADDR 0x0 // address to represent NOTE segment

static void init_crashdump_list(struct google_dpa_crash_dump *crash_dump)
{
	INIT_LIST_HEAD(&crash_dump->registered_ramdump_segments);
	mutex_init(&crash_dump->registered_ramdump_lock);
}

static void release_crashdump_list(struct google_dpa_crash_dump *crash_dump)
{
	struct google_dpa_mem_dump *crash_dump_data, *tmp;

	mutex_lock(&crash_dump->registered_ramdump_lock);
	list_for_each_entry_safe(crash_dump_data, tmp, &crash_dump->registered_ramdump_segments,
				 node) {
		list_del_init(&crash_dump_data->node);
	}
	mutex_unlock(&crash_dump->registered_ramdump_lock);
}

static void init_static_ramdump(struct google_dpa *dpa)
{
	for (int i = 0; i < ARRAY_SIZE(dpa->crash_dump.mem_dumps); i++)
		google_dpa_init_crash_dump_data(&dpa->crash_dump.mem_dumps[i], 0, NULL, 0, 0, 0);
}

static void release_static_ramdump(struct google_dpa *dpa)
{
	for (int i = 0; i < ARRAY_SIZE(dpa->crash_dump.mem_dumps); i++) {
		struct google_dpa_mem_dump *crash_dump_data = &dpa->crash_dump.mem_dumps[i];
		vfree(crash_dump_data->data);
		google_dpa_init_crash_dump_data(crash_dump_data, 0, NULL, 0, 0, 0);
	}
}

void google_dpa_init_crash_dump_data(struct google_dpa_mem_dump *crash_dump_data, u8 flag,
				     void *data, u64 mcu_view, u64 apc_view, size_t len)
{
	memset(crash_dump_data, 0, sizeof(*crash_dump_data));
	crash_dump_data->flags = flag;
	crash_dump_data->data = data;
	crash_dump_data->mcu_view_addr = mcu_view;
	crash_dump_data->apc_view_addr = apc_view;
	crash_dump_data->len = len;
	INIT_LIST_HEAD(&crash_dump_data->node);
}
EXPORT_SYMBOL_GPL(google_dpa_init_crash_dump_data);

int google_dpa_register_crash_dump_data(struct google_dpa *dpa,
					struct google_dpa_mem_dump *crash_dump_data)
{
	struct google_dpa_crash_dump *crash_dump = &dpa->crash_dump;

	if (!crash_dump_data->data || !crash_dump_data->len)
		return -EINVAL;

	WARN_ON(!list_empty(&crash_dump_data->node));

	mutex_lock(&crash_dump->registered_ramdump_lock);
	list_add_tail(&crash_dump_data->node, &crash_dump->registered_ramdump_segments);
	mutex_unlock(&crash_dump->registered_ramdump_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(google_dpa_register_crash_dump_data);

void google_dpa_unregister_crash_dump_data(struct google_dpa *dpa,
					   struct google_dpa_mem_dump *crash_dump_data)
{
	struct google_dpa_crash_dump *crash_dump = &dpa->crash_dump;
	mutex_lock(&crash_dump->registered_ramdump_lock);
	list_del_init(&crash_dump_data->node);
	mutex_unlock(&crash_dump->registered_ramdump_lock);
}
EXPORT_SYMBOL_GPL(google_dpa_unregister_crash_dump_data);

static struct google_dpa_addr_mapping *find_addr_mapping(const struct google_dpa_mcu *mcu,
							 const char *mapping_name)
{
	struct google_dpa_addr_mapping *addr_mapping;
	struct list_head *node;

	list_for_each(node, &mcu->addr_mappings) {
		addr_mapping = list_entry(node, struct google_dpa_addr_mapping, node);
		if (strcmp(addr_mapping->name, mapping_name) == 0)
			return addr_mapping;
	}
	return NULL;
}

static int collect_ramdump(struct google_dpa *dpa, const struct google_dpa_mcu *mcu,
			   struct google_dpa_mem_dump *crash_dump_data, const char *resource_name,
			   u8 flag)
{
	struct device *dev = dpa->dev;
	struct google_dpa_addr_mapping *addr_mapping;

	addr_mapping = find_addr_mapping(mcu, resource_name);
	if (!addr_mapping) {
		dev_err(dev, "Failed to find the source address of \"%s\" to collect ramdump.\n",
			resource_name);
		return -EINVAL;
	}

	crash_dump_data->flags = (flag & GOOGLE_DPA_SF_MASK);
	crash_dump_data->data = vmalloc(addr_mapping->len);
	if (!crash_dump_data->data) {
		dev_err(dev, "Failed to allocate crash dump buffer with len %zu\n",
			addr_mapping->len);
		return -ENOMEM;
	}
	crash_dump_data->len = addr_mapping->len;
	crash_dump_data->mcu_view_addr = addr_mapping->mcu_view_addr;
	// For DPA internal address mappings, apc_view_addr is not used.
	// It is used for dynamic DMA allocation.
	crash_dump_data->apc_view_addr = 0;

	google_dpa_memcpy_fromio(crash_dump_data->data, addr_mapping->ioremapped_addr,
				 addr_mapping->len);

	return 0;
}

static struct platform_device *make_sscd_pdev(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	struct platform_device *sscd_pdev;
	struct sscd_platform_data sscd_pdata = { 0 };
	int ret;

	sscd_pdev = platform_device_alloc(dev->of_node->full_name, PLATFORM_DEVID_NONE);
	if (!sscd_pdev)
		return ERR_PTR(-ENOMEM);

	/* Set the driver_override to get matched with the SSCD driver probe. */
	driver_set_override(&sscd_pdev->dev, &sscd_pdev->driver_override, SSCD_NAME,
			    strlen(SSCD_NAME));
	ret = platform_device_add_data(sscd_pdev, &sscd_pdata, sizeof(sscd_pdata));
	if (ret)
		goto sscd_put;

	return sscd_pdev;

sscd_put:
	platform_device_put(sscd_pdev);

	return ERR_PTR(ret);
}

int google_dpa_init_crashdump(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;

	dpa->sscd_pdev = make_sscd_pdev(dpa);
	if (IS_ERR(dpa->sscd_pdev)) {
		dev_err(dev, "Failed to create sscoredump device.\n");
		return PTR_ERR(dpa->sscd_pdev);
	}

	ret = platform_device_add(dpa->sscd_pdev);
	if (ret) {
		dev_err(dev, "Failed to add sscoredump device.\n");
		goto sscd_put;
	}

	init_crashdump_list(&dpa->crash_dump);
	return ret;

sscd_put:
	platform_device_put(dpa->sscd_pdev);

	return ret;
}

void google_dpa_deinit_crashdump(struct google_dpa *dpa)
{
	release_crashdump_list(&dpa->crash_dump);
	platform_device_unregister(dpa->sscd_pdev);
}

static int google_dpa_collect_ramdump_locked(struct google_dpa *dpa)
{
	struct device *dev = dpa->dev;
	int ret;
	int i;
	struct {
		const struct google_dpa_mcu *mcu;
		struct google_dpa_mem_dump *mem_dump;
		const char *resource_name;
		u8 flag;
	} ramdump_segments[] = {
		// The order is also how sections are put in the coredump.
		// DO NOT change the order, or it will break the legacy ramdump parser.
		[DPA_MEMDUMP_SRAM] = { &dpa->ncp, &dpa->crash_dump.mem_dumps[DPA_MEMDUMP_SRAM],
				       "ncp-sram", GOOGLE_DPA_SF_NCP_BIT | GOOGLE_DPA_SF_NEP_BIT },
		[DPA_MEMDUMP_NCP_ITCM] = { &dpa->ncp,
					   &dpa->crash_dump.mem_dumps[DPA_MEMDUMP_NCP_ITCM],
					   "ncp-itcm", GOOGLE_DPA_SF_NCP_BIT },
		[DPA_MEMDUMP_NCP_DTCM] = { &dpa->ncp,
					   &dpa->crash_dump.mem_dumps[DPA_MEMDUMP_NCP_DTCM],
					   "ncp-dtcm", GOOGLE_DPA_SF_NCP_BIT },
		[DPA_MEMDUMP_NEP_ITCM] = { &dpa->nep,
					   &dpa->crash_dump.mem_dumps[DPA_MEMDUMP_NEP_ITCM],
					   "nep-itcm", GOOGLE_DPA_SF_NEP_BIT },
		[DPA_MEMDUMP_NEP_DTCM] = { &dpa->nep,
					   &dpa->crash_dump.mem_dumps[DPA_MEMDUMP_NEP_DTCM],
					   "nep-dtcm", GOOGLE_DPA_SF_NEP_BIT },
		[DPA_MEMDUMP_NCP_DRAM_CODE] = { &dpa->ncp,
						&dpa->crash_dump
							 .mem_dumps[DPA_MEMDUMP_NCP_DRAM_CODE],
						"ncp-dram-code", GOOGLE_DPA_SF_NCP_BIT },
		[DPA_MEMDUMP_NEP_DRAM_CODE] = { &dpa->nep,
						&dpa->crash_dump
							 .mem_dumps[DPA_MEMDUMP_NEP_DRAM_CODE],
						"nep-dram-code", GOOGLE_DPA_SF_NEP_BIT },
		[DPA_MEMDUMP_NCP_DRAM_BUFFER] = { &dpa->ncp,
						  &dpa->crash_dump
							   .mem_dumps[DPA_MEMDUMP_NCP_DRAM_BUFFER],
						  "ncp-dram-buffer", GOOGLE_DPA_SF_NCP_BIT },
		[DPA_MEMDUMP_NEP_DRAM_BUFFER] = { &dpa->nep,
						  &dpa->crash_dump
							   .mem_dumps[DPA_MEMDUMP_NEP_DRAM_BUFFER],
						  "nep-dram-buffer", GOOGLE_DPA_SF_NEP_BIT },
	};

	dpa->crash_dump.mem_dump_valid = false;

	for (i = 0; i < ARRAY_SIZE(ramdump_segments); ++i) {
		dev_dbg(dev, "Collecting %s dump", ramdump_segments[i].resource_name);
		ret = collect_ramdump(dpa, ramdump_segments[i].mcu, ramdump_segments[i].mem_dump,
				      ramdump_segments[i].resource_name, ramdump_segments[i].flag);
		if (ret) {
			dev_err(dev, "Failed to get %s dump, ret %d.\n",
				ramdump_segments[i].resource_name, ret);
			return ret;
		}
	}

	dpa->crash_dump.mem_dump_valid = true;

	return 0;
}

static int google_dpa_copy_regdump_locked(struct google_dpa *dpa, void *regdump, int reg_dump_len)
{
	struct gdmc_mba_dpa_register_dump *dpa_regdump;
	int ret = 0;

	if (reg_dump_len != sizeof(*dpa_regdump))
		return -EINVAL;

	dpa_regdump = regdump;

	memcpy(&dpa->crash_dump.reg_dumps[DPA_REGDUMP_NCP], &dpa_regdump->ncp_regs,
	       sizeof(dpa_regdump->ncp_regs));
	memcpy(&dpa->crash_dump.reg_dumps[DPA_REGDUMP_NEP], &dpa_regdump->nep_regs,
	       sizeof(dpa_regdump->nep_regs));

	return ret;
}

static void set_sscd_mem_segment(struct sscd_segment *seg,
				 const struct google_dpa_mem_dump *mem_dump)
{
	seg->addr = mem_dump->data;
	seg->size = mem_dump->len;
	seg->paddr = (void *)mem_dump->mcu_view_addr;
	// As sscoredump does not support the program header flag.
	// The low 4 bits of an APC address is used as the DPA specific flag
	// for later ramdump processing.
	seg->vaddr = (void *)((mem_dump->apc_view_addr & ~(u64)GOOGLE_DPA_SF_MASK) |
			      (mem_dump->flags & GOOGLE_DPA_SF_MASK));
}

static void set_sscd_note_segment(struct sscd_segment *seg,
				  struct google_dpa_sscd_note_segment_data *note)
{
	seg->addr = note;
	seg->size = sizeof(*note);
	seg->paddr = (void *)GOOGLE_DPA_SSCD_NOTE_SEG_ADDR;
	// Mark this is a register note.
	seg->vaddr = (void *)((GOOGLE_DPA_SSCD_NOTE_SEG_ADDR & ~(GOOGLE_DPA_SF_MASK)) |
			      GOOGLE_DPA_SF_REG_BIT);
}

void google_dpa_collect_crashdump_locked(struct google_dpa *dpa, void *regdump, int reg_dump_len)
{
	struct device *dev = dpa->dev;
	struct platform_device *sscd_pdev = dpa->sscd_pdev;
	struct sscd_platform_data *sscd_pdata = sscd_pdev->dev.platform_data;
	struct sscd_segment *segments = NULL;
	struct google_dpa_sscd_note_segment_data note_segment;
	size_t nsegments = 0;
	int ret;
	size_t max_segments_count = GOOGLE_DPA_STATIC_SSCD_SEGMENTS;
	struct google_dpa_mem_dump *crash_dump_data;
	struct google_dpa_crash_dump *crash_dump = &dpa->crash_dump;

	if (!sscd_pdata->sscd_report)
		return;

	init_static_ramdump(dpa);

	mutex_lock(&crash_dump->registered_ramdump_lock);

	list_for_each_entry(crash_dump_data, &crash_dump->registered_ramdump_segments, node)
		max_segments_count++;

	segments = vmalloc(sizeof(struct sscd_segment) * max_segments_count);
	if (!segments) {
		dev_err(dev, "Failed to allocate sscd segments\n");
		mutex_unlock(&crash_dump->registered_ramdump_lock);
		return;
	}

	list_for_each_entry(crash_dump_data, &crash_dump->registered_ramdump_segments, node) {
		if (crash_dump_data->data)
			set_sscd_mem_segment(&segments[nsegments++], crash_dump_data);
	}
	mutex_unlock(&crash_dump->registered_ramdump_lock);

	ret = google_dpa_collect_ramdump_locked(dpa);
	if (ret)
		dev_err(dev, "Failed to collect RAM dump. Skip collecting RAM dump.\n");

	ret = google_dpa_copy_regdump_locked(dpa, regdump, reg_dump_len);
	if (ret)
		dev_err(dev, "Failed to collect register dump. Skip collecting register dump.\n");

	if (dpa->crash_dump.mem_dump_valid) {
		for (int i = 0; i < ARRAY_SIZE(dpa->crash_dump.mem_dumps); i++)
			set_sscd_mem_segment(&segments[nsegments++], &dpa->crash_dump.mem_dumps[i]);
	}

	memset(&note_segment, 0, sizeof(note_segment));
	memcpy(&note_segment.ncp_regs, &dpa->crash_dump.reg_dumps[DPA_REGDUMP_NCP],
	       sizeof(note_segment.ncp_regs));
	memcpy(&note_segment.nep_regs, &dpa->crash_dump.reg_dumps[DPA_REGDUMP_NEP],
	       sizeof(note_segment.nep_regs));
	set_sscd_note_segment(&segments[nsegments++], &note_segment);

	ret = sscd_pdata->sscd_report(sscd_pdev, segments, nsegments, SSCD_FLAGS_ELFARM32HDR,
				      "google_dpa_coredump");
	if (ret)
		dev_err(dev, "Failed to send crashdump to SSCD.\n");

	release_static_ramdump(dpa);
	vfree(segments);
}
