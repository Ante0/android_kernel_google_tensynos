#include "harness_buffer_management.h"

#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <soc/google/google_dpa.h>
#include <soc/google/google_dpa_ring_service_proxy.h>

#include "harness_doorbell.h"
#include "harness_memory_mapper.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "ring_service/ring_buffer_pool.h"
#include "ring_service/ring_mgmt/ring_manager.h"

int noa_harness_buf_mgr_init(struct noa_harness_buffer_manager *buf_mgr, struct device *dev,
			     struct google_dpa *dpa)
{
	int ret;

	if (!buf_mgr || !dev)
		return -EINVAL;

	buf_mgr->dev = dev;
	spin_lock_init(&buf_mgr->lock);
	bitmap_zero(buf_mgr->skb_bitmap, MAX_NOA_HARNESS_SKB_ENTRIES);
	memset(buf_mgr->skb_table, 0, sizeof(buf_mgr->skb_table));

	ret = noa_harness_memory_mapper_init(&buf_mgr->mapper, dev, dpa);
	if (ret) {
		dev_err(dev, "Failed to init mapper, err: %d\n", ret);
		return ret;
	}

	dev_info(dev, "Harness buffer manager initialized\n");
	return 0;
}

void noa_harness_buf_mgr_deinit(struct noa_harness_buffer_manager *buf_mgr)
{
	if (!buf_mgr)
		return;

	noa_harness_memory_mapper_deinit(&buf_mgr->mapper);
	dev_info(buf_mgr->dev, "Harness buffer manager deinitialized\n");
}

int noa_harness_buf_mgr_alloc_id(struct noa_harness_buffer_manager *buf_mgr,
				 const struct noa_harness_skb_table_entry *entry)
{
	int id;

	spin_lock_bh(&buf_mgr->lock);

	id = find_first_zero_bit(buf_mgr->skb_bitmap, MAX_NOA_HARNESS_GENERIC_TKID_SIZE);
	if (id >= MAX_NOA_HARNESS_GENERIC_TKID_SIZE) {
		spin_unlock_bh(&buf_mgr->lock);
		dev_err(buf_mgr->dev, "SKB table is full\n");
		return -ENOMEM;
	}

	__set_bit(id, buf_mgr->skb_bitmap);
	buf_mgr->skb_table[id] = *entry;

	spin_unlock_bh(&buf_mgr->lock);

	return id;
}

void noa_harness_buf_mgr_free_id(struct noa_harness_buffer_manager *buf_mgr, int id)
{
	bool should_unmap = false;
	struct noa_harness_mapping_params params;

	if (id < 0 || id >= MAX_NOA_HARNESS_SKB_ENTRIES) {
		dev_err(buf_mgr->dev, "Invalid SKB ID to free: %d\n", id);
		return;
	}

	spin_lock_bh(&buf_mgr->lock);

	if (!test_bit(id, buf_mgr->skb_bitmap)) {
		spin_unlock_bh(&buf_mgr->lock);
		dev_warn(buf_mgr->dev, "Attempted to free an already free SKB ID: %d\n", id);
		return;
	}

	if (buf_mgr->skb_table[id].is_map_to_dpa) {
		struct sk_buff *skb = (struct sk_buff *)buf_mgr->skb_table[id].skb_address;

		if (unlikely(!skb)) {
			dev_err(buf_mgr->dev, "ID %d is mapped but SKB is NULL\n", id);
		} else {
			should_unmap = true;
			params.cpu_addr = (void *)skb->head;
			params.size = buf_mgr->skb_table[id].map_dpa_len;
		}
	}

	memset(&buf_mgr->skb_table[id], 0, sizeof(buf_mgr->skb_table[id]));
	__clear_bit(id, buf_mgr->skb_bitmap);

	spin_unlock_bh(&buf_mgr->lock);

	if (should_unmap)
		noa_harness_memory_mapper_unmap(&buf_mgr->mapper, &params);
}

struct sk_buff *noa_harness_buf_mgr_get_skb(struct noa_harness_buffer_manager *buf_mgr, int id)
{
	struct sk_buff *skb = NULL;

	if (id < 0 || id >= MAX_NOA_HARNESS_SKB_ENTRIES) {
		dev_err(buf_mgr->dev, "Invalid SKB ID to get: %d\n", id);
		return NULL;
	}

	spin_lock_bh(&buf_mgr->lock);

	if (test_bit(id, buf_mgr->skb_bitmap)) {
		skb = (struct sk_buff *)buf_mgr->skb_table[id].skb_address;
	}

	spin_unlock_bh(&buf_mgr->lock);

	return skb;
}

