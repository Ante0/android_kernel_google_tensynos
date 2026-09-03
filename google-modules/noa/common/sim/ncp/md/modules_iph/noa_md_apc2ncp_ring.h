/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD Driver
 *
 * Copyright 2025 Google LLC.
 */
#ifndef __NOA_MD_APC2NCP_RING_H__
#define __NOA_MD_APC2NCP_RING_H__

#include "common/modem_ring_id.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "ring_mgmt/ring_manager.h"

#include "linux_port/types.h"
#include "sys_log.h"

// TODO: Remove it for real device
#ifndef DRAM_FAKE_MODEM_MEMORY_BASE
#define DRAM_FAKE_MODEM_BUFFER_OFFSET (0x1800000)
#define NOA_DRAM_BASE (0x80000000)
#define DRAM_FAKE_MODEM_MEMORY_BASE \
  (NOA_DRAM_BASE + DRAM_FAKE_MODEM_BUFFER_OFFSET)
#endif

struct noa_md_apc2ncp_ring_owner {
	void *owner;
	u32 ring_type;
};

struct noa_md_apc2ncp_tx_buffer_desc {
	void *desc_base;
	void *desc_dpa_base;
	dma_addr_t skb_dma_addr;
	struct noa_ring_wrapper ring;
	struct noa_md_apc2ncp_ring_owner ring_owner;
};

enum noa_md_apc2ncp_ring_index_type {
	NOA_MD_RING_WRITE_INDEX,
	NOA_MD_RING_READ_INDEX,
	NOA_MD_RING_TEMP_READ_INDEX,
	NOA_MD_RING_INDEX_MAX,
};

extern struct noa_md_apc2ncp_tx_buffer_desc apc2ncp_tx_buffer_desc[];
extern const char *NoaModemApcToNcpRingName[];
extern u32 NoaModemApcToNcpRingSize[];
extern u64 NoaModemApcToNcpRingDescMemoryMap[];
extern u64 NoaModemApcToNcpRingDescDpaMemoryMap[];
extern u64 NoaModemApcToNcpRingDmaAddrMemoryMap[];

struct noa_md_apc2ncp_tx_buffer_desc *noa_md_apc2ncp_get_tx_buffer_desc(void);
ssize_t noa_md_apc2ncp_desc_write(void *output_buf, size_t buf_len, const void *input_data,
				  size_t data_len);
void noa_md_apc2ncp_trigger_doorbell(struct noa_ring_wrapper *ring);
const char *noa_md_apc2ncp_get_ring_name(u32 ring_type);
u32 noa_md_apc2ncp_get_ring_item_len(u32 ring_type, bool is_desc);
u32 noa_md_apc2ncp_get_ring_size(u32 ring_type);
#ifndef linux
void noa_md_apc2ncp_ring_memory_map_setup(void);
#endif
int noa_md_apc2ncp_ring_apc_setup(void *ring_owner);
void noa_md_apc2ncp_ring_apc_release(void);
int noa_md_apc2ncp_ring_ncp_setup(void *ring_owner);
void noa_md_apc2ncp_ring_ncp_release(void);
int noa_md_apc2ncp_set_ring_write_idx(u32 q_num, u32 count);
int noa_md_apc2ncp_get_ring_read_idx(u32 q_num);
int noa_ncp_md_apc2ncp_set_ring_read_idx(u32 q_num, u32 count);
int noa_ncp_md_apc2ncp_set_ring_index_by_type(u32 q_num,
					      enum noa_md_apc2ncp_ring_index_type type,
					      u32 ring_index);
u32 noa_ncp_md_apc2ncp_get_ring_index_by_type(u32 q_num,
					      enum noa_md_apc2ncp_ring_index_type type);
#endif /* __NOA_MD_APC2NCP_RING_H__ */
