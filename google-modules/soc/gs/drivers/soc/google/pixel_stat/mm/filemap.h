/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_PIXEL_FILEMAP_H__
#define __MM_PIXEL_FILEMAP_H__

void vh_filemap_get_folio_mod(void *data, struct address_space *mapping, pgoff_t index,
			      int fgp_flags, gfp_t gfp_mask, struct folio *folio);
void rvh_mapping_shrinkable(void *data, bool *shrinkable);

#endif
