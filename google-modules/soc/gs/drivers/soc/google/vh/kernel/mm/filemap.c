// SPDX-License-Identifier: GPL-2.0-only
/* filemap.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2025 Google LLC
 */

#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <uapi/linux/magic.h>

#include "pixel_mm.h"

/*
 * turn readahead off from page fault handler if free swap size
 * is less than free_swap_threshold_mb.
 */
static unsigned long free_swap_threshold_mb = 20;

static struct kobject pixel_filemap_kobj;
static bool async_readahead_adj_enabled;
static bool sync_readahead_adj_enabled;

#define PIXEL_FILEMAP_ATTR_RW(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RW(_name)

static ssize_t swap_free_threshold_mb_store(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf, size_t len)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	free_swap_threshold_mb = val;
	return len;
}

static ssize_t swap_free_threshold_mb_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	return sysfs_emit(buf, "%lu\n", free_swap_threshold_mb);
}
PIXEL_FILEMAP_ATTR_RW(swap_free_threshold_mb);

static ssize_t async_readahead_adj_enable_store(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t len)
{
	bool enable;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	async_readahead_adj_enabled = enable;
	return len;
}

static ssize_t async_readahead_adj_enable_show(struct kobject *kobj,
					       struct kobj_attribute *attr,
					       char *buf)
{
	return sysfs_emit(buf, "%d\n", async_readahead_adj_enabled);
}
PIXEL_FILEMAP_ATTR_RW(async_readahead_adj_enable);

static ssize_t sync_readahead_adj_enable_store(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t len)
{
	bool enable;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	sync_readahead_adj_enabled = enable;
	return len;
}

static ssize_t sync_readahead_adj_enable_show(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      char *buf)
{
	return sysfs_emit(buf, "%d\n", sync_readahead_adj_enabled);
}
PIXEL_FILEMAP_ATTR_RW(sync_readahead_adj_enable);

static struct attribute *pixel_filemap_attrs[] = {
	&swap_free_threshold_mb_attr.attr,
	&async_readahead_adj_enable_attr.attr,
	&sync_readahead_adj_enable_attr.attr,
	NULL,
};

static const struct attribute_group pixel_filemap_attr_group = {
	.attrs = pixel_filemap_attrs,
};

static const struct attribute_group *pixel_filemap_attr_groups[] = {
	&pixel_filemap_attr_group,
	NULL,
};

static void pixel_filemap_kobj_release(struct kobject *obj)
{
	/* Never released the static objects */
}

static const struct kobj_type pixel_filemap_ktype = {
	.release = pixel_filemap_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = pixel_filemap_attr_groups,
};

static inline void vh_mmap_readahead_adj(bool enabled, bool *skip)
{
	if (unlikely(!enabled))
		return;

	if ((get_nr_swap_pages() * PAGE_SIZE) >> 20 < free_swap_threshold_mb)
		*skip = true;
}

static inline void vh_async_mmap_readahead_adj(bool *skip)
{
	vh_mmap_readahead_adj(async_readahead_adj_enabled, skip);
}

static inline void vh_sync_mmap_readahead_adj(bool *skip)
{
	vh_mmap_readahead_adj(sync_readahead_adj_enabled, skip);
}

void vh_do_async_mmap_readahead(void *data, struct vm_fault *vmf,
				    struct folio *folio, bool *skip)
{
	vh_async_mmap_readahead_adj(skip);
}

void vh_do_sync_mmap_readahead(void *data, struct vm_fault *vmf,
				    bool *skip)
{
	vh_sync_mmap_readahead_adj(skip);
}

void vh_page_cache_readahead_start(void *data, struct file *file, pgoff_t pgoff,
				unsigned int size, bool sync)
{
	struct file_ra_state *ra = &file->f_ra;
	struct address_space *mapping = file->f_mapping;
	DEFINE_RA_MMAP_MISS(ra);

	if (file->f_mode & FMODE_RANDOM)
		return;
	if (!mapping_large_folio_support(mapping))
		return;
	/*
	 * FIXME: ra_mmap_miss->order should be replaced with ra->order
	 * in upstream kernel. c.f., https://r.android.com/3875281
	 */
	ra_mmap_miss->order = mapping_max_folio_order(mapping);
}

void vh_ra_alloc_retry(void *data, unsigned int *order, bool *retry)
{
	if (!*order)
		return;

	(*order)--;
	*retry = true;
}

void vh_page_cache_ra_order_bypass(void *data, struct readahead_control *ractl,
				   struct file_ra_state *ra,
				   int new_order, gfp_t *gfp, bool *bypass)
{
	struct address_space *mapping = ractl->mapping;

	if (!mapping_large_folio_support(mapping))
		return;

	/* High-order allocation during readahead should avoid reclaim. */
	if (new_order > 0)
		*gfp &= ~__GFP_DIRECT_RECLAIM;

	if (mapping->host->i_sb->s_magic == EROFS_SUPER_MAGIC_V1)
		*bypass = true;
}

int pixel_mm_filemap_sysfs(struct kobject *parent)
{
	int err = kobject_init_and_add(&pixel_filemap_kobj,
				       &pixel_filemap_ktype,
				       parent, "filemap");
	if (err)
		kobject_put(&pixel_filemap_kobj);

	return err;
}
