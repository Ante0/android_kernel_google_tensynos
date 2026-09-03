// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA core header file
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include "common/ring.h"

#include <common/noa_share/types.h>

#include "common/noatrace.h"
#include "common/memory.h"
#else /* linux */
#include <cstdio>
#include <cstring>

#include "arch/memory.h"
#include "common/defs.h"
#include "common/macro.h"
#include "common/ring.h"
#include "linux_port/log.h"
#include "linux_port/noa_share/types.h"
#endif /* linux */
/*
 * fill the raw pointer address into `iov->base` buffer, and return the
 * original buffer length.
 */
ssize_t noa_generic_read_raw_pointer(const void *data, size_t data_len, struct noa_iovec *iov)
{
	*((unsigned long *)iov->base) = (unsigned long)data;
	iov->len = data_len;
	return data_len;
}

#define NOA_RING_REG_SETGET_FUNCS(name, member, bits)                                              \
	static inline u##bits noa_ring_regs_##name(const struct noa_ring_wrapper *ring)            \
	{                                                                                          \
		InvalidateDCache((void *)ring->regs.member, sizeof(u##bits));                      \
		return sys_io_read##bits((const u##bits *)ring->regs.member);                      \
	}                                                                                          \
	static inline void noa_ring_regs_set_##name(struct noa_ring_wrapper *ring,                 \
						    const u##bits val)                             \
	{                                                                                          \
		sys_io_wr##bits((u##bits *)ring->regs.member, val);                                \
		FlushDCache((void *)ring->regs.member, sizeof(u##bits));                           \
	}

NOA_RING_REG_SETGET_FUNCS(head, write, 32);
NOA_RING_REG_SETGET_FUNCS(tail, read, 32);
NOA_RING_REG_SETGET_FUNCS(base, base, 64);
NOA_RING_REG_SETGET_FUNCS(item_len, len, 32);
NOA_RING_REG_SETGET_FUNCS(size, max_item, 32);
NOA_RING_REG_SETGET_FUNCS(dpa_base, dpa_base, 64);

#define NOA_RING_VAL_SETGET_FUNCS(name, member, bits)                                              \
	static inline u##bits noa_ring_val_##name(const struct noa_ring_wrapper *ring)             \
	{                                                                                          \
		InvalidateDCache((void *)&ring->basic.member, sizeof(u##bits));                    \
		return smp_load_acquire((const u##bits *)&ring->basic.member);                     \
	}                                                                                          \
	static inline void noa_ring_val_set_##name(struct noa_ring_wrapper *ring, u##bits val)     \
	{                                                                                          \
		smp_store_release((u##bits *)&ring->basic.member, val);                            \
		FlushDCache((void *)&ring->basic.member, sizeof(u##bits));                         \
	}

NOA_RING_VAL_SETGET_FUNCS(base, base, 64);
NOA_RING_VAL_SETGET_FUNCS(item_len, item_len, 32);
NOA_RING_VAL_SETGET_FUNCS(size, size, 32);
NOA_RING_VAL_SETGET_FUNCS(dpa_base, dpa_base, 64);

// clang-format off
#define DEFINE_NOA_RING_OPS_ENTRY(entry, type)                  \
	.load_##entry = noa_ring_##type##_##entry,              \
	.store_##entry = noa_ring_##type##_set_##entry,

const static struct noa_ring_data_ops regs_ops = {
	DEFINE_NOA_RING_OPS_ENTRY(head, regs)
	DEFINE_NOA_RING_OPS_ENTRY(tail, regs)
	DEFINE_NOA_RING_OPS_ENTRY(base, regs)
	DEFINE_NOA_RING_OPS_ENTRY(item_len, regs)
	DEFINE_NOA_RING_OPS_ENTRY(size, regs)
	DEFINE_NOA_RING_OPS_ENTRY(dpa_base, regs)
};

const static struct noa_ring_data_ops basic_ops = {
	DEFINE_NOA_RING_OPS_ENTRY(head, regs)
	DEFINE_NOA_RING_OPS_ENTRY(tail, regs)
	DEFINE_NOA_RING_OPS_ENTRY(base, val)
	DEFINE_NOA_RING_OPS_ENTRY(item_len, val)
	DEFINE_NOA_RING_OPS_ENTRY(size, val)
	DEFINE_NOA_RING_OPS_ENTRY(dpa_base, val)
};
// clang-format on

/**
 * noa_ring_readv - Reads `iovcnt` items from the Ring buffer into the buffers described by iov.
 * @consumer: 		the ring buffer wrapper
 * @iov: 		an array of iovec structures
 * @iovcnt: 		the number of iovec in `iov`
 * @pread: 		return pointer the number of items read this time.
 *
 * This function will increment the `tail` pointer of the consumer by one when it
 * successfully reads a data from the Ring buffer. It returns the size of bytes it reads.
 * If it encounters an error while reading, it will stop the process and return the error
 * code, and the `tail` pointer of the consumer will then point to the next item of the
 * latest item it successfully reads.
 *
 * Note that the caller should call the begin_processing() API before calling this
 * function to mark the processing flag on the Ring buffer. After calling this function,
 * the caller must use the complete_processing() API to clean the processing flag and
 * flush the value of the `tail` pointer of the consumer into the Ring buffer.
 */
static ssize_t
readv_variable_length(struct noa_ring_info *ring_info, struct noa_iovec *iov, int iovcnt,
		      int *pread, ssize_t (*parse_payload_len)(const void *),
		      ssize_t (*read_payload)(const void *, size_t, struct noa_iovec *),
		      char *wrap_buf, size_t wrap_buf_sz, bool should_restore_wrapbuf)
{
	ssize_t ret;
	int pos = 0;
	ssize_t total = 0;
	u32 items = noa_ring_items_count(ring_info->head, ring_info->tail, ring_info->size);
	u32 to_end_items =
		noa_ring_items_to_end_count(ring_info->head, ring_info->tail, ring_info->size);

	if (!iov || !iovcnt || !pread)
		return -EINVAL;

	while (pos < iovcnt && items) {
		char *curr = noa_ring_curr_tail_pos(ring_info);
		ssize_t size = 0;
		ssize_t payload_len = ring_info->item_len;
		u32 occupied_items = 1U;
		u32 origin_to_end_items = to_end_items;

		if (parse_payload_len) {
			payload_len = parse_payload_len(curr);
			if (payload_len < 0) {
				ret = payload_len;
				goto out;
			}
			occupied_items = DIV_ROUND_UP((u32)payload_len, ring_info->item_len);

			if (items < occupied_items) {
				break;
			}
			if (to_end_items < occupied_items) {
				ssize_t to_end_size = to_end_items * ring_info->item_len;
				if ((ssize_t) wrap_buf_sz < payload_len) {
					pr_err("Failed to handle wrapping data on ring"
					       "with buf size %u, data size %u\n",
					       (u32)wrap_buf_sz, (u32)payload_len);
					ret = -EINVAL;
					goto out;
				}
				memcpy(wrap_buf, curr, to_end_size);
				memcpy(wrap_buf + to_end_size, ring_info->base,
				       payload_len - to_end_size);
				curr = wrap_buf;
				to_end_items = items;
			}
		}
		InvalidateDCache(curr, payload_len);
		size = read_payload(curr, payload_len, &iov[pos]);
		if (size != payload_len) {
			ret = (size < 0) ? size : -EIO;
			if (should_restore_wrapbuf && curr == wrap_buf) {
				ssize_t to_end_size = origin_to_end_items * ring_info->item_len;
				curr = noa_ring_curr_tail_pos(ring_info);
				memcpy(curr, wrap_buf, to_end_size);
				memcpy(ring_info->base, wrap_buf + to_end_size,
				       payload_len - to_end_size);
			}
			goto out;
		}
		total += size;
		pos++;
		noa_ring_info_tail_move(ring_info, occupied_items);
		items -= occupied_items;
		to_end_items -= occupied_items;
	}

	ret = total;
out:
	*pread = pos;
	return ret;
}

ssize_t noa_ring_readv_variable_length(noa_ring_consumer *consumer, struct noa_iovec *iov,
				       int iovcnt, int *pread, bool should_restore_wrapbuf)
{
	ssize_t ret;
	consumer->basic.head = noa_ring_head_read_once(consumer);
	ret = readv_variable_length(&consumer->basic, iov, iovcnt, pread,
				    consumer->ops->payload_len, consumer->ops->read_payload,
				    consumer->wrap_buf, consumer->wrap_buf_sz,
				    should_restore_wrapbuf);
	if (pread && *pread) {
		set_bit(NOA_RING_FLAG_POS_MOVED, &consumer->flags);
	}
	return ret;
}

ssize_t noa_ring_read_variable_length(noa_ring_consumer *consumer, void *data, size_t len,
				      bool should_restore_wrapbuf)
{
	int read;
	struct noa_iovec iov = {
		.base = data,
		.len = len,
	};
	return noa_ring_readv_variable_length(consumer, &iov, 1, &read, should_restore_wrapbuf);
}

size_t noa_ring_info_read_variable_length(struct noa_ring_info *ring, void *data, size_t len,
					  ssize_t (*parse_payload_len)(const void *),
					  ssize_t (*read_payload)(const void *, size_t,
								  struct noa_iovec *),
					  char *wrap_buf, size_t wrap_buf_sz,
					  bool should_restore_wrapbuf)
{
	int read;
	struct noa_iovec iov = {
		.base = data,
		.len = len,
	};
	return readv_variable_length(ring, &iov, 1, &read, parse_payload_len, read_payload,
				     wrap_buf, wrap_buf_sz, should_restore_wrapbuf);
}

ssize_t noa_ring_readv(noa_ring_consumer *consumer, struct noa_iovec *iov, int iovcnt, int *pread)
{
	ssize_t ret;
	int pos = 0;
	ssize_t total = 0;
	u32 items = noa_ring_items_count(noa_ring_head_read_once(consumer), consumer->basic.tail,
					 consumer->basic.size);

	if (!iov || !iovcnt || !pread)
		return -EINVAL;

	while (pos < iovcnt && items) {
		char *curr = noa_ring_curr_tail_pos(&consumer->basic);
		ssize_t size;
		InvalidateDCache(curr, consumer->basic.item_len);
		size = consumer->ops->read_payload(curr, consumer->basic.item_len, &iov[pos]);
		if (size < 0) {
			ret = size;
			goto out;
		}
		total += size;
		pos++;
		noa_ring_tail_inc(consumer);
		items--;
	}

	ret = total;
out:
	*pread = pos;
	return ret;
}

ssize_t noa_ring_read(noa_ring_consumer *consumer, void *data, size_t len)
{
	int read;
	struct noa_iovec iov = {
		.base = data,
		.len = len,
	};
	return noa_ring_readv(consumer, &iov, 1, &read);
}

/**
 * noa_ring_write - Writes data with size `len` into the Ring buffer.
 * @producer: 		the ring buffer wrapper
 * @data: 		the pointer of data
 * @len: 		the bytes of data
 *
 * This function will increment the `head` pointer of the producer by one when it
 * successfully writes an data to the Ring buffer. It returns the size of bytes it reads
 * If it encounters an error while writing, it will stop the process and return the error
 * code, and the `head` pointer of the producer won't be moved.
 *
 * Note that the caller should call the begin_processing() API before calling this
 * function to mark the processing flag on the Ring buffer. After calling this function,
 * the caller must use the complete_processing() API to clean the processing flag and
 * flush the value of the `tail` pointer of the consumer into the Ring buffer.
 */
static ssize_t
writev_variable_length(struct noa_ring_info *ring_info, const struct noa_iovec *iov, int iovcnt,
		       int *pwrite, ssize_t (*write_payload)(void *, size_t, const void *, size_t),
		       char *wrap_buf, size_t wrap_buf_sz)
{
	int pos = 0;
	ssize_t ret;
	ssize_t total = 0;
	u32 free_items =
		noa_ring_free_items_count(ring_info->head, ring_info->tail, ring_info->size);
	u32 to_end_free_items =
		noa_ring_free_items_to_end_count(ring_info->head, ring_info->tail, ring_info->size);

	if (!iov || !iovcnt || !pwrite)
		return -EINVAL;

	while (pos < iovcnt) {
		char *curr = noa_ring_curr_head_pos(ring_info);
		ssize_t size;
		u32 occupied_items = DIV_ROUND_UP((u32)iov[pos].len, ring_info->item_len);

		if (free_items < occupied_items) {
			ret = -EAGAIN;
			goto out;
		}

		if (to_end_free_items < occupied_items) {
			ssize_t to_end_size = to_end_free_items * ring_info->item_len;
			size = write_payload(wrap_buf, wrap_buf_sz, iov[pos].base, iov[pos].len);
			if (size < 0) {
				ret = size;
				goto out;
			}
			memcpy(curr, wrap_buf, to_end_size);
			FlushDCache(curr, to_end_size);
			memcpy(ring_info->base, wrap_buf + to_end_size, size - to_end_size);
			FlushDCache(ring_info->base, size - to_end_size);
			to_end_free_items = free_items;

		} else {
			size = write_payload(curr, free_items * ring_info->item_len, iov[pos].base,
					     iov[pos].len);
			if (size < 0) {
				ret = size;
				goto out;
			}
			FlushDCache(curr, size);
		}
		total += size;
		++pos;
		noa_ring_info_head_move(ring_info, occupied_items);
		free_items -= occupied_items;
		to_end_free_items -= occupied_items;
	}

	ret = total;
out:
	*pwrite = pos;
	return ret;
}

ssize_t noa_ring_writev_variable_length(noa_ring_producer *producer, const struct noa_iovec *iov,
					int iovcnt, int *pwrite)
{
	ssize_t ret;
	producer->basic.tail = noa_ring_tail_read_once(producer);
	ret = writev_variable_length(&producer->basic, iov, iovcnt, pwrite,
				     producer->ops->write_payload, producer->wrap_buf,
				     producer->wrap_buf_sz);
	if (pwrite && *pwrite) {
		set_bit(NOA_RING_FLAG_POS_MOVED, &producer->flags);
	}
	return ret;
}

ssize_t noa_ring_write_variable_length(noa_ring_producer *producer, const void *data, size_t len)
{
	int write;
	const struct noa_iovec iov = {
		.base = NOA_CONST_CAST(void *, data),
		.len = len,
	};
	return noa_ring_writev_variable_length(producer, &iov, 1, &write);
}

ssize_t
noa_ring_info_write_variable_length(struct noa_ring_info *ring_info, const void *data, size_t len,
				    ssize_t (*write_payload)(void *, size_t, const void *, size_t),
				    char *wrap_buf, size_t wrap_buf_sz)
{
	int write;
	const struct noa_iovec iov = {
		.base = NOA_CONST_CAST(void *, data),
		.len = len,
	};
	return writev_variable_length(ring_info, &iov, 1, &write, write_payload, wrap_buf,
				      wrap_buf_sz);
}

ssize_t noa_ring_writev(noa_ring_producer *producer, const struct noa_iovec *iov, int iovcnt,
			int *pwrite)
{
	int pos = 0;
	ssize_t ret;
	ssize_t total = 0;
	u32 items = noa_ring_free_items_count(
		producer->basic.head, noa_ring_tail_read_once(producer), producer->basic.size);

	if (!iov || !iovcnt || !pwrite)
		return -EINVAL;

	if (!items) {
		ret = -EAGAIN;
		goto out;
	}

	while (pos < iovcnt && items) {
		char *curr = noa_ring_curr_head_pos(&producer->basic);
		ssize_t size = producer->ops->write_payload(curr, producer->basic.item_len,
							    iov[pos].base, iov[pos].len);
		if (size < 0) {
			ret = size;
			goto out;
		}
		FlushDCache(curr, size);
		total += size;
		++pos;
		noa_ring_head_inc(producer);
		items--;
	}

	ret = total;
out:
	*pwrite = pos;
	return ret;
}

ssize_t noa_ring_write(noa_ring_producer *producer, const void *data, size_t len)
{
	int write;
	const struct noa_iovec iov = {
		.base = NOA_CONST_CAST(void *, data),
		.len = len,
	};
	return noa_ring_writev(producer, &iov, 1, &write);
}

static inline void load_ring_info(struct noa_ring_wrapper *ring)
{
	ring->basic.base = (char *)ring->data_ops->load_base(ring);
	ring->basic.size = ring->data_ops->load_size(ring);
	ring->basic.item_len = ring->data_ops->load_item_len(ring);
	ring->basic.tail = noa_ring_tail_read_once(ring);
	ring->basic.head = noa_ring_head_read_once(ring);
	ring->basic.dpa_base = (char *)ring->data_ops->load_dpa_base(ring);
}

void noa_ring_activate(struct noa_ring_wrapper *ring)
{
	if (is_noa_ring_activate(ring))
		pr_warn("This ring %s has already been activate\n", ring->name);
	load_ring_info(ring);
	set_bit(NOA_RING_FLAG_ACTIVE, &ring->flags);
}

void noa_ring_deactivate(struct noa_ring_wrapper *ring)
{
	// TODO: we should wait the ring has leaved processing state.
	clear_bit(NOA_RING_FLAG_ACTIVE, &ring->flags);
}

static int __noa_ring_wrapper_init(struct noa_ring_wrapper *ring, int type,
				   const struct noa_ring_ops *ops, struct noa_ring_regs *regs,
				   void *owner, const char *name, int shift,
				   const struct noa_ring_data_ops *data_ops)
{
	if (!ring || !ops || !regs || !data_ops)
		return -EINVAL;
	memset((void *)ring, 0, sizeof(struct noa_ring_wrapper));
	ring->ops = ops;
	ring->data_ops = data_ops;
	ring->regs = *regs;
	ring->owner = owner;
	ring->flags |= (type & NOA_RING_TYPE_MASK);
	ring->pos_shift = shift;
	snprintf(ring->name, sizeof(ring->name), "%s", name);

	return 0;
}

int noa_ring_regs_wrapper_init(struct noa_ring_wrapper *ring, int type,
			       const struct noa_ring_ops *ops, struct noa_ring_regs *regs,
			       void *owner, const char *name, int shift)
{
	return __noa_ring_wrapper_init(ring, type, ops, regs, owner, name, shift, &regs_ops);
}

int noa_ring_basic_wrapper_init(struct noa_ring_wrapper *ring, int type,
				const struct noa_ring_ops *ops, struct noa_ring_regs *regs,
				void *owner, const char *name, int shift)
{
	return __noa_ring_wrapper_init(ring, type, ops, regs, owner, name, shift, &basic_ops);
}

/*
 * return 1: success
 *        0: already in process
 *       <0: error
 */
int noa_ring_begin_processing(struct noa_ring_wrapper *ring)
{
#ifdef linux
	trace_ring_begin_processing(ring);
#endif /* linux */
	if (!is_noa_ring_activate(ring))
		return -EINVAL;

	if (ring->ops->begin_hook) {
		ring->ops->begin_hook(ring);
		set_bit(NOA_RING_FLAG_PROCESSING, &ring->flags);
	} else if (test_and_set_bit(NOA_RING_FLAG_PROCESSING, &ring->flags)) {
		return 0;
	}

	return 1;
}

void noa_ring_complete_processing(struct noa_ring_wrapper *ring)
{
#ifdef linux
	trace_ring_complete_processing(ring);
#endif /* linux */
	if (!test_bit(NOA_RING_FLAG_PROCESSING, &ring->flags))
		return;

	if (ring->ops->complete_prehook)
		ring->ops->complete_prehook(ring);

	if (noa_ring_pos_is_moved(ring)) {
		switch (noa_ring_type(ring)) {
		case NOA_RING_TYPE_CONSUMER:
			noa_ring_tail_write_once(ring, ring->basic.tail);
			break;
		case NOA_RING_TYPE_PRODUCER:
			noa_ring_head_write_once(ring, ring->basic.head);
			break;
		default:
			break;
		}
	}

	clear_bit(NOA_RING_FLAG_PROCESSING, &ring->flags);

	if (ring->ops->complete_hook)
		ring->ops->complete_hook(ring);

	clear_bit(NOA_RING_FLAG_POS_MOVED, &ring->flags);
}

static inline void rollback_consumer_tail(noa_ring_consumer *consumer, int val)
{
	int orig_tail = noa_ring_tail_read_once(consumer);
	int step = noa_ring_items_count(consumer->basic.tail, orig_tail, consumer->basic.size);
	if (val == -1 || val >= step) {
		consumer->basic.tail = orig_tail;
		clear_bit(NOA_RING_FLAG_POS_MOVED, &consumer->flags);
		return;
	}
	noa_ring_tail_move(consumer, consumer->basic.size - val);
}

static inline void rollback_producer_head(noa_ring_producer *producer, int val)
{
	int orig_head = noa_ring_head_read_once(producer);
	int step = noa_ring_items_count(producer->basic.head, orig_head, producer->basic.size);
	if (val == -1 || val >= step) {
		producer->basic.head = orig_head;
		clear_bit(NOA_RING_FLAG_POS_MOVED, &producer->flags);
		return;
	}
	noa_ring_head_move(producer, producer->basic.size - val);
}

/**
 * This function will rollback the temporary value by 'val' steps, if the 'val'
 * is "-1" it will rollback the temporary value to the original value.
 */
void noa_ring_rollback(struct noa_ring_wrapper *ring, int val)
{
	switch (noa_ring_type(ring)) {
	case NOA_RING_TYPE_CONSUMER:
		rollback_consumer_tail(ring, val);
		break;
	case NOA_RING_TYPE_PRODUCER:
		rollback_producer_head(ring, val);
		break;
	default:
		break;
	}
}

const char *noa_ring_type_to_name(u32 type)
{
	switch (type) {
	case NOA_RING_TYPE_CONSUMER:
		return "consumer";
	case NOA_RING_TYPE_PRODUCER:
		return "producer";
	default:
		break;
	}
	return "UNKNOWN";
}

void noa_ring_info_setup(struct noa_ring_wrapper *ring, const struct noa_ring_info *info)
{
	if (!ring->data_ops) {
		return;
	}
	ring->data_ops->store_head(ring, info->head);
	ring->data_ops->store_tail(ring, info->tail);
	ring->data_ops->store_base(ring, (u64)info->base);
	ring->data_ops->store_item_len(ring, info->item_len);
	ring->data_ops->store_size(ring, info->size);
	ring->data_ops->store_dpa_base(ring, (u64)info->dpa_base);
	load_ring_info(ring);
}

void noa_ring_info_clean(struct noa_ring_wrapper *ring)
{
	const struct noa_ring_info zero_info = {
		.head = 0,
		.tail = 0,
		.base = NULL,
		.item_len = 0,
		.size = 0,
		.dpa_base = NULL,
	};
	noa_ring_info_setup(ring, &zero_info);
}
