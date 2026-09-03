#ifndef __NOA_MD_APC2NCP_RING_FULLSOC_H__
#define __NOA_MD_APC2NCP_RING_FULLSOC_H__

// From noa_md_apc2ncp_ring.h
#include "common/modem_ring_id.h"

struct noa_md_apc2ncp_ring_owner {
	void *owner;
	u32 ring_type;
};

struct noa_md_apc2ncp_tx_buffer_desc {
	void *desc_base;
	dma_addr_t skb_dma_addr;
	struct noa_ring_wrapper ring;
	struct noa_md_apc2ncp_ring_owner ring_owner;
};
static inline struct noa_md_apc2ncp_tx_buffer_desc *noa_md_apc2ncp_get_tx_buffer_desc(void)
{
	return NULL;
}
static inline u32 noa_md_apc2ncp_get_ring_item_len(u32 ring_type, bool is_desc)
{
	return -EINVAL;
}
static inline int noa_md_apc2ncp_ring_apc_setup(void *ring_owner)
{
	return -EINVAL;
}
static inline void noa_md_apc2ncp_ring_apc_release(void)
{
	return;
}
static inline int noa_md_apc2ncp_set_ring_write_idx(u32 q_num, u32 count)
{
	return -EINVAL;
}
static inline int noa_md_apc2ncp_get_ring_read_idx(u32 q_num)
{
	return -EINVAL;
}

// From noa_md_apc2ncp_ring.c
static const char *NoaModemApcToNcpRingName[kNoaModemApcToNcpRingMax] = {};
static u32 NoaModemApcToNcpRingSize[kNoaModemApcToNcpRingMax] = {};
static u64 NoaModemApcToNcpRingDescMemoryMap[kNoaModemApcToNcpRingMax] = {};
static u64 NoaModemApcToNcpRingDescDpaMemoryMap[kNoaModemApcToNcpRingMax] = {};
static u64 NoaModemApcToNcpRingDmaAddrMemoryMap[kNoaModemApcToNcpRingMax] = {};

// From ncp_md_irq.h
#define NCP_MD_PORT_MAX kNoaModemApcToNcpRingMax

struct ncp_md_irq_port {
	struct tasklet_struct irq_apc_task;
	struct tasklet_struct irq_ncp_task;
	u32 irq;
	u32 idx;
	u32 rcv_irq;
	irq_handler_t apc_isr;
	irq_handler_t ncp_isr;
	void *priv;
};

struct ncp_md_irq_simulator {
	struct ncp_md_irq_port ports[NCP_MD_PORT_MAX];
	struct tasklet_struct irq_apc_handler;
	struct tasklet_struct irq_ncp_handler;
#ifndef linux
	noa::module::notifier::NotifierMailbox irq_apc_notifier;
	noa::module::notifier::NotifierMailbox irq_ncp_notifier;
#endif
	u32 apc_intr_mask;
	u32 ncp_intr_mask;
	u32 dpmaif_q_mask;
	u32 noa_q_mask;
};

static inline struct ncp_md_irq_simulator *ncp_md_irq_sim_get(void)
{
	return NULL;
}
static inline void ncp_md_irq_set_ncp_intr_mask(u32 ring_type)
{
	return;
}
static inline void ncp_md_irq_notify_ncp(void)
{
	return;
}
static inline int ncp_md_irq_register(int id, irq_handler_t isr, void *priv, bool is_ncp)
{
	return -EINVAL;
}
static inline void ncp_md_irq_unregister(int id, void *priv, bool is_ncp)
{
	return;
}
static inline int ncp_md_irq_init(const char *name, bool is_ncp)
{
	return -EINVAL;
}
static inline void ncp_md_irq_exit(bool is_ncp)
{
	return;
}

#endif
