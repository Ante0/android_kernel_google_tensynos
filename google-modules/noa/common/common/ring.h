/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic API for NOA ring operations
 *
 * noa_ring_wrapper_init(), Will initialize the ring structure and determine
 * its type and the behavior of operations.
 *
 * noa_ring_info_setup(), Set the shared information into the memory address
 * regs in the ring structure. This function should be called before the ring
 * is activated.
 *
 * noa_ring_activate/deactivate(), Activate / Deactivate the ring.
 *
 * noa_ring_begin_processing(), Mark the ring as in processing. If the ring
 * is already in processing, it will return zero; otherwise, return 1 for success.
 *
 * noa_ring_complete_processing(), Make the ring leave the in-processing state
 * and flush its temporary `tail` or `head position` into the shared
 * memory address `regs`.
 *
 * noa_ring_read/write, Read / write data into the ring.
 *
 * Below describes the life cycle of Ring operations.
 *
 *
 * noa_ring_wrapper_init()
 *     |
 *     +-------------+
 *     |             |
 *     |             v
 *     |         noa_ring_info_setup()
 *     v                 |
 * noa_ring_activate() <-+
 *     |
 *     |
 *     v
 * noa_ring_begin_processiong() <-----+
 *     |                              |
 *     |                              |
 *     v                              |
 * noa_ring_read() / noa_ring_write() |
 *     |                              |
 *     |                              |
 *     v                              |
 * noa_ring_complete_processing()     |
 *     |                              |
 *     |                              |
 *     v                              |
 * noa_ring_deactivate() -------------+
 *
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
// NOLINTBEGIN
#ifndef __NOA_RING_H__
#define __NOA_RING_H__

#ifdef linux
#include <common/noa_hw_ring.h>
#include <linux/kernel.h>
#include <linux/types.h>
#else
#include <cstddef>
#include <algorithm>
#include <cerrno>
#include <sys/types.h>

#include "linux_port/atomic.h"
#include "linux_port/types.h"

#include "common/noa_hw_ring.h"
#endif

struct noa_iovec {
	void *base;
	size_t len;
};

struct noa_ring_wrapper;

struct noa_ring_ops {
	ssize_t (*payload_len)(const void *data);
	ssize_t (*read_payload)(const void *data, size_t data_len, struct noa_iovec *iov);
	int (*fill_noop)(void *buf, size_t buf_len);
	ssize_t (*write_payload)(void *buf, size_t buf_len, const void *data, size_t data_len);
	void (*begin_hook)(struct noa_ring_wrapper *ring);
	void (*complete_prehook)(struct noa_ring_wrapper *ring);
	void (*complete_hook)(struct noa_ring_wrapper *ring);
};

struct noa_ring_data_ops {
	u32 (*load_head)(const struct noa_ring_wrapper *ring);
	void (*store_head)(struct noa_ring_wrapper *ring, u32 val);
	u32 (*load_tail)(const struct noa_ring_wrapper *ring);
	void (*store_tail)(struct noa_ring_wrapper *ring, u32 val);
	u64 (*load_base)(const struct noa_ring_wrapper *ring);
	void (*store_base)(struct noa_ring_wrapper *ring, u64 val);
	u32 (*load_item_len)(const struct noa_ring_wrapper *ring);
	void (*store_item_len)(struct noa_ring_wrapper *ring, u32 val);
	u32 (*load_size)(const struct noa_ring_wrapper *ring);
	void (*store_size)(struct noa_ring_wrapper *ring, u32 val);
	u64 (*load_dpa_base)(const struct noa_ring_wrapper *ring);
	void (*store_dpa_base)(struct noa_ring_wrapper *ring, u64 val);
};

enum {
	NOA_RING_FLAG_TYPE_BITS = 2,
	NOA_RING_FLAG_ACTIVE,
	NOA_RING_FLAG_PROCESSING,
	NOA_RING_FLAG_POS_MOVED,
};

