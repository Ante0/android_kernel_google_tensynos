// SPDX-License-Identifier: GPL-2.0-or-later

#define KMSG_COMPONENT "zram_vh"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <trace/hooks/mm.h>

#include "zram_vh.h"

#define K(x) ((x) << (PAGE_SHIFT - 10))

/*
 * After completing I/O on a page, call this routine to update the page
 * flags appropriately
 */
static void zram_read_page_end_io(struct page *page)
{
	struct folio *folio = page_folio(page);

	folio_mark_uptodate(folio);
	folio_unlock(folio);
}

static void rvh_swap_read_folio_bdev_sync(void *data, struct block_device *bdev,
					  sector_t sector, struct page *page,
					  bool *read)
{
	u32 index;
	struct zram *zram;
	int ret;

	if (PageTransHuge(page))
		return;

	zram = bdev->bd_disk->private_data;
	index = sector >> SECTORS_PER_PAGE_SHIFT;

	ret = zram_read_page(zram, page, index, NULL);
	/* fallback to bio path for ZRAM_WB and error cases */
	if (ret)
		return;

	flush_dcache_page(page);
	zram_slot_lock(zram, index);
	zram_accessed(zram, index);
	zram_slot_unlock(zram, index);
	zram_read_page_end_io(page);
	*read = true;
}

static void vh_smaps_pte_entry(void *data, swp_entry_t swpent, int mapcount,
			       unsigned long *swap_shared,
			       unsigned long *writeback,
			       unsigned long *same,
			       unsigned long *huge)
{
	struct zram *zram = (struct zram *)data;
	struct swap_info_struct *sis;
	unsigned long index;
	u64 nr_pages = zram->disksize >> PAGE_SHIFT;

	/* prevent the swapoff race condition */
	sis = get_swap_device(swpent);
	if (unlikely(!sis))
		return;

	if (unlikely(!sis->bdev || !sis->bdev->bd_disk ||
		     sis->bdev->bd_disk->private_data != zram))
		goto unlock_swap_device;

	index = swp_offset(swpent);
	if (unlikely(index >= nr_pages))
		goto unlock_swap_device;

	if (mapcount >= 2)
		(*swap_shared)++;

	zram_slot_lock(zram, index);
	if (unlikely(!zram_allocated(zram, index)))
		goto unlock_zram_slot;

	if (zram_test_flag(zram, index, ZRAM_WB))
		(*writeback)++;
	else if (zram_test_flag(zram, index, ZRAM_SAME))
		(*same)++;
	else if (zram_test_flag(zram, index, ZRAM_HUGE))
		(*huge)++;

unlock_zram_slot:
	zram_slot_unlock(zram, index);
unlock_swap_device:
	put_swap_device(sis);
}

static void vh_show_smap(void *data, struct seq_file *m,
			 unsigned long swap_shared,
			 unsigned long writeback,
			 unsigned long same,
			 unsigned long huge)
{
	char str[128];
	char *p = str;

	p += sprintf(p, "SwapShared:     %8lu kB\n", K(swap_shared));
	p += sprintf(p, "Writeback:      %8lu kB\n", K(writeback));
	p += sprintf(p, "Same:           %8lu kB\n", K(same));
	p += sprintf(p, "Huge:           %8lu kB\n", K(huge));
	seq_puts(m, str);
}

int zram_vh_init(struct zram *zram)
{
	int ret = 0;

	ret = register_trace_android_rvh_swap_read_folio_bdev_sync(
		rvh_swap_read_folio_bdev_sync, NULL);
	if (ret) {
		pr_err("register swap_read_folio_bdev_sync failed\n");
		return ret;
	}

	ret = register_trace_android_vh_smaps_pte_entry(vh_smaps_pte_entry,
							zram);
	if (ret) {
		pr_err("register vh_smaps_pte_entry failed\n");
		return ret;
	}

	ret = register_trace_android_vh_show_smap(vh_show_smap, zram);
	if (ret)
		pr_err("register vh_show_smap failed\n");

	return ret;
}

int zram_vh_deinit(struct zram *zram)
{
	int ret = 0;

	ret = unregister_trace_android_vh_smaps_pte_entry(vh_smaps_pte_entry,
							  zram);
	if (ret)
		pr_err("unregister vh_smaps_pte_entry failed\n");

	ret = unregister_trace_android_vh_show_smap(vh_show_smap, zram);
	if (ret)
		pr_err("unregister vh_show_smap failed\n");

	return ret;
}

