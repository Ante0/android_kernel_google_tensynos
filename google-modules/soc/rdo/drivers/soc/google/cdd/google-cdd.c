// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC.
 *
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <soc/google/google-cdd.h>
#include <soc/google/google-smc.h>

#include "google-cdd-local.h"

#define CDD_VERSION	0x90000000

struct google_cdd_param cdd_param;
struct google_cdd_item cdd_items[] = {
	[CDD_ITEM_HEADER_ID]	= {CDD_ITEM_HEADER,	{0, 0, 0, true}, true, NULL, NULL},
	[CDD_ITEM_KEVENTS_ID]	= {CDD_ITEM_KEVENTS,	{0, 0, 0, false}, false, NULL, NULL},
};

struct google_cdd_base *cdd_base;
struct google_cdd_ctx cdd_ctx;

void __iomem *google_cdd_get_header_vaddr(void)
{
	if (google_cdd_get_item_enable(CDD_ITEM_HEADER))
		return (void __iomem *)(cdd_items[CDD_ITEM_HEADER_ID].entry.vaddr);
	return NULL;
}
EXPORT_SYMBOL_GPL(google_cdd_get_header_vaddr);

unsigned long google_cdd_get_header_paddr(void)
{
	if (google_cdd_get_item_enable(CDD_ITEM_HEADER))
		return (unsigned long)cdd_items[CDD_ITEM_HEADER_ID].entry.paddr;
	return 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_header_paddr);

static void google_cdd_set_sjtag_status(void)
{
#ifdef SMC_CMD_GET_SJTAG_STATUS
	cdd_ctx.sjtag_status = google_smc(SMC_CMD_GET_SJTAG_STATUS, 0x3, 0, 0);
	dev_info(cdd_ctx.dev, "SJTAG is %sabled\n",
		 cdd_ctx.sjtag_status ? "en" : "dis");
#endif
}

int google_cdd_get_sjtag_status(void)
{
	return cdd_ctx.sjtag_status;
}
EXPORT_SYMBOL_GPL(google_cdd_get_sjtag_status);

bool google_cdd_get_reboot_status(void)
{
	return cdd_ctx.in_reboot;
}
EXPORT_SYMBOL_GPL(google_cdd_get_reboot_status);

bool google_cdd_get_panic_status(void)
{
	return cdd_ctx.in_panic;
}
EXPORT_SYMBOL_GPL(google_cdd_get_panic_status);

bool google_cdd_get_warm_status(void)
{
	return cdd_ctx.in_warm;
}
EXPORT_SYMBOL_GPL(google_cdd_get_warm_status);

void google_cdd_set_powerkey_status(bool val)
{
	cdd_ctx.long_press_power = val;
}
EXPORT_SYMBOL_GPL(google_cdd_set_powerkey_status);

void google_cdd_set_debug_test_buffer_addr(u64 paddr, unsigned int cpu)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (header)
		__raw_writeq(paddr, header + CDD_OFFSET_DEBUG_TEST_BUFFER(cpu));
}
EXPORT_SYMBOL_GPL(google_cdd_set_debug_test_buffer_addr);

unsigned int google_cdd_get_debug_test_buffer_addr(unsigned int cpu)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	return header ? __raw_readq(header + CDD_OFFSET_DEBUG_TEST_BUFFER(cpu)) : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_debug_test_buffer_addr);

uint64_t google_cdd_get_last_pc(unsigned int cpu)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	return header ? __raw_readq(header + CDD_OFFSET_CORE_LAST_PC(cpu)) : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_last_pc);

uint32_t google_cdd_get_hardlockup_mask(void)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	return header ? __raw_readl(header + CDD_OFFSET_HARDLOCKUP_MASK) : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_hardlockup_mask);

uint32_t google_cdd_get_hardlockup_magic(int cpu)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	return header ? __raw_readl(header + CDD_OFFSET_CORE_HARDLOCKUP_MAGIC(cpu)) : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_hardlockup_magic);

unsigned int google_cdd_get_fiq_pending_core(void)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	return header ? __raw_readl(header + CDD_OFFSET_CORE_FIQ_PENDING) : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_fiq_pending_core);

int google_cdd_get_dpm_none_dump_mode(void)
{
	unsigned int val;
	void __iomem *header = google_cdd_get_header_vaddr();

	if (!header)
		return -1;

	val = __raw_readl(header + CDD_OFFSET_NONE_DPM_DUMP_MODE);
	if ((val & GENMASK(31, 16)) == CDD_SIGN_MAGIC)
		return (val & GENMASK(15, 0));

	return -1;
}
EXPORT_SYMBOL_GPL(google_cdd_get_dpm_none_dump_mode);

