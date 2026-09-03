// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA MD (CPIF) Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>
 */

#include "noa_md_custom.h"

// TODO:
// all ncp md fw init procedure should be
// replaced with cmd/event path
#include <nep/nep.h>
#include <ncp/md/samsung/ncp_md_fw.h>
#include <nep/ring_manager.h>
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/modem_ring_id.h"

extern struct kobj_type noa_md_ktype; // for debug fs
extern void *noa_entry_register(u32 prv_sz,  const char *name, struct kobj_type *ktype);


static irqreturn_t noa_md_isr(int id, void *data);

void *noa_md_custom_init(struct noa_md_dev_init_info *init_info)
{
	struct noa_pktproc_adaptor_dl *noa_ppa_dl = &noa_pktproc_dl;
	struct mr_pktproc_adaptor_ul *mr_ppa_ul = &mr_pktproc_ul;
	struct noa_ring *ring = NULL;
	struct ncp_md_adaptor *p_md_adaptor = NULL; // for debug fs
	struct noa_md_client *p_client = NULL; // for debug fs
	int i;

	noa_ppa_dl->cp_base = init_info->cp_base;
	noa_ppa_dl->info_rgn_offset = init_info->info_rgn_offset;
	noa_ppa_dl->info_rgn_size = init_info->info_rgn_size;
	noa_ppa_dl->desc_rgn_offset = init_info->desc_rgn_offset;
	noa_ppa_dl->desc_rgn_size = init_info->desc_rgn_size;
	noa_ppa_dl->buff_rgn_offset = init_info->buff_rgn_offset;
	noa_ppa_dl->buff_rgn_size = init_info->buff_rgn_size;
	noa_ppa_dl->num_queue = init_info->num_queue;
	noa_ppa_dl->max_packet_size = init_info->max_packet_size;
	noa_ppa_dl->true_packet_size = init_info->true_packet_size;
	noa_ppa_dl->dev = init_info->dev;
	noa_ppa_dl->info_vbase = init_info->info_vbase;
	noa_ppa_dl->desc_vbase = init_info->desc_vbase;
	noa_ppa_dl->buff_vbase = init_info->buff_vbase;
	noa_ppa_dl->buff_pbase = init_info->buff_pbase;
	noa_ppa_dl->cp_buff_pbase = init_info->cp_buff_pbase;
	noa_ppa_dl->skb_padding_size = init_info->skb_padding_size;
	noa_ppa_dl->buff_size_by_q = init_info->buff_size_by_q;
	// simulator uses VA
	noa_ppa_dl->apc2noa_info_vbase = init_info->apc2noa_info_vbase;
	noa_ppa_dl->apc2noa_desc_vbase = init_info->apc2noa_desc_vbase;
	noa_ppa_dl->noa2apc_info_vbase = init_info->noa2apc_info_vbase;
	noa_ppa_dl->noa2apc_desc_vbase = init_info->noa2apc_desc_vbase;
	noa_ppa_dl->p_napi_noa2apc = init_info->p_napi_noa2apc;

	/* uplink path */
	mr_ppa_ul->cp_base = init_info->ul_cp_base;
	mr_ppa_ul->info_vbase = init_info->ul_info_vbase;
	mr_ppa_ul->desc_vbase = init_info->ul_desc_vbase;
	mr_ppa_ul->buff_vbase = init_info->ul_buff_vbase;
	mr_ppa_ul->default_max_packet_size =
		init_info->ul_default_max_packet_size;
	mr_ppa_ul->num_queue = init_info->ul_num_queue;
	mr_ppa_ul->desc_rgn_size = init_info->ul_desc_rgn_size;
	mr_ppa_ul->buff_rgn_offset = init_info->ul_buff_rgn_offset;
	for (i = 0; i < 2; i ++) {
		mr_ppa_ul->q_max_packet_size[i] = init_info->ul_q_max_packet_size[i];
	}
	mr_ppa_ul->cp_padding = init_info->ul_cp_padding;
	/* end of uplink path */

	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				    kNoaModemRingTxData, kNoaRingNepInput);
	ring->base = (unsigned long)(noa_ppa_dl->apc2noa_desc_vbase);
	ring->dpa_base = ring->base;

	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				    kNoaModemRingRxData, kNoaRingNepOutput);
	ring->base = (unsigned long)(noa_ppa_dl->noa2apc_desc_vbase);
	ring->dpa_base = ring->base;
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
							   kNoaNetworkFlowHostToDevice,
							   kNoaModemRingTxData),
				      kNoaRingNepInput);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
							   kNoaNetworkFlowDeviceToHost,
							   kNoaModemRingRxData),
				      kNoaRingNepOutput);

	// irq number of NOA_PORT_MODEM_FW is 4
	// defined in nep/nep.c
	noa_interrupt_register(4, noa_md_isr, noa_ppa_dl);

	// Error check
	// NOA sim only supports one queue
	if (1 != noa_ppa_dl->num_queue) {
		pr_err("NOA simulator only supports one queue, not (%d) queues",
			noa_ppa_dl->num_queue);
		return NULL;
	}

	// TODO: replace with cmd/event mechanism
	p_md_adaptor = ncp_md_init(
		noa_ppa_dl, init_info->buff_size_by_q,
		mr_ppa_ul, init_info->ul_buff_size_by_q);
	p_md_adaptor->p_mld = init_info->p_mld;
	p_md_adaptor->mr2cp_irq = init_info->mr2cp_irq;

	// debug fs, simulation only
	p_client =
		noa_entry_register(
			sizeof(struct noa_md_client),
			"modem",
			&noa_md_ktype
			);
	p_client->p_md_adaptor = p_md_adaptor;
	// FIXME: no unregister causes p_client memory leak
	return noa_ppa_dl;
}
EXPORT_SYMBOL_GPL(noa_md_custom_init);

