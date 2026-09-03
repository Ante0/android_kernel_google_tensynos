#ifndef __HARNESS_BUFFER_MANAGEMENT_H__
#define __HARNESS_BUFFER_MANAGEMENT_H__

#include "noa_test_harness.h"

/**
 * noa_harness_buf_mgr_init() - Initializes the harness buffer manager.
 * @buf_mgr: Pointer to the buffer manager structure.
 * @dev: Pointer to the device structure.
 * @dpa: Pointer to the Google DPA structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_harness_buf_mgr_init(struct noa_harness_buffer_manager *buf_mgr, struct device *dev,
			     struct google_dpa *dpa);

/**
 * noa_harness_buf_mgr_deinit() - Deinitializes the harness buffer manager.
 * @buf_mgr: Pointer to the buffer manager structure.
 */
void noa_harness_buf_mgr_deinit(struct noa_harness_buffer_manager *buf_mgr);

/**
 * noa_harness_buf_mgr_alloc_id() - Allocates a unique ID for a given SKB entry.
 * @buf_mgr: Pointer to the buffer manager structure.
 * @entry: Pointer to the SKB table entry to store.
 *
 * Notice: This function only allocates from the generic pool; NEP pools manage
 * their own TKID ranges directly.
 *
 * Return: The allocated ID on success, or a negative error code on failure.
 */
int noa_harness_buf_mgr_alloc_id(struct noa_harness_buffer_manager *buf_mgr,
				 const struct noa_harness_skb_table_entry *entry);

/**
 * noa_harness_buf_mgr_free_id() - Frees a previously allocated SKB ID.
 * @buf_mgr: Pointer to the buffer manager structure.
 * @id: The ID to be freed.
 */
void noa_harness_buf_mgr_free_id(struct noa_harness_buffer_manager *buf_mgr, int id);

/**
 * noa_harness_buf_mgr_get_skb() - Retrieves the SKB associated with a given ID.
 * @buf_mgr: Pointer to the buffer manager structure.
 * @id: The ID of the SKB to retrieve.
 *
 * Return: A pointer to the SKB on success, or NULL if the ID is not valid.
 */
struct sk_buff *noa_harness_buf_mgr_get_skb(struct noa_harness_buffer_manager *buf_mgr, int id);

int noa_harness_register_nep_buffer_pool(struct noa_harness *tm, struct noa_harness_vdev *vdev,
					 u8 pool_id, const char *pool_name, u32 tkid_offset,
					 u32 pool_size);
void noa_harness_unregister_nep_buffer_pool(struct noa_harness *tm, struct noa_harness_vdev *vdev);
void noa_harness_refill_nep_buffer_task(struct work_struct *work);

static inline void noa_harness_cpu_addr_to_dp(void *cpu_addr, u32 *dp_low, u32 *dp_high)
{
	u64 addr = (u64)cpu_addr;

	*dp_low = lower_32_bits(addr);
	*dp_high = upper_32_bits(addr);
}

static inline u64 noa_harness_dp_to_cpu_addr(u32 dp_low, u32 dp_high)
{
	return ((u64)dp_high << 32) | dp_low;
}

#endif /* __HARNESS_BUFFER_MANAGEMENT_H__ */