void google_cdd_set_dpm_none_dump_mode(unsigned int mode)
{
	void __iomem *header = google_cdd_get_header_vaddr();

	if (!header)
		return;

	if (mode)
		mode |= CDD_SIGN_MAGIC;
	else
		mode = 0;

	__raw_writel(mode, header + CDD_OFFSET_NONE_DPM_DUMP_MODE);
}
EXPORT_SYMBOL_GPL(google_cdd_set_dpm_none_dump_mode);

static struct google_cdd_item *google_cdd_get_item(const char *name)
{
	unsigned long i;

	for (i = 0; i < ARRAY_SIZE(cdd_items); i++) {
		if (cdd_items[i].name && !strncmp(name, cdd_items[i].name,
		    strlen(cdd_items[i].name)))
			return &cdd_items[i];
	}

	return NULL;
}

unsigned int google_cdd_get_item_size(const char *name)
{
	struct google_cdd_item *item = google_cdd_get_item(name);

	return item && item->entry.enabled ? item->entry.size : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_item_size);

unsigned long google_cdd_get_item_vaddr(const char *name)
{
	struct google_cdd_item *item = google_cdd_get_item(name);

	return item && item->entry.enabled ? item->entry.vaddr : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_item_vaddr);

unsigned int google_cdd_get_item_paddr(const char *name)
{
	struct google_cdd_item *item = google_cdd_get_item(name);

	return item && item->entry.enabled ? item->entry.paddr : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_item_paddr);

int google_cdd_get_item_enable(const char *name)
{
	struct google_cdd_item *item = google_cdd_get_item(name);

	return item && item->entry.enabled ? item->entry.enabled : 0;
}
EXPORT_SYMBOL_GPL(google_cdd_get_item_enable);

void google_cdd_set_item_enable(const char *name, int en)
{
	struct google_cdd_item *item = NULL;

	if (!name)
		return;

	/* This is default for debug-mode */
	item = google_cdd_get_item(name);
	if (item) {
		item->entry.enabled = en;
		pr_info("item - %s is %sabled\n", name, en ? "en" : "dis");
	}
}
EXPORT_SYMBOL_GPL(google_cdd_set_item_enable);

static void google_cdd_set_enable(int en)
{
	cdd_base->enabled = en;
	dev_info(cdd_ctx.dev, "%sabled\n", en ? "en" : "dis");
}

int google_cdd_get_enable(void)
{
	return cdd_base->enabled;
}
EXPORT_SYMBOL_GPL(google_cdd_get_enable);

struct google_cdd_item *google_cdd_get_item_by_index(int index)
{
	if (index < 0 || index > ARRAY_SIZE(cdd_items))
		return NULL;

	return &cdd_items[index];
}
EXPORT_SYMBOL_GPL(google_cdd_get_item_by_index);

int google_cdd_get_num_items(void)
{
	return ARRAY_SIZE(cdd_items);
}
EXPORT_SYMBOL_GPL(google_cdd_get_num_items);

void google_cdd_output(void)
{
	unsigned long i, size = 0;

	dev_info(cdd_ctx.dev, "google-cdd physical / virtual memory layout:\n");
	for (i = 0; i < ARRAY_SIZE(cdd_items); i++) {
		if (!cdd_items[i].entry.enabled)
			continue;
		pr_info("%-16s: phys:%pa / virt:%pK / size:0x%zx / en:%d\n",
				cdd_items[i].name,
				&cdd_items[i].entry.paddr,
				(void *) cdd_items[i].entry.vaddr,
				cdd_items[i].entry.size,
				cdd_items[i].entry.enabled);
		size += cdd_items[i].entry.size;
	}

	dev_info(cdd_ctx.dev, "total_item_size: %ldKB, google_cdd_log struct size: %zdKB\n",
		 size / SZ_1K, sizeof(struct google_cdd_log) / SZ_1K);
}
EXPORT_SYMBOL_GPL(google_cdd_output);

unsigned int google_cdd_get_max_core_num(void)
{
	if (nr_cpu_ids > CDD_NR_CPUS)
		pr_err("unexpected nr_cpu_ids(%u) or CDD_NR_CPUS (%u)\n", nr_cpu_ids, CDD_NR_CPUS);

	return min(nr_cpu_ids, (unsigned int)CDD_NR_CPUS);
}
EXPORT_SYMBOL_GPL(google_cdd_get_max_core_num);