enum {
	NOA_RING_TYPE_CONSUMER,
	NOA_RING_TYPE_PRODUCER,
	NOA_RING_TYPE_MAX,
};

#define NOA_RING_TYPE_MASK ((1U << NOA_RING_FLAG_TYPE_BITS) - 1U)
static_assert(NOA_RING_TYPE_MAX <= (1U << NOA_RING_FLAG_TYPE_BITS));

struct noa_ring_info {
	u32 head;
	u32 tail;
	// This base address refers to its own Virtual Address space
	char *base;
	u32 item_len;
	u32 size;
	// This dpa base address is specifically designated for ring services within the DPA view space
	char *dpa_base;
};

/**
 * struct noa_ring_wrapper can be a consumer or a producer; it should be
 * decided at the beginning, and once its role is determined, it cannot
 * be changed.
 *
 * The member 'regs' in struct noa_ring_wrapper will store the address of
 * the shared information, which constructs a basic circular buffer.
 * This information includes the buffer address, head position, tail
 * position, length of items, and the total number of items in this buffer.
 *
 * Additionally, there is a special member 'curr_tail'/'curr_head' in this
 * structure. We use this value as a temporary position of the head and tail,
 * so we don't need to immediately write back its value while performing reading
 * or writing.
 */
typedef struct noa_ring_wrapper {
#ifdef linux
	unsigned long flags; /* reserve first 2 bits for wrapper type */
#else /* linux */
	atomic_t flags; /* reserve first 2 bits for wrapper type */
#endif /* linux */
	struct noa_ring_regs regs;
	struct noa_ring_info basic;
	u8 pos_shift; /* used to shift the value of `tail` or `head` */
	char name[24];
	const struct noa_ring_data_ops *data_ops;
	const struct noa_ring_ops *ops;
	void *owner;
	char *wrap_buf;
	size_t wrap_buf_sz;
} __aligned(4) noa_ring_consumer, noa_ring_producer;

static inline int noa_ring_type(const struct noa_ring_wrapper *ring)
{
#ifdef linux
	return ring->flags & NOA_RING_TYPE_MASK;
#else /* linux */
	return ring->flags.load(std::memory_order_relaxed) & NOA_RING_TYPE_MASK;
#endif /* linux */
}

static inline bool noa_ring_is_consumer(const struct noa_ring_wrapper *ring)
{
	return noa_ring_type(ring) == NOA_RING_TYPE_CONSUMER;
}

static inline bool noa_ring_is_producer(const struct noa_ring_wrapper *ring)
{
	return noa_ring_type(ring) == NOA_RING_TYPE_PRODUCER;
}

static inline u32 noa_ring_items_count(u32 head, u32 tail, u32 size)
{
	return (head + size - tail) % size;
}

static inline u32 noa_ring_free_items_count(u32 head, u32 tail, u32 size)
{
	return (tail + size - head - 1U) % size;
}

/* Return items available up to the end of the buffer. */
static inline u32 noa_ring_items_to_end_count(u32 head, u32 tail, u32 size)
{
#ifndef linux
	using std::min;
#endif /* linux */
	return min(noa_ring_items_count(head, tail, size), size - tail);
}

/* Return free items available up to the end of the buffer. */
static inline u32 noa_ring_free_items_to_end_count(u32 head, u32 tail, u32 size)
{
#ifndef linux
	using std::min;
#endif /* linux */
	return min(noa_ring_free_items_count(head, tail, size), size - head);
}

/* Return free items available from the start of the buffer. */
static inline u32 noa_ring_free_items_from_start_count(u32 head, u32 tail, u32 size)
{
#ifndef linux
	using std::min;
#endif /* linux */
	return min(noa_ring_free_items_count(head, tail, size), tail);
}

static inline u32 noa_ring_head_read_once(const struct noa_ring_wrapper *ring)
{
	return ring->data_ops->load_head(ring) >> ring->pos_shift;
}

