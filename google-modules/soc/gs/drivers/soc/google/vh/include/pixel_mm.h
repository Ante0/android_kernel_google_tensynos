/* SPDX-License-Identifier: GPL-2.0 */

#ifndef PIXEL_MM_H
#define PIXEL_MM_H

int pixel_mm_filemap_sysfs(struct kobject *parent);

void vh_do_async_mmap_readahead(void *data, struct vm_fault *vmf,
				struct folio *folio, bool *skip);
void vh_do_sync_mmap_readahead(void *data, struct vm_fault *vmf,
				bool *skip);
void vh_page_cache_readahead_start(void *data, struct file *file, pgoff_t pgoff,
				   unsigned int size, bool sync);
void vh_page_cache_ra_order_bypass(void *data, struct readahead_control *ractl,
				   struct file_ra_state *ra,
				   int new_order, gfp_t *gfp, bool *bypass);
void vh_ra_alloc_retry(void *data, unsigned int *order, bool *retry);
#endif	/* PIXEL_MM_H */
