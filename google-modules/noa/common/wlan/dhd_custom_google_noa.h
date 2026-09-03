/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux cfg80211 driver - Dongle Host Driver (DHD) related
 *
 * Copyright (C) 2022, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef __DHD_CUSTOM_GOOGLE_NOA_H__
#define __DHD_CUSTOM_GOOGLE_NOA_H__

#include <bcmpcie.h>
#include <dhd.h>
#include <dhd_flowring.h>
#include <pcie_core.h>
#include <dhd_pcie.h>
#include <bcmmsgbuf.h>
#include <dhd_plat.h>
#include <bcmendian.h>

#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmmsgbuf.h>
#include <bcmendian.h>
#include <bcmstdlib_s.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <dhd_bus.h>

#include <typedefs.h>
#include <osl.h>

#include <bcmutils.h>
#include <bcmmsgbuf.h>
#include <bcmendian.h>
#include <bcmstdlib_s.h>

#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_proto.h>
#include <pcicfg.h>
#include <sbchipc.h>

#define noa_wlan_err(fmt, ...) \
do {                            \
	pr_err("%s:" fmt, "noa_wlan", ##__VA_ARGS__);    \
} while (0)

enum dhd_mapper_pool_id {
	DHD_MAPPER_POOL_APC_RX,
	DHD_MAPPER_POOL_APC_TX,

	__DHD_MAPPER_POOL_MAX,
};

#define INVALID_TXQ_ID 0xFFFFU
#define FLOWID_TO_RINGID(flowid) (flowid - 2)

/*  data structure align */
#define RING_NAME_MAX_LENGTH 24
#define TXP_FLUSH_NITEMS
#define DHD_DEBUG_INVALID_PKTID

/* First TxPost Flowring Id */
#define DHD_FLOWRING_START_FLOWID   BCMPCIE_H2D_COMMON_MSGRINGS

/* Determine whether a ringid belongs to a TxPost flowring */
#define DHD_IS_FLOWRING(ringid, max_flow_rings) \
	((ringid) >= BCMPCIE_COMMON_MSGRINGS && \
	(ringid) < ((max_flow_rings) + BCMPCIE_COMMON_MSGRINGS))

/* Convert a H2D TxPost FlowId to a MsgBuf RingId */
#define DHD_FLOWID_TO_RINGID(flowid) \
	(BCMPCIE_COMMON_MSGRINGS + ((flowid) - BCMPCIE_H2D_COMMON_MSGRINGS))

/* Convert a MsgBuf RingId to a H2D TxPost FlowId */
#define DHD_RINGID_TO_FLOWID(ringid) \
	(BCMPCIE_H2D_COMMON_MSGRINGS + ((ringid) - BCMPCIE_COMMON_MSGRINGS))

/* Convert a H2D MsgBuf RingId to an offset index into the H2D DMA indices array
 * This may be used for the H2D DMA WR index array or H2D DMA RD index array or
 * any array of H2D rings.
 */
#define DHD_H2D_RING_OFFSET(ringid) \
	(((ringid) >= BCMPCIE_COMMON_MSGRINGS) ? DHD_RINGID_TO_FLOWID(ringid) : (ringid))

/* Convert a H2D MsgBuf Flowring Id to an offset index into the H2D DMA indices array
 * This may be used for IFRM.
 */
#define DHD_H2D_FRM_FLOW_RING_OFFSET(ringid) \
	((ringid) - BCMPCIE_COMMON_MSGRINGS)

/* Convert a D2H MsgBuf RingId to an offset index into the D2H DMA indices array
 * This may be used for the D2H DMA WR index array or D2H DMA RD index array or
 * any array of D2H rings.
 * d2h debug ring is located at the end, i.e. after all the tx flow rings and h2d debug ring
 * max_h2d_rings: total number of h2d rings
 */
#define DHD_D2H_RING_OFFSET(ringid, max_h2d_rings) \
	((ringid) > (max_h2d_rings) ? \
		((ringid) - max_h2d_rings) : \
		((ringid) - BCMPCIE_H2D_COMMON_MSGRINGS))

/* Convert a D2H DMA Indices Offset to a RingId */
#define DHD_D2H_RINGID(offset) \
	((offset) + BCMPCIE_H2D_COMMON_MSGRINGS)

#define DHD_FLOWRINGS_POOL_OFFSET(flowid) \
	((flowid) - BCMPCIE_H2D_COMMON_MSGRINGS)

#define DHD_RING_IN_FLOWRINGS_POOL(prot, flowid) \
	((msgbuf_ring_t *)((prot)->h2d_flowrings_pool) + \
	    DHD_FLOWRINGS_POOL_OFFSET(flowid))

#define FOREACH_RING_IN_FLOWRINGS_POOL(prot, ring, flowid, total_flowrings) \
	for ((flowid) = DHD_FLOWRING_START_FLOWID, \
		(ring) = DHD_RING_IN_FLOWRINGS_POOL(prot, flowid); \
		 (flowid) < ((total_flowrings) + DHD_FLOWRING_START_FLOWID); \
		 (ring)++, (flowid)++)

#define USE_DHD_PKTID_LOCK   1
#define DHD_PKTID_LOCK_INIT(osh)                osl_spin_lock_init(osh)
#define DHD_PKTID_LOCK_DEINIT(osh, lock)        osl_spin_lock_deinit(osh, lock)
#define DHD_PKTID_LOCK(lock, flags) 			(flags = osl_spin_lock(lock))
#define DHD_PKTID_UNLOCK(lock, flags)           osl_spin_unlock(lock, flags)

typedef enum dhd_locker_state {
	LOCKER_IS_FREE,
	LOCKER_IS_BUSY,
	LOCKER_IS_RSVD
} dhd_locker_state_t;

/* Enum for marking the buffer color based on usage */
typedef enum dhd_pkttype {
	PKTTYPE_DATA_TX = 0,
	PKTTYPE_DATA_RX,
	PKTTYPE_IOCTL_RX,
	PKTTYPE_EVENT_RX,
	PKTTYPE_INFO_RX,
	/* dhd_prot_pkt_free no check, if pktid reserved and no space avail case */
	PKTTYPE_NO_CHECK,
	PKTTYPE_TSBUF_RX
} dhd_pkttype_t;

/* Packet metadata saved in packet id mapper */

typedef struct dhd_pktid_item {
	dhd_locker_state_t state;  /* tag a locker to be free, busy or reserved */
	uint8       dir;      /* dma map direction (Tx=flush or Rx=invalidate) */
	dhd_pkttype_t pkttype; /* pktlists are maintained based on pkttype */
	uint16      len;      /* length of mapped packet's buffer */
	void        *pkt;     /* opaque native pointer to a packet */
	dmaaddr_t   pa;       /* physical address of mapped packet's buffer */
	void        *dmah;    /* handle to OS specific DMA map */
	void		*secdma;
} dhd_pktid_item_t;

typedef uint32 dhd_pktid_key_t;

typedef struct dhd_pktid_map {
	uint32      items;    /* total items in map */
	uint32      avail;    /* total available items */
	int         failures; /* lockers unavailable count */
	/* Spinlock to protect dhd_pktid_map in process/tasklet context */
	void        *pktid_lock; /* Used when USE_DHD_PKTID_LOCK is defined */

#if defined(DHD_PKTID_AUDIT_ENABLED)
	void		*pktid_audit_lock;
	struct bcm_mwbmap *pktid_audit; /* multi word bitmap based audit */
#endif /* DHD_PKTID_AUDIT_ENABLED */
	dhd_pktid_key_t	*keys; /* map_items +1 unique pkt ids */
	dhd_pktid_item_t lockers[0];           /* metadata storage */
} dhd_pktid_map_t;

typedef void *dhd_pktid_map_handle_t; /* opaque handle to a pktid map */

static void *
BCMFASTPATH(dhd_pktid_map_get)(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle, uint32 nkey)
{
	dhd_pktid_map_t *map;
	dhd_pktid_item_t *locker;
	void *pkt = NULL;
	unsigned long flags;

	if (!handle || !dhd) {
		return NULL;
	}
	map = (dhd_pktid_map_t *)handle;

	DHD_PKTID_LOCK(map->pktid_lock, flags);

	/* XXX PLEASE DO NOT remove this ASSERT, fix the bug in caller. */
	if ((nkey == 0) || (nkey > map->items) ||
			(dhd->dhd_induce_error == DHD_INDUCE_PKTID_INVALID_FREE)) {
		DHD_PKTID_UNLOCK(map->pktid_lock, flags);
		return NULL;
	}

	locker = &map->lockers[nkey];
	pkt = locker->pkt;
	DHD_PKTID_UNLOCK(map->pktid_lock, flags);
	return pkt;
}

static int
BCMFASTPATH(dhd_pktid_map_get_avail)(dhd_pub_t *dhd, dhd_pktid_map_handle_t *handle)
{
	dhd_pktid_map_t *map;

	if (!handle || !dhd) {
		return 0;
	}
	map = (dhd_pktid_map_t *)handle;
	return map->avail;
}

#ifdef AGG_H2D_DB
typedef struct _agg_h2d_db_info {
	void *dhd;
	struct hrtimer timer;
	bool init;
	uint32 direct_db_cnt;
	uint32 timer_db_cnt;
	uint64  *inflight_histo;
} agg_h2d_db_info_t;
#endif /* AGG_H2D_DB */

/* Used in loopback tests */
typedef struct dhd_dmaxfer {
	dhd_dma_buf_t srcmem;
	dhd_dma_buf_t dstmem;
	uint32        srcdelay;
	uint32        destdelay;
	uint32        len;
	bool          in_progress;
	uint64        start_usec;
	uint64        time_taken;
	uint32        d11_lpbk;
	int           status;
} dhd_dmaxfer_t;

typedef struct msgbuf_ring {
	bool           inited;
	uint16         idx;       /* ring id */
	uint16         rd;        /* read index */
	uint16         curr_rd;   /* read index for debug */
	uint16         wr;        /* write index */
	uint16         max_items; /* maximum number of items in ring */
	uint16         item_len;  /* length of each item in the ring */
	sh_addr_t      base_addr; /* LITTLE ENDIAN formatted: base address */
	dhd_dma_buf_t  dma_buf;   /* DMA-able buffer: pa, va, len, dmah, secdma */
	uint32         seqnum;    /* next expected item's sequence number */
#ifdef TXP_FLUSH_NITEMS
	void           *start_addr;
	/* # of messages on ring not yet announced to dongle */
	uint16         pend_items_count;
#ifdef AGG_H2D_DB
	osl_atomic_t	inflight;
#endif /* AGG_H2D_DB */
#endif /* TXP_FLUSH_NITEMS */
	uint8           ring_type;
	uint8           n_completion_ids;
	bool            create_pending;
	uint16          create_req_id;
	uint8           current_phase;
	uint16	        compeltion_ring_ids[MAX_COMPLETION_RING_IDS_ASSOCIATED];
	uchar		name[RING_NAME_MAX_LENGTH];
	uint32		ring_mem_allocated;
	void	        *ring_lock;
	struct msgbuf_ring *linked_ring; /* Ring Associated to metadata ring */
} msgbuf_ring_t;

#ifdef EWP_EDL
typedef int (* d2h_edl_sync_cb_t)(dhd_pub_t *dhd, struct msgbuf_ring *ring,
				volatile cmn_msg_hdr_t *msg);
#endif /* EWP_EDL */

typedef uint8 (* d2h_sync_cb_t)(dhd_pub_t *dhd, struct msgbuf_ring *ring,
				volatile cmn_msg_hdr_t *msg, int msglen);

typedef struct dhd_prot {
	osl_t *osh;		/* OSL handle */
	uint16 rxbufpost_sz;		/* Size of rx buffer posted to dongle */
	uint16 rxbufpost_alloc_sz;	/* Actual rx buffer packet allocated in the host */
	uint16 rxbufpost;
	uint16 rx_buf_burst;
	uint16 rx_bufpost_threshold;
	uint16 max_rxbufpost;
	uint32 tot_rxbufpost;
	uint32 tot_rxcpl;
	uint16 max_eventbufpost;
	uint16 max_ioctlrespbufpost;
	uint16 max_tsbufpost;
	uint16 max_infobufpost;
	uint16 infobufpost;
	uint16 cur_event_bufs_posted;
	uint16 cur_ioctlresp_bufs_posted;
	uint16 cur_ts_bufs_posted;

	/* Flow control mechanism based on active transmits pending */
	osl_atomic_t active_tx_count; /* increments/decrements on every packet tx/tx_status */
	uint16 h2d_max_txpost;
	uint16 h2d_htput_max_txpost;
	uint16 txp_threshold;  /* optimization to write "n" tx items at a time to ring */

	/* MsgBuf Ring info: has a dhd_dma_buf that is dynamically allocated */
	msgbuf_ring_t h2dring_ctrl_subn; /* H2D ctrl message submission ring */
	msgbuf_ring_t h2dring_rxp_subn; /* H2D RxBuf post ring */
	msgbuf_ring_t d2hring_ctrl_cpln; /* D2H ctrl completion ring */
	msgbuf_ring_t d2hring_tx_cpln; /* D2H Tx complete message ring */
	msgbuf_ring_t d2hring_rx_cpln; /* D2H Rx complete message ring */
	msgbuf_ring_t *h2dring_info_subn; /* H2D info submission ring */
	msgbuf_ring_t *d2hring_info_cpln; /* D2H info completion ring */
	msgbuf_ring_t *d2hring_edl; /* D2H Enhanced Debug Lane (EDL) ring */

	msgbuf_ring_t *h2d_flowrings_pool; /* Pool of preallocated flowings */
	dhd_dma_buf_t flowrings_dma_buf; /* Contiguous DMA buffer for flowrings */
	uint16        h2d_rings_total; /* total H2D (common rings + flowrings) */

	uint32		rx_dataoffset;

	dhd_mb_ring_t	mb_ring_fn;	/* called when dongle needs to be notified of new msg */
	dhd_mb_ring_2_t	mb_2_ring_fn;	/* called when dongle needs to be notified of new msg */

	/* ioctl related resources */
	uint8 ioctl_state;
	int16 ioctl_status;		/* status returned from dongle */
	uint16 ioctl_resplen;
	dhd_ioctl_recieved_status_t ioctl_received;
	uint curr_ioctl_cmd;
	dhd_dma_buf_t	retbuf;		/* For holding ioctl response */
	dhd_dma_buf_t	ioctbuf;	/* For holding ioctl request */

	dhd_dma_buf_t	d2h_dma_scratch_buf;	/* For holding d2h scratch */

	/* DMA-able arrays for holding WR and RD indices */
	uint32          rw_index_sz; /* Size of a RD or WR index in dongle */
	dhd_dma_buf_t   h2d_dma_indx_wr_buf;	/* Array of H2D WR indices */
	dhd_dma_buf_t	h2d_dma_indx_rd_buf;	/* Array of H2D RD indices */
	dhd_dma_buf_t	d2h_dma_indx_wr_buf;	/* Array of D2H WR indices */
	dhd_dma_buf_t	d2h_dma_indx_rd_buf;	/* Array of D2H RD indices */
	dhd_dma_buf_t h2d_ifrm_indx_wr_buf;	/* Array of H2D WR indices for ifrm */

	dhd_dma_buf_t	host_bus_throughput_buf; /* bus throughput measure buffer */

	dhd_dma_buf_t   *flowring_buf;    /* pool of flow ring buf */
#ifdef DHD_DMA_INDICES_SEQNUM
	char *h2d_dma_indx_rd_copy_buf; /* Local copy of H2D WR indices array */
	char *d2h_dma_indx_wr_copy_buf; /* Local copy of D2H WR indices array */
	uint32 h2d_dma_indx_rd_copy_bufsz; /* H2D WR indices array size */
	uint32 d2h_dma_indx_wr_copy_bufsz; /* D2H WR indices array size */
	uint32 host_seqnum;	/* Seqence number for D2H DMA Indices sync */
#endif /* DHD_DMA_INDICES_SEQNUM */
	uint32			flowring_num;

	d2h_sync_cb_t d2h_sync_cb; /* Sync on D2H DMA done: SEQNUM or XORCSUM */
#ifdef EWP_EDL
	d2h_edl_sync_cb_t d2h_edl_sync_cb; /* Sync on EDL D2H DMA done: SEQNUM or XORCSUM */
#endif /* EWP_EDL */
	ulong d2h_sync_wait_max; /* max number of wait loops to receive one msg */
	ulong d2h_sync_wait_tot; /* total wait loops */

	dhd_dmaxfer_t	dmaxfer; /* for test/DMA loopback */

	uint16		ioctl_seq_no;
	uint16		data_seq_no;  /* XXX this field is obsolete */
	uint16		ioctl_trans_id;
	void		*pktid_ctrl_map; /* a pktid maps to a packet and its metadata */
	void		*pktid_rx_map;	/* pktid map for rx path */
	void		*pktid_tx_map;	/* pktid map for tx path */
	bool		metadata_dbg;
	void		*pktid_map_handle_ioctl;
#ifdef DHD_MAP_PKTID_LOGGING
	void		*pktid_dma_map;	/* pktid map for DMA MAP */
	void		*pktid_dma_unmap; /* pktid map for DMA UNMAP */
#endif /* DHD_MAP_PKTID_LOGGING */
	uint32		pktid_depleted_cnt;	/* pktid depleted count */
	/* netif tx queue stop count */
	uint8		pktid_txq_stop_cnt;
	/* netif tx queue start count */
	uint8		pktid_txq_start_cnt;
	uint64		ioctl_fillup_time;	/* timestamp for ioctl fillup */
	uint64		ioctl_ack_time;		/* timestamp for ioctl ack */
	uint64		ioctl_cmplt_time;	/* timestamp for ioctl completion */

	/* Applications/utilities can read tx and rx metadata using IOVARs */
	uint16		rx_metadata_offset;
	uint16		tx_metadata_offset;

#if (defined(BCM_ROUTER_DHD) && defined(HNDCTF))
	rxchain_info_t	rxchain;	/* chain of rx packets */
#endif

#if defined(DHD_D2H_SOFT_DOORBELL_SUPPORT)
	/* Host's soft doorbell configuration */
	bcmpcie_soft_doorbell_t soft_doorbell[BCMPCIE_D2H_COMMON_MSGRINGS];
#endif /* DHD_D2H_SOFT_DOORBELL_SUPPORT */

	/* Work Queues to be used by the producer and the consumer, and threshold
	 * when the WRITE index must be synced to consumer's workq
	 */
	dhd_dma_buf_t	fw_trap_buf; /* firmware trap buffer */
#ifdef FLOW_RING_PREALLOC
	/* pre-allocation htput ring buffer */
	dhd_dma_buf_t	htput_ring_buf[HTPUT_TOTAL_FLOW_RINGS];
	/* pre-allocation folw ring(non htput rings) */
	dhd_dma_buf_t	flow_ring_buf[MAX_FLOW_RINGS];
#endif /* FLOW_RING_PREALLOC */
	uint32  host_ipc_version; /* Host sypported IPC rev */
	uint32  device_ipc_version; /* FW supported IPC rev */
	uint32  active_ipc_version; /* Host advertised IPC rev */
	dhd_dma_buf_t   hostts_req_buf; /* For holding host timestamp request buf */
	bool    hostts_req_buf_inuse;
	bool    rx_ts_log_enabled;
	bool    tx_ts_log_enabled;
#ifdef BTLOG
	msgbuf_ring_t *h2dring_btlog_subn; /* H2D btlog submission ring */
	msgbuf_ring_t *d2hring_btlog_cpln; /* D2H btlog completion ring */
	uint16 btlogbufpost;
	uint16 max_btlogbufpost;
#endif	/* BTLOG */
#ifdef DHD_HMAPTEST
	uint32 hmaptest_rx_active;
	uint32 hmaptest_rx_pktid;
	char *hmap_rx_buf_va;
	dmaaddr_t hmap_rx_buf_pa;
	uint32 hmap_rx_buf_len;

	uint32 hmaptest_tx_active;
	uint32 hmaptest_tx_pktid;
	char *hmap_tx_buf_va;
	dmaaddr_t hmap_tx_buf_pa;
	uint32	  hmap_tx_buf_len;
	dhd_hmaptest_t	hmaptest; /* for hmaptest */
	bool hmap_enabled; /* TRUE = hmap is enabled */
#endif /* DHD_HMAPTEST */
#ifdef SNAPSHOT_UPLOAD
	dhd_dma_buf_t snapshot_upload_buf;	/* snapshot upload buffer */
	uint32 snapshot_upload_len;		/* snapshot uploaded len */
	uint8 snapshot_type;			/* snaphot uploaded type */
	bool snapshot_cmpl_pending;		/* snapshot completion pending */
#endif	/* SNAPSHOT_UPLOAD */
	bool no_retry;
	bool no_aggr;
	bool fixed_rate;
	dhd_dma_buf_t	host_scb_buf; /* scb host offload buffer */
	bool no_tx_resource;
	uint32 txcpl_db_cnt;
#ifdef AGG_H2D_DB
	agg_h2d_db_info_t agg_h2d_db_info;
#endif /* AGG_H2D_DB */
	uint64 tx_h2d_db_cnt;
#ifdef DHD_DEBUG_INVALID_PKTID
	uint8 ctrl_cpl_snapshot[D2HRING_CTRL_CMPLT_ITEMSIZE];
#endif /* DHD_DEBUG_INVALID_PKTID */
	uint32 event_wakeup_pkt; /* Number of event wakeup packet rcvd */
	uint32 rx_wakeup_pkt;    /* Number of Rx wakeup packet rcvd */
	uint32 info_wakeup_pkt;  /* Number of info cpl wakeup packet rcvd */
	msgbuf_ring_t *d2hring_md_cpl; /* D2H metadata completion ring */
	/* no. which controls how many rx cpl/post items are processed per dpc */
	uint32 rx_cpl_post_bound;
	/*
	 * no. which controls how many tx post items are processed per dpc,
	 * i.e. how many tx pkts are posted to flowring from the bkp queue
	 * from dpc context
	 */
	uint32 tx_post_bound;
	/* no. which controls how many tx cpl items are processed per dpc */
	uint32 tx_cpl_bound;
	/* no. which controls how many ctrl cpl/post items are processed per dpc */
	uint32 ctrl_cpl_post_bound;
} dhd_prot_t;

static uint16
dhd_prot_dma_indx_get(dhd_pub_t *dhd, uint8 type, uint16 ringid)
{
	uint8 *ptr;
	uint16 data = 0;
	uint16 offset;
	dhd_prot_t *prot = dhd->prot;
	uint16 max_h2d_rings = dhd->bus->max_submission_rings;

	/* Ensure that instruction operates on workitem are
	 * completed before dma indices are read.
	 */
	OSL_MB();

	switch (type) {
	case H2D_DMA_INDX_WR_UPD:
		ptr = (uint8 *)(prot->h2d_dma_indx_wr_buf.va);
		offset = DHD_H2D_RING_OFFSET(ringid);
		break;
	case H2D_DMA_INDX_RD_UPD:
#ifdef DHD_DMA_INDICES_SEQNUM
		if (prot->h2d_dma_indx_rd_copy_buf) {
			ptr = (uint8 *)(prot->h2d_dma_indx_rd_copy_buf);
		} else
#endif /* DHD_DMA_INDICES_SEQNUM */
		{
			ptr = (uint8 *)(prot->h2d_dma_indx_rd_buf.va);
		}
		offset = DHD_H2D_RING_OFFSET(ringid);
		break;
	case D2H_DMA_INDX_WR_UPD:
#ifdef DHD_DMA_INDICES_SEQNUM
		if (prot->d2h_dma_indx_wr_copy_buf) {
			ptr = (uint8 *)(prot->d2h_dma_indx_wr_copy_buf);
		} else
#endif /* DHD_DMA_INDICES_SEQNUM */
		{
			ptr = (uint8 *)(prot->d2h_dma_indx_wr_buf.va);
		}
		offset = DHD_D2H_RING_OFFSET(ringid, max_h2d_rings);
		break;
	case D2H_DMA_INDX_RD_UPD:
		ptr = (uint8 *)(prot->d2h_dma_indx_rd_buf.va);
		offset = DHD_D2H_RING_OFFSET(ringid, max_h2d_rings);
		break;
	default:
		noa_wlan_err("%s: Invalid option for DMAing read/write index\n",
			__FUNCTION__);
		return 0;
	}

	ASSERT(prot->rw_index_sz != 0);
	ptr += offset * prot->rw_index_sz;

	OSL_CACHE_INV((void *)ptr, prot->rw_index_sz);
	/* XXX: Test casting ptr to uint16* for 32bit indices case on Big Endian */
	data = LTOH16(*((uint16 *)ptr));
	OSL_MB();
	return data;
} /* dhd_prot_dma_indx_get */

#define DHD_RING_MEM_MEMBER_ADDR(bus, ringid, member) \
	((bus)->ring_sh[ringid].ring_mem_addr + OFFSETOF(ring_mem_t, member))

#define DHD_RING_INFO_MEMBER_ADDR(bus, member) \
	((bus)->pcie_sh->rings_info_ptr + OFFSETOF(ring_info_t, member))

extern ulong dhdpcie_bus_chkandshift_bpoffset(dhd_bus_t *bus, ulong offset);
#endif