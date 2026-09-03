/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA dma operation
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
// NOLINTBEGIN
#ifndef __NOA_HW_RING_H__
#define __NOA_HW_RING_H__

#ifdef linux
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#else /* linux */
#include "common/defs.h"
#include "linux_port/io.h"
#include "linux_port/spinlock.h"
#include "linux_port/types.h"
#endif /* linux */

struct noa_ring_regs {
	/* the start address of the ring buffer */
	u64 base;
	/* the size of each item in the ring buffer */
	u64 len;
	/* the total count of all slots in the ring buffer */
	u64 max_item;
	/* the point at which the consumer finds the next item in the buffer */
	u64 read;
	/* the point at which the producer inserts items into the buffer */
	u64 write;
	/* the start address of the ring buffer for ring services with the DPA view space */
	u64 dpa_base;
};

struct noa_ring_sw_csr {
	u64 base_addr;
	u32 len;
	u32 max_item;
	u32 read;
	u32 write;
};

enum {
	TX_RING_FLAG_ACTIVE = 0,
};

struct noa_hw_ring {
	char name[32];
	struct noa_ring_regs regs;
#ifdef linux
	spinlock_t lock;
#endif
	void *desc;
	u32 buf_size;
	u32 desc_sz;
	u16 ndesc;
	u16 read;
	u16 write;
	u8 flags;
	u8 hw_idx;
	u8 sn;
	u8 stride;
	dma_addr_t desc_dma;
};

static inline u32 hw_io_read(const char *base, u32 offset)
{
	const u32 *addr = (const u32 *)(base + offset);
	return readl(addr);
}

static inline void hw_io_write(char *base, u32 offset, u32 value)
{
	u32 *addr = (u32 *)(base + offset);
	writel(value, addr);
}

static inline u16 hw_io_readw(const char *base, u32 offset)
{
	const u16 *addr = (const u16 *)(base + offset);
	return readw(addr);
}

static inline void hw_io_writew(char *base, u32 offset, u16 value)
{
	u16 *addr = (u16 *)(base + offset);
	writew(value, addr);
}

static inline u32 sys_io_read(const u32 *addr)
{
	return readl(addr);
}

#define sys_io_read32(addr) sys_io_read(addr);

static inline u64 sys_io_read64(const u64 *addr)
{
	const u8 *ptr = (const u8 *)addr;
	u64 value = readl((const u32 *)ptr);

	ptr += 4;
	value |= ((u64)readl((const u32 *)ptr)) << 32ULL;
	return value;
}

static inline uintptr_t sys_io_readptr(const uintptr_t *addr)
{
#ifdef CONFIG_64BIT
	return sys_io_read64((const uint64_t *)addr);
#else /* CONFIG_64BIT */
	return sys_io_read32(addr);
#endif /* CONFIG_64BIT */
}

static inline u16 sys_io_readw(const u16 *addr)
{
	return readw(addr);
}

static inline void sys_io_writew(u16 *addr, u16 value)
{
	writew(value, addr);
}

static inline void sys_io_write(u32 *addr, u32 value)
{
	writel(value, addr);
}

#define sys_io_wr32(addr, val) sys_io_write(addr, val)

static inline void sys_io_wr64(u64 *addr, u64 var)
{
	u8 *ptr = (u8 *)addr;
	u32 value;

	value = (u32)var;
	writel(value, (u32 *)ptr);
	ptr += 4;
	value = (u32)(var >> 32);
	writel(value, (u32 *)ptr);
}

static inline void sys_io_writeptr(uintptr_t *addr, uintptr_t val)
{
#ifdef CONFIG_64BIT
	sys_io_wr64((uint64_t *)addr, (uint64_t)val);
#else /* CONFIG_64BIT */
	sys_io_wr32(addr, val);
#endif /* CONFIG_64BIT */
}

static inline u16 _noa_dma_get_read_count(u16 r, u16 w, u16 len)
{
	return (w < r) ? (len - r + w) : (w - r);
}

static inline u16 _noa_dma_get_write_count(u16 r, u16 w, u16 len)
{
	return (w < r) ? (r - w - 1) : (len - w - 1 + r);
}

static inline u16 _noa_dma_get_next_idx(u16 idx, u16 max_cnt)
{
	return (max_cnt == (idx + 1)) ? 0 : (idx + 1);
}

static inline u16 noa_dma_get_read_count(struct noa_hw_ring *ring)
{
	u16 cnt;

	if (!ring->regs.read || !ring->regs.write)
		return 0;

	ring->write = sys_io_read((u32 *)ring->regs.write);
	cnt = _noa_dma_get_read_count(ring->read, ring->write, ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

static inline u16 noa_dma_get_write_count(struct noa_hw_ring *ring)
{
	u16 cnt;

	if (!ring->regs.read || !ring->regs.write)
		return 0;

	ring->read = sys_io_read((u32 *)ring->regs.read);
	cnt = _noa_dma_get_write_count(ring->read, ring->write, ring->ndesc);
	return cnt > ring->ndesc ? 0 : cnt;
}

static inline u8 *noa_dma_get_read_base(struct noa_hw_ring *ring)
{
	return (u8 *)ring->desc + ring->read * ring->desc_sz;
}

static inline u8 *noa_dma_get_write_base(struct noa_hw_ring *ring)
{
	return (u8 *)ring->desc + ring->write * ring->desc_sz;
}

static inline void noa_dma_update_sw_write(struct noa_hw_ring *ring)
{
	ring->write = (ring->write + 1) % ring->ndesc;
}

static inline void noa_dma_update_sw_read(struct noa_hw_ring *ring)
{
	ring->read = (ring->read + 1) % ring->ndesc;
}

static inline void noa_dma_update_hw_write(struct noa_hw_ring *ring)
{
	sys_io_write((u32 *)ring->regs.write, ring->write);
}

static inline void noa_dma_update_hw_read(struct noa_hw_ring *ring)
{
	sys_io_write((u32 *)ring->regs.read, ring->read);
}

static inline int noa_dma_hw_init(struct noa_hw_ring *ring)
{
	struct noa_ring_regs *regs = &ring->regs;
	/*system io in simulator mode */
#ifdef CONFIG_64BIT
	sys_io_wr64((u64 *)regs->base, (u64)ring->desc);
#else /* CONFIG_64BIT */
	sys_io_write((u32 *)regs->base, (u32)ring->desc);
#endif /* CONFIG_64BIT */
	sys_io_write((u32 *)regs->len, ring->desc_sz);
	sys_io_write((u32 *)regs->max_item, ring->ndesc);
	sys_io_write((u32 *)regs->read, ring->read);
	sys_io_write((u32 *)regs->write, ring->write);
	return 0;
}
#endif /* __NOA_HW_RING_H__ */
// NOLINTEND