static inline void noa_ring_head_write_once(struct noa_ring_wrapper *ring, u32 val)
{
	ring->data_ops->store_head(ring, val << ring->pos_shift);
}

static inline u32 noa_ring_tail_read_once(const struct noa_ring_wrapper *ring)
{
	return ring->data_ops->load_tail(ring) >> ring->pos_shift;
}

static inline void noa_ring_tail_write_once(struct noa_ring_wrapper *ring, u32 val)
{
	ring->data_ops->store_tail(ring, val << ring->pos_shift);
}

#ifdef linux
#define noa_ring_buf_pos(buf, pos, len) ((typeof(buf))((char *)buf) + (pos) * (len))
#else /* linux */
#define noa_ring_buf_pos(buf, pos, len) ((decltype(buf))((char *)buf) + (pos) * (len))
#endif /* linux */

static inline char *noa_ring_curr_tail_pos(const struct noa_ring_info *ring_info)
{
	return noa_ring_buf_pos(ring_info->base, ring_info->tail, ring_info->item_len);
}

static inline char *noa_ring_curr_head_pos(const struct noa_ring_info *ring_info)
{
	return noa_ring_buf_pos(ring_info->base, ring_info->head, ring_info->item_len);
}

static inline bool noa_ring_pos_is_moved(struct noa_ring_wrapper *ring)
{
	return test_bit(NOA_RING_FLAG_POS_MOVED, &ring->flags);
}

#define noa_ring_move_pos(pos, step, size) ((pos + step) % size)

static inline void noa_ring_info_tail_move(struct noa_ring_info *ring_info, u32 items)
{
	ring_info->tail = noa_ring_move_pos(ring_info->tail, items, ring_info->size);
}

static inline void noa_ring_tail_move(struct noa_ring_wrapper *ring, u32 items)
{
	set_bit(NOA_RING_FLAG_POS_MOVED, &ring->flags);
	noa_ring_info_tail_move(&ring->basic, items);
}

static inline void noa_ring_tail_inc(struct noa_ring_wrapper *ring)
{
	noa_ring_tail_move(ring, 1);
}

static inline void noa_ring_info_head_move(struct noa_ring_info *ring_info, u32 items)
{
	ring_info->head = noa_ring_move_pos(ring_info->head, items, ring_info->size);
}

static inline void noa_ring_head_move(struct noa_ring_wrapper *ring, u32 items)
{
	set_bit(NOA_RING_FLAG_POS_MOVED, &ring->flags);
	noa_ring_info_head_move(&ring->basic, items);
}

static inline void noa_ring_head_inc(struct noa_ring_wrapper *ring)
{
	noa_ring_head_move(ring, 1);
}

ssize_t noa_generic_read_raw_pointer(const void *data, size_t data_len, struct noa_iovec *iov);

const char *noa_ring_type_to_name(u32 type);
int noa_ring_regs_wrapper_init(struct noa_ring_wrapper *ring, int type,
			       const struct noa_ring_ops *ops, struct noa_ring_regs *regs,
			       void *owner, const char *name, int shift);
int noa_ring_basic_wrapper_init(struct noa_ring_wrapper *ring, int type,
				const struct noa_ring_ops *ops, struct noa_ring_regs *regs,
				void *owner, const char *name, int shift);
static inline bool is_noa_ring_activate(struct noa_ring_wrapper *ring)
{
	return test_bit(NOA_RING_FLAG_ACTIVE, &ring->flags);
}
void noa_ring_activate(struct noa_ring_wrapper *ring);
void noa_ring_deactivate(struct noa_ring_wrapper *ring);
int noa_ring_begin_processing(struct noa_ring_wrapper *ring);
void noa_ring_complete_processing(struct noa_ring_wrapper *ring);
void noa_ring_rollback(struct noa_ring_wrapper *ring, int val);
#define noa_ring_rollback_all(ring) noa_ring_rollback(ring, -1)
#define __noa_ring_is_empty(head, tail) (!!(head == tail))
#define __noa_ring_is_full(head, tail, size) (!!(((head + 1) % size) == tail))