u32 *noa_md_get_apc2noa_write_ref(void)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				    kNoaModemRingTxData, kNoaRingNepInput);
	return &ring->write;
}
EXPORT_SYMBOL_GPL(noa_md_get_apc2noa_write_ref);

u32 *noa_md_get_apc2noa_read_ref(void)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				    kNoaModemRingTxData, kNoaRingNepInput);
	return &ring->read;
}
EXPORT_SYMBOL_GPL(noa_md_get_apc2noa_read_ref);

void noa_md_set_apc2noa_max_num(u32 max_num)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				    kNoaModemRingTxData, kNoaRingNepInput);
	ring->ctrl = max_num;
}
EXPORT_SYMBOL_GPL(noa_md_set_apc2noa_max_num);

void noa_md_set_apc2noa_desc_size(u32 desc_size)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				    kNoaModemRingTxData, kNoaRingNepInput);
	ring->len = desc_size;
}
EXPORT_SYMBOL_GPL(noa_md_set_apc2noa_desc_size);

u32 *noa_md_get_noa2apc_write_ref(void)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				    kNoaModemRingRxData, kNoaRingNepOutput);
	return &ring->write;
}
EXPORT_SYMBOL_GPL(noa_md_get_noa2apc_write_ref);

u32 *noa_md_get_noa2apc_read_ref(void)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				    kNoaModemRingRxData, kNoaRingNepOutput);
	return &ring->read;
}
EXPORT_SYMBOL_GPL(noa_md_get_noa2apc_read_ref);

void noa_md_set_noa2apc_max_num(u32 max_num)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				    kNoaModemRingRxData, kNoaRingNepOutput);
	ring->ctrl = max_num;
}
EXPORT_SYMBOL_GPL(noa_md_set_noa2apc_max_num);

void noa_md_set_noa2apc_desc_size(u32 desc_size)
{
	struct noa_ring *ring = NULL;
	/* TODO: This function call will be replaced with cmd/event path */
	ring = NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				    kNoaModemRingRxData, kNoaRingNepOutput);
	ring->len = desc_size;
}
EXPORT_SYMBOL_GPL(noa_md_set_noa2apc_desc_size);

void noa_md_set_va_mapping(void *p_adaptor, void **p_map)
{
	struct noa_pktproc_adaptor_dl *noa_ppa_dl = (struct noa_pktproc_adaptor_dl *)p_adaptor;
	noa_ppa_dl->pf_buf = p_map;
}
EXPORT_SYMBOL_GPL(noa_md_set_va_mapping);

void noa_md_apc2noa_doorbell(void)
{
	//struct noa_port *port = &sim->ports[NOA_PORT_MODEM_SW];
	struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_SW); // may fail?
	port->doorbell = 1;
	noa_sim_trig_rx();
}
EXPORT_SYMBOL_GPL(noa_md_apc2noa_doorbell);