static int replenish_nep_buffer(struct noa_harness *tm, struct harness_nep_buffer_pool *pool,
				u32 tkid, void *cpu_addr, u64 dpa_va)
{
	u32 index;
	struct noa_ring_wrapper *ring = &pool->ring;
	noa_buffer_pool_desc *item;
	u32 dp_low;
	u32 dp_high;

	index = tkid - pool->tkid_offset;
	if (index >= pool->size) {
		dev_err(tm->dev, "Invalid tkid %u on the buffer pool", tkid);
		return -EINVAL;
	}

	if (__noa_ring_is_full(ring->basic.head, noa_ring_tail_read_once(ring), ring->basic.size)) {
		dev_err(tm->dev, "Failed to replenish buffer due to ring %s is full\n", ring->name);
		WARN_ON(1);
		return -EAGAIN;
	}

	noa_harness_cpu_addr_to_dp(cpu_addr, &dp_low, &dp_high);

	item = (noa_buffer_pool_desc *)noa_ring_buf_pos(ring->basic.base, ring->basic.head,
							ring->basic.item_len);
	item->tkid = tkid;
	item->dp_high = dp_high;
	item->dp_low = dp_low;
	item->dv = dpa_va;
	ring->basic.head = noa_ring_move_pos(ring->basic.head, 1, ring->basic.size);
	noa_ring_head_write_once(ring, ring->basic.head);

	return 0;
}

#define NEP_BUFFER_SIZE ((2048U))

static int fill_nep_buffer_pool(struct noa_harness *tm, struct harness_nep_buffer_pool *pool,
				u32 count)
{
	int ret;
	u32 i;
	struct noa_harness_buffer_manager *buf_mgr = &tm->buf_mgr;

	for (i = 0; i < count; i++) {
		u32 id;
		struct sk_buff *skb;
		struct noa_harness_mapping_params params;
		u64 dpa_va;
		int bit;
		spin_lock_bh(&buf_mgr->lock);
		bit = find_next_zero_bit(buf_mgr->skb_bitmap, pool->tkid_offset + pool->size,
					 pool->tkid_offset);
		if (bit >= pool->tkid_offset + pool->size) {
			spin_unlock_bh(&buf_mgr->lock);
			ret = 0;
			goto out;
		}
		spin_unlock_bh(&buf_mgr->lock);

		skb = alloc_skb(NEP_BUFFER_SIZE, GFP_KERNEL);
		if (!skb) {
			ret = -ENOMEM;
			goto out;
		}

		params.cpu_addr = skb->head;
		params.size = NEP_BUFFER_SIZE;
		ret = noa_harness_memory_mapper_remap(&buf_mgr->mapper, &params, &dpa_va);
		if (ret) {
			dev_err(tm->dev, "Failed to remap address 0x%lx, err: %d\n",
				(unsigned long)params.cpu_addr, ret);
			dev_kfree_skb_any(skb);
			goto out;
		}

		spin_lock_bh(&buf_mgr->lock);
		bit = find_next_zero_bit(buf_mgr->skb_bitmap, pool->tkid_offset + pool->size,
					 pool->tkid_offset);
		if (bit >= pool->tkid_offset + pool->size) {
			spin_unlock_bh(&buf_mgr->lock);
			noa_harness_memory_mapper_unmap(&buf_mgr->mapper, &params);
			dev_err(tm->dev, "NEP buffer pool is full\n");
			goto out;
		}
		id = bit;
		buf_mgr->skb_table[id].is_map_to_dpa = true;
		buf_mgr->skb_table[id].map_dpa_len = params.size;
		buf_mgr->skb_table[id].skb_address = skb;
		__set_bit(bit, buf_mgr->skb_bitmap);
		spin_unlock_bh(&buf_mgr->lock);

		ret = replenish_nep_buffer(tm, pool, id, params.cpu_addr, dpa_va);
		if (ret) {
			noa_harness_buf_mgr_free_id(buf_mgr, id);
			dev_kfree_skb_any(skb);
			goto out;
		}
	}

	ret = 0;
out:
	return ret;
}

static int init_nep_buffer_pool(struct noa_harness *tm, struct harness_nep_buffer_pool *pool)
{
	if (!pool->size)
		return -EINVAL;

	// Since the ring buffer can only store size - 1 buffers, we only initialize
	// size - 1 buffers at the beginning.
	return fill_nep_buffer_pool(tm, pool, pool->size - 1);
}

static void free_nep_buffer_pool(struct noa_harness *tm, struct harness_nep_buffer_pool *pool)
{
	u32 i;
	struct noa_harness_buffer_manager *buf_mgr = &tm->buf_mgr;

	for (i = 0; i < pool->size; i++) {
		u32 id = i + pool->tkid_offset;
		struct sk_buff *skb;

		if (!test_bit(id, buf_mgr->skb_bitmap)) {
			continue;
		}

		skb = noa_harness_buf_mgr_get_skb(&tm->buf_mgr, id);
		noa_harness_buf_mgr_free_id(&tm->buf_mgr, id);
		if (skb) {
			dev_kfree_skb_any(skb);
		}
	}
}

const static struct noa_ring_ops pool_ring_ops;
#define NOA_BUFFER_POOL_ITEM_LENGTH ((sizeof(noa_buffer_pool_desc)))

