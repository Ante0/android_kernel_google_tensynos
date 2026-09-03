#include "noa_wlan_buffer_management.h"

#include <linux/slab.h>

int noa_wlan_bm_init(struct noa_wlan_bm *bm, u32 size)
{
	u32 i = 0;

	if (bm->lockers) {
		return 0;
	}

	bm->lockers = kzalloc(size * sizeof(struct noa_wlan_bm_tkid_item), GFP_KERNEL);

	if (!bm->lockers) {
		bm->size = 0;
		return -ENOMEM;
	} else {
		bm->size = size;
	}

	for (i = 0; i < size; i++) {
		bm->lockers[i].state = LOCKER_STATE_UNUSED;
	}

	return 0;
}

void noa_wlan_bm_deinit(struct noa_wlan_bm *bm)
{
	if (bm->lockers) {
		kfree(bm->lockers);
		bm->lockers = NULL;
	}

	bm->size = 0;
}

static bool noa_wlan_bm_validate_tkid(struct noa_wlan_bm *bm, u16 tkid)
{
	return tkid < bm->size;
}

int noa_wlan_bm_find(struct noa_wlan_bm *bm, u16 tkid,
		     const struct noa_wlan_bm_tkid_item **tkid_item)
{
	struct noa_wlan_bm_tkid_item *item = NULL;

	if (noa_wlan_bm_validate_tkid(bm, tkid)) {
		item = &bm->lockers[tkid];
		if (item->state == LOCKER_STATE_IN_USE) {
			*tkid_item = item;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(noa_wlan_bm_find);

int noa_wlan_bm_register(struct noa_wlan_bm *bm, u16 tkid, u16 buf_size, u64 dpa_va)
{
	struct noa_wlan_bm_tkid_item *item = NULL;

	if (noa_wlan_bm_validate_tkid(bm, tkid)) {
		item = &bm->lockers[tkid];
		if (item->state == LOCKER_STATE_IN_USE) {
			pr_err("%s(): Dup tkid: %u\n", __func__, tkid);
			return -EINVAL;
		}

		item->state = LOCKER_STATE_IN_USE;
		item->tkid = tkid;
		item->size = buf_size;
		item->dpa_va = dpa_va;

		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(noa_wlan_bm_register);


void noa_wlan_bm_remove(struct noa_wlan_bm *bm, u16 tkid)
{
	struct noa_wlan_bm_tkid_item *item = NULL;

	if (noa_wlan_bm_validate_tkid(bm, tkid)) {
		item = &bm->lockers[tkid];
		item->state = LOCKER_STATE_UNUSED;
	}
}
EXPORT_SYMBOL_GPL(noa_wlan_bm_remove);