static void google_cdd_init_ctx(struct device *dev)
{
	memset((void *)&cdd_ctx, 0, sizeof(struct google_cdd_ctx));
	raw_spin_lock_init(&cdd_ctx.ctrl_lock);
	cdd_ctx.dev = dev;
	u32 apc_wdt_sub_reason;

	google_cdd_set_sjtag_status();

	if (!of_property_read_u32(dev->of_node, "apc-wdt-sub-reason", &apc_wdt_sub_reason)) {
		cdd_ctx.apc_wdt_sub_reason =
			devm_ioremap(dev, apc_wdt_sub_reason, sizeof(apc_wdt_sub_reason));
		if (!cdd_ctx.apc_wdt_sub_reason)
			dev_err(dev, "unable to remap apc_wdt_sub_reason\n");
	} else {
		dev_err(dev, "%pOF missing apc-wdt-sub-reason property\n\n", dev->of_node);
	}

	if (of_property_read_u32(dev->of_node, "panic-action", &cdd_ctx.panic_action))
	    cdd_ctx.panic_action = GO_DEFAULT_ID;
}

static void google_cdd_fixmap(void)
{
	size_t vaddr, size;
	unsigned long i,j;

	for (i = 0; i < ARRAY_SIZE(cdd_items); i++) {
		if (!cdd_items[i].entry.enabled)
			continue;

		/*  assign cdd_item information */
		vaddr = cdd_items[i].entry.vaddr;
		size = cdd_items[i].entry.size;

		if (i == CDD_ITEM_HEADER_ID) {
			/*  initialize cdd_header to 0 except block 2 */
			for (j = 0; j < CDD_HDR_INFO_BLOCK_MAX_INDEX; j++) {
				if (j != CDD_HDR_INFO_BLOCK_KEEP_INDEX)
					memset((void *)(vaddr + j * CDD_HDR_INFO_BLOCK_SZ), 0,
						CDD_HDR_INFO_BLOCK_SZ);
			}
		} else {
			/*  initialized log to 0 if persist == false */
			if (!cdd_items[i].persist)
				memset((void *)vaddr, 0, size);
		}
	}

	if (cdd_items[CDD_ITEM_KEVENTS_ID].entry.enabled)
		cdd_log = (struct google_cdd_log *)(cdd_items[CDD_ITEM_KEVENTS_ID].entry.vaddr);

	cdd_param.cdd_log_misc = &cdd_log_misc;
	cdd_param.cdd_items = &cdd_items;
	cdd_param.cdd_log_items = &cdd_log_items;
	cdd_param.cdd_log = cdd_log;

	cdd_base->param = &cdd_param;

	/* output the information of google crash debug dump */
	google_cdd_output();
}