int noa_harness_register_nep_buffer_pool(struct noa_harness *tm, struct noa_harness_vdev *vdev,
					 u8 pool_id, const char *pool_name, u32 tkid_offset,
					 u32 pool_size)
{
	int ret;
	u8 interface;
	u8 flow;
	u8 category;
	struct harness_nep_buffer_pool *pool = &vdev->nep_buffer_pool;
	struct device *dpa_dev = google_dpa_get_dpa_dev(tm->dpa);
	struct noa_ring_regs ring_regs = { 0 };
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.size = pool_size,
		.item_len = NOA_BUFFER_POOL_ITEM_LENGTH,
	};

	pool->pool_id = pool_id;
	pool->size = pool_size;
	pool->tkid_offset = tkid_offset;
	pool->doorbell = vdev->doorbell;

	if (!dpa_dev) {
		dev_err(tm->dev, "Failed to get DPA device for nep buffer pool %s\n", pool_name);
		return -ENODEV;
	}

	spin_lock_init(&pool->lock);

	NoaRingPathIdParse(pool_id, &interface, &flow, &category);
	ret = NoaRingSharedRegsGet(&ring_regs, interface, flow, category, kNoaRingNepInput);
	if (ret) {
		dev_err(tm->dev, "Failed to get buffer pool %s regs, ret %d\n", pool_name, ret);
		return ret;
	}

	pool->ring_buffer = dmam_alloc_coherent(dpa_dev, info.size * NOA_BUFFER_POOL_ITEM_LENGTH,
						&pool->ring_buffer_dma, GFP_KERNEL);
	if (!pool->ring_buffer) {
		dev_err(tm->dev, "Failed to allocate ring buffer for %s\n", pool_name);
		return -ENOMEM;
	}
	info.base = pool->ring_buffer;
	info.dpa_base = (char *)pool->ring_buffer_dma;

	ret = noa_ring_regs_wrapper_init(&pool->ring, NOA_RING_TYPE_PRODUCER, &pool_ring_ops,
					 &ring_regs, vdev, pool_name, 0);
	if (ret) {
		dev_err(tm->dev, "Failed to init %s buffer pool ring, ret %d\n", pool_name, ret);
		goto free_ring;
	}

	noa_ring_info_setup(&pool->ring, &info);
	noa_ring_activate(&pool->ring);

	ret = init_nep_buffer_pool(tm, pool);
	if (ret) {
		dev_err(tm->dev, "Failed to fill %s nep buffer pool, ret %d\n", pool_name, ret);
		goto free_buffer_pool;
	}

	ret = google_dpa_ring_service_rpc_event_activate(pool_id, kNoaRingNepInput);
	if (ret) {
		dev_err(tm->dev, "Failed to activate nep buffer pool %s [id: %u]\n", pool_name,
			pool_id);
		goto free_buffer_pool;
	}

	noa_harness_trigger_doorbell(pool->doorbell);
	return 0;

free_buffer_pool:
	noa_ring_deactivate(&pool->ring);
	free_nep_buffer_pool(tm, pool);

free_ring:
	dmam_free_coherent(dpa_dev, info.size * NOA_BUFFER_POOL_ITEM_LENGTH, pool->ring_buffer,
			   pool->ring_buffer_dma);
	return ret;
}

void noa_harness_unregister_nep_buffer_pool(struct noa_harness *tm, struct noa_harness_vdev *vdev)
{
	struct harness_nep_buffer_pool *pool = &vdev->nep_buffer_pool;
	struct device *dpa_dev = google_dpa_get_dpa_dev(tm->dpa);

	google_dpa_ring_service_rpc_event_deactivate(pool->pool_id, kNoaRingNepInput);
	noa_ring_deactivate(&pool->ring);
	free_nep_buffer_pool(tm, pool);
	if (pool->ring_buffer)
		dmam_free_coherent(dpa_dev, pool->size * NOA_BUFFER_POOL_ITEM_LENGTH,
				   pool->ring_buffer, pool->ring_buffer_dma);
	pool->ring_buffer = NULL;
	pool->size = 0;
	pool->tkid_offset = 0;
}

void noa_harness_refill_nep_buffer_task(struct work_struct *work)
{
	int ret;
	struct noa_harness_vdev *vdev = container_of(work, struct noa_harness_vdev, refill_work);
	struct noa_harness *tm = vdev->tm;
	struct harness_nep_buffer_pool *pool = &vdev->nep_buffer_pool;
	struct noa_ring_wrapper *ring = &pool->ring;
	u32 refill_count;

	spin_lock_bh(&pool->lock);
	refill_count = noa_ring_free_items_count(ring->basic.head, noa_ring_tail_read_once(ring),
						 ring->basic.size);
	spin_unlock_bh(&pool->lock);

	if (!refill_count)
		return;

	ret = fill_nep_buffer_pool(tm, pool, refill_count);
	if (ret) {
		dev_err(tm->dev, "Failed to refill nep buffer pool %s, ret %d\n", ring->name, ret);
	}

	noa_harness_trigger_doorbell(pool->doorbell);
}
