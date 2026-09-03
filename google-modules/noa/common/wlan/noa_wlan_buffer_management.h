#ifndef __NOA_WLAN_BUFFER_MANAGEMENT_H__
#define __NOA_WLAN_BUFFER_MANAGEMENT_H__

#include <linux/types.h>

#include "common/wlan/noa_wlan.h"

enum noa_wlan_bm_locker_state {
	LOCKER_STATE_UNUSED = 0,
	LOCKER_STATE_IN_USE,
};

struct noa_wlan_bm_tkid_item {
	u16 tkid;
	u16 size;
	enum noa_wlan_bm_locker_state state;
	u64 dpa_va;
};

struct noa_wlan_bm {
	struct noa_wlan_bm_tkid_item *lockers;
	u32 size;
};

/**
 * @brief Initializes the buffer manager.
 *
 * @param[in] bm Pointer to the buffer manager structure to initialize.
 * @param[in] size The total number of buffer lockers to allocate.
 * @return 0 on success, or a negative error code on failure.
 */
extern int noa_wlan_bm_init(struct noa_wlan_bm *bm, u32 size);

/**
 * @brief Deinitializes the buffer manager and frees its resources.
 *
 * @param[in] bm Pointer to the buffer manager to deinitialize.
 */
extern void noa_wlan_bm_deinit(struct noa_wlan_bm *bm);

/**
 * @brief Finds a registered buffer item by its TKID.
 *
 * @param[in] bm Pointer to the buffer manager.
 * @param[in] tkid The Tracking ID of the buffer to find.
 * @param[out] tkid_item A pointer to a pointer that will be updated to point
 * to the found item.
 * @return 0 if the item is found, -ENOENT otherwise.
 */
extern int noa_wlan_bm_find(struct noa_wlan_bm *bm, u16 tkid,
			    const struct noa_wlan_bm_tkid_item **tkid_item);

/**
 * @brief Registers a new buffer with the buffer manager.
 *
 * @param[in] bm Pointer to the buffer manager.
 * @param[in] tkid The Tracking ID for the new buffer.
 * @param[in] buf_size The size of the buffer in bytes.
 * @param[in] dpa_va The DPA virtual address of the buffer.
 * @return 0 on success, or a negative error code on failure.
 */
extern int noa_wlan_bm_register(struct noa_wlan_bm *bm, u16 tkid, u16 buf_size, u64 dpa_va);

/**
 * @brief Removes a buffer registration from the manager.
 *
 * @param[in] bm Pointer to the buffer manager.
 * @param[in] tkid The Tracking ID of the buffer to remove.
 */
extern void noa_wlan_bm_remove(struct noa_wlan_bm *bm, u16 tkid);

#endif /* __NOA_WLAN_BUFFER_MANAGEMENT_H__ */