// modem sw port to apc interrupt handler
static irqreturn_t noa_md_isr(int id, void *data)
{
	struct noa_pktproc_adaptor_dl *noa_ppa_dl =
		(struct noa_pktproc_adaptor_dl *)data;

	// fake interrupt  to APC
	if (napi_schedule_prep(noa_ppa_dl->p_napi_noa2apc)) {
		__napi_schedule(noa_ppa_dl->p_napi_noa2apc);
	}

	return IRQ_HANDLED;
}

void dbg_pr_buf(u8 *p_buf, u32 len, u8 *prefix)
{
	int limit_sz = DBG_BUF_MAX_SIZE;
	int written_sz = 0;
	int dbg_ofst = 0;
	int i = 0;
	written_sz = snprintf(dbg_buf, limit_sz,
					"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	limit_sz -= written_sz;
	dbg_ofst += written_sz;
	if (limit_sz <= 0)
		return;

	for (i = 0; i < len; i++) {
		written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz, "%02x ", p_buf[i]);
		limit_sz -= written_sz;
		dbg_ofst += written_sz;
		if (limit_sz <= 0)
			break;

		if (0 == ((i + 1) % 16)) {
			written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz, "\n");
			limit_sz -= written_sz;
			dbg_ofst += written_sz;
			if (limit_sz <= 0)
				break;
		}
	}

	pr_info("%s\n", dbg_buf);
}
EXPORT_SYMBOL_GPL(dbg_pr_buf);

void dbg_pr_buf_w_fake_eth(u8 *p_buf, u32 len)
{
	int limit_sz = DBG_BUF_MAX_SIZE;
	int written_sz = 0;
	int dbg_ofst = 0;
	int i = 0;
	int raw_ofst = 0;
	if (limit_sz <= 0)
		return;
	written_sz = snprintf(dbg_buf, limit_sz,
					"      00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
	limit_sz -= written_sz;
	dbg_ofst += written_sz;

	if (limit_sz <= 0) {
		dbg_buf[dbg_ofst] = '\0';
	}

	// prefix offset, 0000
	// the 1st line, fake ether header and ethertype
	if (limit_sz <= 0)
		return;
	written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz,
					"%04x  ", raw_ofst);
	limit_sz -= written_sz;
	dbg_ofst += written_sz;

	for (i = 0; i < 6; i++) {
		if (limit_sz <= 0)
			break;
		written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz,
						"%02x ", fake_eth_src[i]);
		limit_sz -= written_sz;
		dbg_ofst += written_sz;
		raw_ofst++;
	}

	for (i = 0; i < 6; i++) {
		if (limit_sz <= 0)
			break;
		written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz,
						"%02x ", fake_eth_dst[i]);
		limit_sz -= written_sz;
		dbg_ofst += written_sz;
		raw_ofst++;
	}

	// Ether type
	if (6 == (p_buf[0] >> 4)) {
		// IPv6
		written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz,
						"86 dd ");
	} else if (4 == (p_buf[0] >> 4)) {
		// IPv4
		written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz,
						"08 00 ");
	} else {
		// Unknown
		written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz,
						"00 00 ");
	}
	limit_sz -= written_sz;
	dbg_ofst += written_sz;
	raw_ofst += 2;

	for (i = 0; i < len; i++) {
		if (0 == (raw_ofst % 16)) {
			if (limit_sz <= 0)
				break;
			written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz, "%04x  ", raw_ofst);
			limit_sz -= written_sz;
			dbg_ofst += written_sz;
		}

		if (limit_sz <= 0)
			break;
		written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz, "%02x ", p_buf[i]);
		limit_sz -= written_sz;
		dbg_ofst += written_sz;

		if (0 == ((raw_ofst + 1) % 16)) {
			if (limit_sz <= 0)
				break;
			written_sz = snprintf(dbg_buf + dbg_ofst, limit_sz, "\n");
			limit_sz -= written_sz;
			dbg_ofst += written_sz;
		}
		raw_ofst++;
	}

	if (limit_sz > 0)
		dbg_buf[dbg_ofst] = '\0';
	pr_info("%s\n", dbg_buf);
}
EXPORT_SYMBOL_GPL(dbg_pr_buf_w_fake_eth);