static int google_cdd_rmem_setup(struct device *dev)
{
	struct reserved_mem *rmem;
	struct device_node *rmem_np;
	struct google_cdd_item *item;
	bool en;
	unsigned long i, j;
	unsigned long flags = VM_MAP;
	pgprot_t prot = __pgprot(PROT_NORMAL_NC);
	int page_size, mem_count = 0;
	struct page *page;
	struct page **pages;
	void *vaddr;

	mem_count = of_count_phandle_with_args(dev->of_node, "memory-region", NULL);
	if (mem_count <= 0) {
		dev_err(dev, "no such memory-region\n");
		return -ENOMEM;
	}

	for (i = 0; i < mem_count; i++) {
		rmem_np = of_parse_phandle(dev->of_node, "memory-region", i);
		if (!rmem_np) {
			dev_err(dev, "no such memory-region of index %ld\n", i);
			continue;
		}

		en = of_device_is_available(rmem_np);
		if (!en) {
			dev_err(dev, "%s item is disabled, Skip alloc reserved memory\n",
					rmem_np->name);
			continue;
		}

		rmem = of_reserved_mem_lookup(rmem_np);
		if (!rmem) {
			dev_err(dev, "no such reserved mem of node name %s\n", rmem_np->name);
			continue;
		}

		google_cdd_set_item_enable(rmem->name, en);
		item = google_cdd_get_item(rmem->name);
		if (!item) {
			dev_err(dev, "no such %s item in cdd_items\n", rmem->name);
			continue;
		}

		if (!rmem->base || !rmem->size) {
			dev_err(dev, "%s item wrong base(%pap) or size(%pap)\n",
					item->name, &rmem->base, &rmem->size);
			item->entry.enabled = false;
			continue;
		}
		page_size = rmem->size / PAGE_SIZE;
		pages = kcalloc(page_size, sizeof(struct page *), GFP_KERNEL);
		page = phys_to_page(rmem->base);

		for (j = 0; j < page_size; j++)
			pages[j] = page++;

		vaddr = vmap(pages, page_size, flags, prot);
		kfree(pages);
		if (!vaddr) {
			dev_err(dev, "%s: paddr:%pap page_size:%pap failed to vmap\n",
					item->name, &rmem->base, &rmem->size);
			item->entry.enabled = false;
			continue;
		}

		item->entry.paddr = rmem->base;
		item->entry.size = rmem->size;
		item->entry.vaddr = (size_t)vaddr;
		item->head_ptr = (unsigned char *)vaddr;
		item->curr_ptr = (unsigned char *)vaddr;

		if (item == &cdd_items[CDD_ITEM_HEADER_ID]) {
			cdd_base = (struct google_cdd_base *)google_cdd_get_header_vaddr();
			cdd_base->vaddr = (size_t)vaddr;
			cdd_base->paddr = rmem->base;
			cdd_base->version = CDD_VERSION;
			rmem->priv = vaddr;
		}

		if (!cdd_base) {
			dev_err(dev, "Header memory region needs to be first!");
			/* This has to be the first and only mapped entry. */
			vunmap(vaddr);
			return -EINVAL;
		}

		cdd_base->size += rmem->size;
	}

	if (!cdd_base) {
		dev_err(dev, "No/Invalid header memory region found");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(cdd_items); i++) {
		item = &cdd_items[i];
		if (!item->entry.paddr || !item->entry.size)
			item->entry.enabled = false;
	}

	return 0;
}

static ssize_t in_warm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%sable\n", cdd_ctx.in_warm ? "en" : "dis");
}

static ssize_t in_warm_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);

	if (!ret)
		cdd_ctx.in_warm = !!val;

	return count;
}
DEVICE_ATTR_RW(in_warm);

static ssize_t system_dev_stat_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	uint32_t device, val;
	/* Check if the input has the correct format and extract the hex values using sscanf */
	if (sscanf(buf, "%x %x", &device, &val) != 2)
		return -EINVAL;

	if (device >= CDD_SYSTEM_DEVICE_DEV_MAX)
		return -ENODEV;

	google_cdd_set_system_dev_stat(device, val);
	return count;
}
DEVICE_ATTR_WO(system_dev_stat);

static struct attribute *cdd_sysfs_attrs[] = {
	&dev_attr_in_warm.attr,
	&dev_attr_system_dev_stat.attr,
	NULL,
};

static struct attribute_group cdd_sysfs_group = {
	.attrs = cdd_sysfs_attrs,
};

static const struct attribute_group *cdd_sysfs_groups[] = {
	&cdd_sysfs_group,
	NULL,
};

static int google_cdd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (google_cdd_dpm_scand_dt(dev))
		dev_warn(dev, "dpm dt scan failed\n");

	google_cdd_init_ctx(dev);

	if (google_cdd_rmem_setup(&pdev->dev)) {
		dev_err(dev, "%s failed\n", __func__);
		return -ENODEV;
	}

	google_cdd_fixmap();
	google_cdd_init_log();

	google_cdd_init_utils(dev);

	google_cdd_register_vh_log();

	google_cdd_set_enable(true);
	google_cdd_start_log(dev);

	if (sysfs_create_groups(&pdev->dev.kobj, cdd_sysfs_groups))
		dev_err(dev, "fail to register crash-debug-dump sysfs\n");

	dev_info(dev, "%s successful.\n", __func__);
	return 0;
}

static const struct of_device_id google_cdd_of_match[] = {
	{ .compatible	= "google,crash-debug-dump" },
	{},
};
MODULE_DEVICE_TABLE(of, google_cdd_of_match);

static struct platform_driver google_cdd_driver = {
	.probe = google_cdd_probe,
	.driver  = {
		.name  = "google-cdd",
		.of_match_table = of_match_ptr(google_cdd_of_match),
	},
};
module_platform_driver(google_cdd_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google Crash Debug Dump");
MODULE_LICENSE("GPL");