static inline bool noa_ring_is_full(struct noa_ring_wrapper *ring)
{
	if (!is_noa_ring_activate(ring))
		return false;
	return __noa_ring_is_full(ring->basic.head, noa_ring_tail_read_once(ring),
				  ring->basic.size);
}

static inline bool noa_ring_is_empty(struct noa_ring_wrapper *ring)
{
	if (!is_noa_ring_activate(ring))
		return true;
	return __noa_ring_is_empty(noa_ring_head_read_once(ring), ring->basic.tail);
}

ssize_t noa_ring_readv_variable_length(noa_ring_consumer *consumer, struct noa_iovec *iov,
				       int iovcnt, int *pread, bool should_restore_wrapbuf);
ssize_t noa_ring_read_variable_length(noa_ring_consumer *consumer, void *data, size_t len,
				      bool should_restore_wrapbuf);
size_t noa_ring_info_read_variable_length(struct noa_ring_info *ring_info, void *data, size_t len,
					  ssize_t (*parse_payload_len)(const void *),
					  ssize_t (*read_payload)(const void *, size_t,
								  struct noa_iovec *),
					  char *wrap_buf, size_t wrap_buf_len,
					  bool should_restore_wrapbuf);
ssize_t noa_ring_writev_variable_length(noa_ring_producer *producer, const struct noa_iovec *iov,
					int iovcnt, int *pwrite);
ssize_t noa_ring_write_variable_length(noa_ring_producer *producer, const void *data, size_t len);
ssize_t
noa_ring_info_write_variable_length(struct noa_ring_info *ring_info, const void *data, size_t len,
				    ssize_t (*write_payload)(void *, size_t, const void *, size_t),
				    char *wrap_buf, size_t wrap_buf_len);
ssize_t noa_ring_readv(noa_ring_consumer *consumer, struct noa_iovec *iov, int iovcnt, int *pread);
ssize_t noa_ring_read(noa_ring_consumer *consumer, void *data, size_t len);
ssize_t noa_ring_writev(noa_ring_producer *producer, const struct noa_iovec *iov, int iovcnt,
			int *pwrite);
ssize_t noa_ring_write(noa_ring_producer *producer, const void *data, size_t len);
void noa_ring_info_setup(struct noa_ring_wrapper *ring, const struct noa_ring_info *info);
void noa_ring_info_clean(struct noa_ring_wrapper *ring);

#ifdef linux
#define noa_ring_for_each_item(ring, item, idx)                                                    \
	for (idx = 0, item = (typeof(item))(ring)->basic.base; idx < (ring)->.basic.size;          \
	     ++idx, item = (typeof(item))((char *)item + (ring)->basic.item_len))
#else /* linux */
#define noa_ring_for_each_item(ring, item, idx)                                                    \
	for (idx = 0, item = (decltype(item))(ring)->basic.base; idx < (ring)->basic.size;         \
	     ++idx, item = (decltype(item))((char *)item + (ring)->basic.item_len))
#endif /* linux */

/*
 * The Bit Ring is a circular buffer designed with a size that's a power of two. This binary
 * nature allows for efficient calculation of count and available space using bitwise operations
 */
/* Return count in buffer. */
#define noa_bitring_count(head, tail, size_mask) (((head) - (tail)) & (size_mask))
#define noa_bitring_is_empty(head, tail, size_mask) (!noa_bitring_count(head, tail, size_mask))
/* Return space available, 0..size_mask */
#define noa_bitring_space(head, tail, size_mask) noa_bitring_count((tail), ((head) + 1), (size_mask))
#define noa_bitring_is_full(head, tail, size_mask) (!noa_bitring_space(head, tail, size_mask))
#define noa_bitring_move_pos(index, val, size_mask) (((index) + (val)) & (size_mask))

#endif /* __NOA_RING_H__ */
// NOLINTEND
