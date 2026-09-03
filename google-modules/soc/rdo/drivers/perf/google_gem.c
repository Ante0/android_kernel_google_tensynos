// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */

#include <linux/debugfs.h>
#include <linux/dma-map-ops.h>
#include <linux/dmapool.h>
#include <linux/idr.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include "google_gem.h"
#include "google_gem_utils.h"

#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_mba_nq_xport.h>

#define COUNTER_SIZE sizeof(uint64_t)

#define COUNTER_FMT "counter-%d"
#define EVENT_GRP_FMT "event_group%d"
#define NAMEBUF_SZ 60

#define DEFAULT_SCAN_PERIOD_MS 1

#define EVENT_ID_FIELD_MASK    GENMASK(23, 0)
#define EVENT_TYPE_FIELD_MASK  GENMASK(31, 24)

#define BUILD_FIELD(_field, _value) \
	FIELD_PREP(GENMASK(_field##_OFFSET + _field##_LENGTH - 1, _field##_OFFSET), _value)

/* Maintains virtual address and DMA address to a single counter buffer. */
struct cntbuf {
	u64 *va;
	phys_addr_t pa;
};

struct event_filter_config {
	struct list_head list_node;
	u8 ip;
	u8 event_id;
	u8 filter_id;
	u64 addr;
	u64 mask;
};

struct gem_drvdata {
	struct device *dev;
	struct dentry *debugfs_dir;
	struct gdmc_iface *gdmc_iface;
	struct list_head eventgrp_list;

	const struct eventgrp_desc *eventgrp_desc;

	struct dma_pool *cntbuf_pool;

	spinlock_t efcfg_lock; /* efcfg_list */
	struct list_head efcfg_list;
};

#define GDMC_MSG_SIZE 4 * sizeof(u32)

struct gdmc_msg {
	u32 header;
	union {
		struct gem_msg_ip_ctrl ip_ctrl;
		struct gem_msg_event_ctrl event_ctrl;
		struct gem_msg_mode_ctrl mode_ctrl;
		struct gem_msg_ip_list ip_list;
		struct gem_msg_filter_ctrl filter_ctrl;
		struct gem_msg_trace_ctrl trace_ctrl;
	};
};
static_assert(sizeof(struct gdmc_msg) <= GDMC_MSG_SIZE,
	      "size of a GEM msg exceeded gdmc_msg.payload");

static const struct event_entry std_hw_events[] = {
	{ 0, "read-req-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 1, "write-req-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 2, "write-data-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 3, "read-data-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 4, "write-response-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 5, "read-req-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 6, "write-req-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 7, "write-data-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 8, "read-data-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 9, "write-response-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 10, "curr-read-outstanding", GEM_EVENT_TYPE_OUTSTANDING },
	{ 11, "curr-write-outstanding", GEM_EVENT_TYPE_OUTSTANDING },
	{ 12, "total-read-latency", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 13, "total-write-latency", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 14, "read-outstanding-cycle-count", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 15, "write-outstanding-cycle-count", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 16, "total-bytes-read", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 17, "total-bytes-written", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 18, "total-read-burst-length", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 19, "total-write-burst-length", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 20, "max-read-latency", GEM_EVENT_TYPE_HISTORICAL_HIGH },
	{ 21, "max-write-latency", GEM_EVENT_TYPE_HISTORICAL_HIGH },
	{ 22, "read-latency-samples", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 23, "write-latency-samples", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 24, "max-read-stall-cycles", GEM_EVENT_TYPE_HISTORICAL_HIGH },
	{ 25, "max-write-stall-cycles", GEM_EVENT_TYPE_HISTORICAL_HIGH },
	{ 26, "core-clock-cycles", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 27, "total-write-req-bytes", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 0, NULL },
};

static const struct filter_entry std_hw_filters[] = {
	{ 0, "axaddr" },
	{ 1, "axid" },
	{ 2, "axlen" },
	{ 3, "axsize" },
	{ 4, "axcache" },
	{ 5, "axprot" },
	{ 6, "axqos" },
	{ 7, "axuser" },
	{ 8, "axburst" },
	{ 9, "avalid" },
	{ 10, "strm" },
	{ 11, "xresp" },
	{ 0, NULL },
};

static const struct event_entry dvfs_noc_hw_events[] = {
	{ 0, "read-req-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 1, "write-req-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 2, "write-data-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 3, "read-data-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 4, "write-response-transfers", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 5, "read-req-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 6, "write-req-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 7, "write-data-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 8, "read-data-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 9, "write-response-blocked", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 10, "curr-read-outstanding", GEM_EVENT_TYPE_OUTSTANDING },
	{ 11, "curr-write-outstanding", GEM_EVENT_TYPE_OUTSTANDING },
	{ 12, "total-read-latency", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 13, "total-write-latency", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 14, "read-outstanding-cycle-count", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 15, "write-outstanding-cycle-count", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 16, "total-bytes-read", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 17, "total-bytes-written", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 18, "total-read-burst-length", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 19, "total-write-burst-length", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 26, "core-clock-cycles", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 0, NULL },
};

static const struct event_entry gmc_generic_hw_events[] = {
	{ 0, "CMD_RD" },
	{ 1, "CMD_RDAP" },
	{ 2, "CMD_RD32" },
	{ 3, "CMD_RDAP32" },
	{ 4, "CMD_WR" },
	{ 5, "CMD_WRAP" },
	{ 6, "CMD_WR32" },
	{ 7, "CMD_WRAP32" },
	{ 8, "CMD_MWR" },
	{ 9, "CMD_MWRAP" },
	{ 10, "CMD_MWR32" },
	{ 11, "CMD_MWRAP32" },
	{ 12, "CMD_ACT" },
	{ 13, "CMD_PREpb" },
	{ 14, "CMD_PREab" },
	{ 15, "CMD_REFpb" },
	{ 16, "CMD_REFab" },
	{ 17, "CMD_PDE" },
	{ 18, "CMD_PDX" },
	{ 19, "CMD_SRE" },
	{ 20, "CMD_SRX" },
	{ 21, "CMD_CASwsfs" },
	{ 22, "CMD_CASwsrd" },
	{ 23, "CMD_CASwswr" },
	{ 24, "CMD_CASwsrdb3" },
	{ 25, "CMD_CASwswrwrx" },
	{ 26, "CMD_CASwswrdc" },
	{ 27, "CMD_RFF" },
	{ 28, "CMD_RDC" },
	{ 29, "CMD_WFF" },
	{ 30, "CMD_MRR" },
	{ 31, "CMD_MRW" },
	{ 32, "KB_count" },
	{ 33, "rd" },
	{ 34, "wr" },
	{ 35, "partial_rd" },
	{ 36, "mwr" },
	{ 37, "wrx" },
	{ 38, "page_miss" },
	{ 39, "page_hit" },
	{ 40, "page_conflict" },
	{ 41, "rd2wr_switch" },
	{ 42, "wr2rd_switch" },
	{ 43, "col_winner" },
	{ 44, "hz_detection" },
	{ 45, "hz_prom" },
	{ 46, "atid_prom" },
	{ 47, "ipct_expired" },
	{ 48, "cyc_rank_0_PD" },
	{ 49, "cyc_rank_1_PD" },
	{ 50, "cyc_CSPD" },
	{ 51, "cyc_rank_0_bank_0_open" },
	{ 52, "cyc_rank_0_bank_1_open" },
	{ 53, "cyc_rank_0_bank_2_open" },
	{ 54, "cyc_rank_0_bank_3_open" },
	{ 55, "cyc_rank_0_bank_4_open" },
	{ 56, "cyc_rank_0_bank_5_open" },
	{ 57, "cyc_rank_0_bank_6_open" },
	{ 58, "cyc_rank_0_bank_7_open" },
	{ 59, "cyc_rank_0_bank_8_open" },
	{ 60, "cyc_rank_0_bank_9_open" },
	{ 61, "cyc_rank_0_bank_10_open" },
	{ 62, "cyc_rank_0_bank_11_open" },
	{ 63, "cyc_rank_0_bank_12_open" },
	{ 64, "cyc_rank_0_bank_13_open" },
	{ 65, "cyc_rank_0_bank_14_open" },
	{ 66, "cyc_rank_0_bank_15_open" },
	{ 67, "cyc_rank_1_bank_0_open" },
	{ 68, "cyc_rank_1_bank_1_open" },
	{ 69, "cyc_rank_1_bank_2_open" },
	{ 70, "cyc_rank_1_bank_3_open" },
	{ 71, "cyc_rank_1_bank_4_open" },
	{ 72, "cyc_rank_1_bank_5_open" },
	{ 73, "cyc_rank_1_bank_6_open" },
	{ 74, "cyc_rank_1_bank_7_open" },
	{ 75, "cyc_rank_1_bank_8_open" },
	{ 76, "cyc_rank_1_bank_9_open" },
	{ 77, "cyc_rank_1_bank_10_open" },
	{ 78, "cyc_rank_1_bank_11_open" },
	{ 79, "cyc_rank_1_bank_12_open" },
	{ 80, "cyc_rank_1_bank_13_open" },
	{ 81, "cyc_rank_1_bank_14_open" },
	{ 82, "cyc_rank_1_bank_15_open" },
	{ 83, "idle_cycle" },
	{ 84, "busy_cycle" },
	{ 85, "overhead_cycle" },
	{ 86, "data_cycle" },
	{ 87, "Qurgency_vc_0" },
	{ 88, "Qurgency_vc_1" },
	{ 89, "Qurgency_vc_2" },
	{ 90, "Qurgency_vc_3" },
	{ 91, "Qurgency_vc_4" },
	{ 92, "timeout_vc_0" },
	{ 93, "timeout_vc_1" },
	{ 94, "timeout_vc_2" },
	{ 95, "timeout_vc_3" },
	{ 96, "timeout_vc_4" },
	{ 97, "CMD_RFMpb" },
	{ 98, "CMD_RFMab" },
	{ 99, "cyc_rank_0_SR" },
	{ 100, "cyc_rank_1_SR" },
	{ 101, "cyc_rank_0_SRPD" },
	{ 102, "cyc_rank_1_SRPD" },
	{ 103, "cyc_CSSRPD" },
	{ 104, "cyc_PHY_LP_data" },
	{ 105, "cyc_PHY_LP_ctrl" },
	{ 106, "write_timeout" },
	{ 107, "occurrence_ref_alert_lvl_3_bo" },
	{ 108, "cyc_ref_alert_lvl_3_bo" },
	{ 109, "occurrence_dmnt_alert_lvl_2_bo" },
	{ 110, "cyc_dmnt_alert_lvl_2_bo" },
	{ 111, "occurrence_ctrlupd_bo" },
	{ 112, "cyc_ctrlupd_bo" },
	{ 113, "occurrence_phymstr_bo" },
	{ 114, "cyc_phymstr_bo" },
	{ 115, "occurrence_lp2_exit" },
	{ 116, "cyc_lp2_exit" },
	{ 117, "occurrence_sw_seq_block" },
	{ 118, "cyc_sw_seq_block" },
	{ 119, "same_rank_r2w" },
	{ 120, "same_rank_w2r" },
	{ 121, "diff_rank_r2w" },
	{ 122, "diff_rank_w2r" },
	{ 123, "diff_rank_r2r" },
	{ 124, "diff_rank_w2w" },
	{ 0, NULL },
};

static const struct filter_entry gmc_generic_hw_filters[] = {
	{ 0, "bank" },
	{ 1, "rank" },
	{ 2, "rw" },
	{ 3, "proftag" },
	{ 4, "vc-id" },
	{ 5, "bl" },
	{ 0, NULL },
};

static const struct event_entry gmc_dfi_hw_events[] = {
	{ 0, "RD" },
	{ 1, "WR" },
	{ 2, "dfi_wrdata_en" },
	{ 3, "dfi_rddata_valid" },
	{ 0, NULL },
};

static const struct event_entry gmc_dfi_hw_events_v2[] = {
	{ 0, "RD" },
	{ 1, "WR" },
	{ 2, "dfi_rddata_valid" },
	{ 3, "dfi_wrdata_en" },
	{ 0, NULL },
};

static const struct filter_entry gmc_dfi_hw_filters[] = {
	{ 0, "bank" },
	{ 1, "rank" },
	{ 2, "rw" },
	{ 3, "proftag" },
	{ 4, "vc-id" },
	{ 5, "bl" },
	{ 0, NULL },
};

static const struct event_entry gslc_core_hw_events[] = {
	{ 0, "RQB_ENTRY_VALID" },
	{ 1, "NDR_REQ" },
	{ 2, "SPEC_RD_VALID" },
	{ 3, "SPEC_RD_DROPPED" },
	{ 4, "DEPENDENCY_DETECTED" },
	{ 5, "SILENT_REPLACEMENT" },
	{ 6, "MEMATTR_CHANGED_TO_ALLOC_CSR" },
	{ 7, "MEMATTR_CHANGED_TO_NOALLOC_FCC" },
	{ 8, "MEMATTR_CHANGED_TO_NOALLOC_CSR" },
	{ 9, "MEMATTR_CHANGED_TO_NOALLOC_PARTIAL_WR_CSR" },
	{ 10, "HIT_IN_ANY_WAY" },
	{ 11, "HIT_IN_UNASSIGNED_WAY" },
	{ 12, "HIT_IN_DISABLED_WAY" },
	{ 13, "HIT_IN_SECONDARY_WAY" },
	{ 14, "HIT_CAUSING_INVALIDATION" },
	{ 15, "MISS_CAUSING_SELF_EVICTION" },
	{ 16, "MISS_CAUSING_OTHER_EVICTION" },
	{ 17, "MEMSCHED_STALL" },
	{ 18, "RMW_from_MEM_to_CACHE" },
	{ 19, "MEMSCHED_STALL_FOR_EVICT_DEP" },
	{ 20, "RMW_from_CACHE_TO_CACHE" },
	{ 21, "RMW_from_MEM_TO_MEM" },
	{ 22, "DATARAM_SEC_EVENT" },
	{ 23, "HWPA_CYC" },
	{ 24, "HWPA_TRIG_EVENT" },
	{ 25, "SWPA_TRIG_EVENT" },
	{ 26, "SWPA_CYC" },
	{ 27, "CMO_CLEAN_EVENT" },
	{ 28, "CMO_CLEANINV_EVENT" },
	{ 29, "CMO_INV_EVENT" },
	{ 30, "CMO_CLEAN_INV_ZERO_EVENT" },
	{ 31, "QACTIVE_CYC" },
	{ 32, "RQB_RDFILL_LEVEL_00_AND_SlcNR_d4" },
	{ 33, "RQB_RDFILL_LEVEL_SlcNR_d4_a1_AND_SlcNR_d2" },
	{ 34, "RQB_RDFILL_LEVEL_SlcNR_d2_a1_AND_3SlcNR_d4" },
	{ 35, "RBQ_RDFILL_LEVEL_3SlcNR_d4_a1_AND_SlcNR" },
	{ 36, "RQB_WRFILL_LEVEL_00_AND_SlcNR_d4" },
	{ 37, "RQB_WRFILL_LEVEL_SlcNR_d4_a1_AND_SlcNR_d2" },
	{ 38, "RQB_WRFILL_LEVEL_SlcNR_d2_a1_AND_3SlcNR_d4" },
	{ 39, "RBQ_WRFILL_LEVEL_3SlcNR_d4_a1_AND_SlcNR" },
	{ 40, "RQB_TOTALFILL_LEVEL_00_AND_SlcNR_d4" },
	{ 41, "RQB_TOTALFILL_LEVEL_SlcNR_d4_a1_AND_SlcNR_d2" },
	{ 42, "RQB_TOTALFILL_LEVEL_SlcNR_d2_a1_AND_3SlcNR_d4" },
	{ 43, "RBQ_TOTALFILL_LEVEL_3SlcNR_d4_a1_AND_SlcNR" },
	{ 44, "RQB_FULL" },
	{ 45, "RQB_EMPTY" },
	{ 46, "RQB_STALL_BE_LINK_DC_CYC" },
	{ 47, "VC0_RD_BE_CREDIT_ZERO" },
	{ 48, "VC1_RD_BE_CREDIT_ZERO" },
	{ 49, "VC2_RD_BE_CREDIT_ZERO" },
	{ 50, "VC3_RD_BE_CREDIT_ZERO" },
	{ 51, "VC4_RD_BE_CREDIT_ZERO" },
	{ 52, "VC5_RD_BE_CREDIT_ZERO" },
	{ 53, "VC0_WR_BE_CREDIT_ZERO" },
	{ 54, "VC1_WR_BE_CREDIT_ZERO" },
	{ 55, "VC2_WR_BE_CREDIT_ZERO" },
	{ 56, "VC3_WR_BE_CREDIT_ZERO" },
	{ 57, "VC4_WR_BE_CREDIT_ZERO" },
	{ 58, "VC5_WR_BE_CREDIT_ZERO" },
	{ 59, "RD_URGENCYLVL_00_CYC_VC0" },
	{ 60, "RD_URGENCYLVL_00_CYC_VC1" },
	{ 61, "RD_URGENCYLVL_00_CYC_VC2" },
	{ 62, "RD_URGENCYLVL_00_CYC_VC3" },
	{ 63, "RD_URGENCYLVL_00_CYC_VC4" },
	{ 64, "RD_URGENCYLVL_00_CYC_VC5" },
	{ 65, "RD_URGENCYLVL_01_CYC_VC0" },
	{ 66, "RD_URGENCYLVL_01_CYC_VC1" },
	{ 67, "RD_URGENCYLVL_01_CYC_VC2" },
	{ 68, "RD_URGENCYLVL_01_CYC_VC3" },
	{ 69, "RD_URGENCYLVL_01_CYC_VC4" },
	{ 70, "RD_URGENCYLVL_01_CYC_VC5" },
	{ 71, "RD_URGENCYLVL_02_CYC_VC0" },
	{ 72, "RD_URGENCYLVL_02_CYC_VC1" },
	{ 73, "RD_URGENCYLVL_02_CYC_VC2" },
	{ 74, "RD_URGENCYLVL_02_CYC_VC3" },
	{ 75, "RD_URGENCYLVL_02_CYC_VC4" },
	{ 76, "RD_URGENCYLVL_02_CYC_VC5" },
	{ 77, "RD_URGENCYLVL_03_CYC_VC0" },
	{ 78, "RD_URGENCYLVL_03_CYC_VC1" },
	{ 79, "RD_URGENCYLVL_03_CYC_VC2" },
	{ 80, "RD_URGENCYLVL_03_CYC_VC3" },
	{ 81, "RD_URGENCYLVL_03_CYC_VC4" },
	{ 82, "RD_URGENCYLVL_03_CYC_VC5" },
	{ 83, "WR_URGENCYLVL_00_CYC_VC0" },
	{ 84, "WR_URGENCYLVL_00_CYC_VC1" },
	{ 85, "WR_URGENCYLVL_00_CYC_VC2" },
	{ 86, "WR_URGENCYLVL_00_CYC_VC3" },
	{ 87, "WR_URGENCYLVL_00_CYC_VC4" },
	{ 88, "WR_URGENCYLVL_00_CYC_VC5" },
	{ 89, "WR_URGENCYLVL_01_CYC_VC0" },
	{ 90, "WR_URGENCYLVL_01_CYC_VC1" },
	{ 91, "WR_URGENCYLVL_01_CYC_VC2" },
	{ 92, "WR_URGENCYLVL_01_CYC_VC3" },
	{ 93, "WR_URGENCYLVL_01_CYC_VC4" },
	{ 94, "WR_URGENCYLVL_01_CYC_VC5" },
	{ 95, "WR_URGENCYLVL_02_CYC_VC0" },
	{ 96, "WR_URGENCYLVL_02_CYC_VC1" },
	{ 97, "WR_URGENCYLVL_02_CYC_VC2" },
	{ 98, "WR_URGENCYLVL_02_CYC_VC3" },
	{ 99, "WR_URGENCYLVL_02_CYC_VC4" },
	{ 100, "WR_URGENCYLVL_02_CYC_VC5" },
	{ 101, "WR_URGENCYLVL_03_CYC_VC0" },
	{ 102, "WR_URGENCYLVL_03_CYC_VC1" },
	{ 103, "WR_URGENCYLVL_03_CYC_VC2" },
	{ 104, "WR_URGENCYLVL_03_CYC_VC3" },
	{ 105, "WR_URGENCYLVL_03_CYC_VC4" },
	{ 106, "WR_URGENCYLVL_03_CYC_VC5" },
	{ 107, "CSR_PREFETCH_GEN" },
	{ 108, "CSR_PREFETCH_DRP" },
	{ 109, "RQB_STALL_BE_AW_CYC" },
	{ 110, "RQB_STALL_BE_AR_CYC" },
	{ 111, "LOCK_CACHELINE" },
	{ 112, "LOCK_FAIL" },
	{ 140, "TAGRAM_ACCESS_CYCLE" },
	{ 141, "TAGRAM_RET_CYCLE" },
	{ 142, "TAGRAM_PG_CYCLE" },
	{ 180, "DATARAM_ACCESS_CYCLE" },
	{ 181, "DATARAM_RET_CYCLE" },
	{ 182, "DATARAM_PG_CYCLE" },
	{ 220, "PFW_RAM_ACCESS_CYCLE" },
	{ 221, "PFW_RAM_RET_CYCLE" },
	{ 222, "PFW_RAM_PG_CYCLE" },
	{ 0, NULL },
};

static const struct filter_entry gslc_core_hw_filters[] = {
	{ 0, "aproftag" },
	{ 1, "gmsi_prot" },
	{ 2, "cacheop" },
	{ 3, "pbha" },
	{ 4, "qos" },
	{ 5, "of" },
	{ 6, "hit" },
	{ 7, "evict" },
	{ 8, "gmsi_atid" },
	{ 9, "gmsi_atype" },
	{ 10, "way_num" },
	{ 11, "vc" },
	{ 12, "auser" },
	{ 13, "ime_en" },
	{ 14, "pid_num" },
	{ 15, "gmsi_client_id" },
	{ 16, "gmsi_addr" },
	{ 17, "outside_vc" },
	{ 18, "outside_qos" },
	{ 0, NULL },
};

// Event definition reference: b/342531630#comment9
static const struct event_entry dvfs_gmc_hw_events[] = {
	{ 0, "total-read-requests", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 1, "dram-kbyte-count", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 2, "idle-cycles", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 3, "busy-cycles", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 4, "total-read-latency", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 0, NULL },
};

// Event definition reference: b/342531630#comment9
static const struct event_entry dvfs_gslc_hw_events[] = {
	{ 0, "total-read-miss-requests", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 1, "cache-kbyte-count", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 2, "ddr-kbyte-count", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 3, "total-read-miss-latency", GEM_EVENT_TYPE_ACCUMULATOR },
	{ 0, NULL },
};

static const struct eventgrp_desc gem_event_groups[] = {
	{ "std", std_hw_events, std_hw_filters },
	{ "dvfs-noc", dvfs_noc_hw_events, std_hw_filters },
	{ "dvfs-gmc", dvfs_gmc_hw_events, NULL },
	{ "dvfs-gslc", dvfs_gslc_hw_events, NULL },
	{ "gmc-generic", gmc_generic_hw_events, gmc_generic_hw_filters },
	{ "gmc-dfi", gmc_dfi_hw_events, gmc_dfi_hw_filters },
	{ "gmc-dfi-v2", gmc_dfi_hw_events_v2, gmc_dfi_hw_filters },
	{ "gslc-core", gslc_core_hw_events, gslc_core_hw_filters },
	{ NULL },
};

static inline void set_pa(phys_addr_t pa, u32 *pa_low, u32 *pa_high)
{
	*pa_low = (u32)(((uintptr_t)pa) & 0xffffffffUL);
	*pa_high = (u32)((((uintptr_t)pa) >> 32) & 0xffffffffUL);
}

static int gdmc_gem_ctrl(struct gem_drvdata *drvdata, int cmd, struct gdmc_msg *msg_buf,
			 void *resp_buf)
{
	int ret;
	struct gdmc_msg *resp_msg = resp_buf;

	msg_buf->header = goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_GEM_CTRL, cmd);
	ret = gdmc_send_message(drvdata->gdmc_iface, msg_buf);
	if (ret) {
		u32 resp_err = goog_mba_nq_xport_get_error(&msg_buf->header);
		u32 resp_data = goog_mba_nq_xport_get_data(&msg_buf->header);

		dev_err(drvdata->dev, "%s: %s: cmd=%d ret=%d err=%u data=%u\n", __func__,
			resp_err ? "gdmc returned err" : "failed to send msg",
			cmd, ret, resp_err, resp_data);
		print_hex_dump(KERN_ERR, "raw: ", DUMP_PREFIX_OFFSET, 16, 4,
			       msg_buf, sizeof(*msg_buf), false);
	} else if (resp_msg) {
		memcpy(resp_msg, msg_buf, sizeof(*resp_msg));
	}

	return ret;
}

static u8 build_event_cfg(u8 event_type, bool on)
{
	return BUILD_FIELD(GEM_EVENT_CFG_TYPE, event_type) | BUILD_FIELD(GEM_EVENT_CFG_ON, on);
}

static int gem_set_event_ctrl(struct gem_drvdata *drvdata, u8 ip, u8 counter_id, u8 event_id,
			      u8 event_type, bool on, phys_addr_t buf_pa)
{
	struct gdmc_msg msg_buf;

	msg_buf.event_ctrl.ip = ip;
	msg_buf.event_ctrl.counter_id = counter_id;
	msg_buf.event_ctrl.event_id = event_id;
	msg_buf.event_ctrl.event_cfg = build_event_cfg(event_type, on);
	set_pa(buf_pa, &msg_buf.event_ctrl.buf_pa_low, &msg_buf.event_ctrl.buf_pa_high);
	return gdmc_gem_ctrl(drvdata, GEMCTRL_CMD_EVENT_CTRL, &msg_buf, NULL);
}

static int gem_add_event(struct gem_drvdata *drvdata, u8 ip, u8 counter_id, u8 event_id,
			 u8 event_type, phys_addr_t buf_pa)
{
	return gem_set_event_ctrl(drvdata, ip, counter_id, event_id, event_type, true, buf_pa);
}

static int gem_del_event(struct gem_drvdata *drvdata, u8 ip, u8 counter_id)
{
	return gem_set_event_ctrl(drvdata, ip, counter_id, 0, 0, false, 0);
}

static int gem_set_ip_ctrl(struct gem_drvdata *drvdata, u8 ip, bool on)
{
	struct gdmc_msg msg_buf;

	msg_buf.ip_ctrl.ip = ip;
	msg_buf.ip_ctrl.on = on;
	return gdmc_gem_ctrl(drvdata, GEMCTRL_CMD_IP_CTRL, &msg_buf, NULL);
}

static int gem_set_mode_ctrl(struct gem_drvdata *drvdata, u8 mode, u32 period_ms)
{
	struct gdmc_msg msg_buf;

	msg_buf.mode_ctrl.mode = mode;
	msg_buf.mode_ctrl.period_ms = period_ms;
	return gdmc_gem_ctrl(drvdata, GEMCTRL_CMD_MODE_CTRL, &msg_buf, NULL);
}

static int gem_set_filter_ctrl(struct gem_drvdata *drvdata, u8 ip, u8 counter_id, u8 cfg_type,
			       u8 op, u16 type, u32 value, u32 mask)
{
	struct gdmc_msg msg_buf;

	msg_buf.filter_ctrl.ip = ip;
	msg_buf.filter_ctrl.cfg = BUILD_FIELD(GEM_FILTER_CFG_TYPE, cfg_type) |
		BUILD_FIELD(GEM_FILTER_CFG_CNTR_ID, counter_id);
	msg_buf.filter_ctrl.op = op;
	msg_buf.filter_ctrl.type = type;
	msg_buf.filter_ctrl.value = value;
	msg_buf.filter_ctrl.mask = mask;

	return gdmc_gem_ctrl(drvdata, GEMCTRL_CMD_FILTER_CTRL, &msg_buf, NULL);
}

static int gem_set_filter_reset(struct gem_drvdata *drvdata, u8 ip, u8 counter_id)
{
	return gem_set_filter_ctrl(drvdata, ip, counter_id, GEM_FILTER_TYPE_COUNTER,
				   GEM_FILTER_CMD_OP_RESET, 0, 0, 0);
}

static int gem_set_filter_add(struct gem_drvdata *drvdata, u8 ip, u8 counter_id, u16 type,
			      u64 value, u64 mask)
{
	int ret_lo = gem_set_filter_ctrl(drvdata, ip, counter_id, GEM_FILTER_TYPE_COUNTER,
					 GEM_FILTER_CMD_OP_ADD_LOW, type, lower_32_bits(value),
					 lower_32_bits(mask));
	int ret_hi = gem_set_filter_ctrl(drvdata, ip, counter_id, GEM_FILTER_TYPE_COUNTER,
					 GEM_FILTER_CMD_OP_ADD_HIGH, type, upper_32_bits(value),
					 upper_32_bits(mask));

	if (ret_lo)
		return ret_lo;
	if (ret_hi)
		return ret_hi;

	return 0;
}

static int gem_set_filter_disable(struct gem_drvdata *drvdata, u8 ip, u8 counter_id, u16 type)
{
	return gem_set_filter_ctrl(drvdata, ip, counter_id, GEM_FILTER_TYPE_COUNTER,
				   GEM_FILTER_CMD_OP_DISABLE, type, 0, 0);
}

static int gem_set_trace_ctrl(struct gem_drvdata *drvdata, u8 ip, u8 enable, u8 id, u8 types)
{
	struct gdmc_msg msg_buf;

	msg_buf.trace_ctrl.ip = ip;
	msg_buf.trace_ctrl.enable = enable;
	msg_buf.trace_ctrl.id = id;
	msg_buf.trace_ctrl.types = types;
	return gdmc_gem_ctrl(drvdata, GEMCTRL_CMD_TRACE_CTRL, &msg_buf, NULL);
}

static int alloc_cntbuf(struct dma_pool *pool, struct cntbuf *cntbuf)
{
	cntbuf->va = dma_pool_zalloc(pool, GFP_KERNEL, &cntbuf->pa);
	if (!cntbuf->va)
		return -ENOMEM;
	return 0;
}

static void free_cntbuf(struct dma_pool *pool, struct cntbuf *cntbuf)
{
	dma_pool_free(pool, cntbuf->va, cntbuf->pa);
}

static ssize_t mode_ctrl_write(struct file *filp, const char __user *ubuf, size_t size,
			       loff_t *ppos)
{
	struct gem_drvdata *drvdata = filp->private_data;
	int ret;
	u8 mode;
	u32 period_ms;
	char *buf, *mode_str, *mode_end, *period_ms_str;

	if (*ppos)
		return -ERANGE;

	buf = strndup_user(ubuf, size + 1);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	mode_str = skip_spaces(buf);
	mode_end = strchrnul(mode_str, ' '); // skip to ' ' after <mode>, or terminating '\0'.
	period_ms_str = skip_spaces(mode_end); // skip spaces between <mode> and <period_ms>.
	*mode_end = '\0';

	if (!strcmp(mode_str, "userctrl")) {
		mode = GEM_MODE_USERCTRL;
	} else if (!strcmp(mode_str, "once")) {
		mode = GEM_MODE_ONCE;
	} else if (!strcmp(mode_str, "interval")) {
		mode = GEM_MODE_INTERVAL;
	} else {
		dev_err(drvdata->dev, "Could not parse 'mode' from '%s'\n", buf);
		ret = -EINVAL;
		goto exit;
	}

	/* Parse period_ms_str */
	ret = kstrtou32(period_ms_str, 0, &period_ms);
	if (ret < 0) {
		dev_err(drvdata->dev, "Could not parse 'period_ms' from '%s': %d\n", buf, ret);
		ret = -EINVAL;
		goto exit;
	}

	ret = gem_set_mode_ctrl(drvdata, mode, period_ms);
	if (ret)
		dev_err(drvdata->dev, "send cmd failed: mode=%d period_ms=%u: %d\n",
			mode, period_ms, ret);

exit:
	kfree(buf);
	return ret ? ret : size;
}

static const struct file_operations mode_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = mode_ctrl_write,
};

struct ip_export_filter_entry {
	struct list_head list_node;
	struct gem_drvdata *drvdata;

	bool is_enabled;
	u8 ip;
	u8 counter_id;
	u16 type;
	u64 value;
	u64 mask;
};

struct ip_export_entry;
struct ip_export_counter_entry {
	struct ip_export_entry *ip_ent;
	u8 counter_id;
	struct list_head filter_list;

	u8 event_id;
	u8 event_type;
	bool is_enabled;
	struct cntbuf cntbuf;
};

struct trace_type {
	u8 id;
	const char *name;
};

static const struct trace_type gem_trace_types[] = {
	{0, "read_req"},
	{1, "write_req"},
	{2, "read_data"},
	{3, "write_data"},
	{4, "generic_data"},
};

struct ip_trace_conf {
	u8 id;
	bool is_enabled;
	bool types[ARRAY_SIZE(gem_trace_types)];
	struct list_head filter_list;
};

struct trace_filter_entry {
	struct list_head list_node;
	u8 filter_id;
	bool is_enabled;
	u64 value;
	u64 mask;
};

struct ip_export_entry {
	int ip;
	struct gem_drvdata *drvdata;
	struct dentry *ip_dir;
	const struct eventgrp *eventgrp;
	const struct ip_info_entry *ip_info;
	struct ip_trace_conf trace_conf;
	int num_counters;
	struct ip_export_counter_entry counters[];
};

static ssize_t gem_counter_enable_write(struct file *filp, const char __user *ubuf, size_t size,
					loff_t *ppos)
{
	int ret;
	struct ip_export_counter_entry *cnt_ent = filp->private_data;
	struct ip_export_entry *ip_ent = cnt_ent->ip_ent;
	struct cntbuf *cntbuf = &cnt_ent->cntbuf;

	if (*ppos)
		return 0;

	ret = kstrtobool_from_user(ubuf, size, &cnt_ent->is_enabled);
	if (ret)
		return ret;

	if (!cntbuf->va) {
		ret = alloc_cntbuf(ip_ent->drvdata->cntbuf_pool, cntbuf);
		if (ret) {
			dev_err(ip_ent->drvdata->dev, "failed to alloc_cntbuf(c%d) for ip %d: %d\n",
				cnt_ent->counter_id, ip_ent->ip, ret);
			return ret;
		}
	}

	ret = gem_set_event_ctrl(ip_ent->drvdata, ip_ent->ip, cnt_ent->counter_id,
				 cnt_ent->event_id, cnt_ent->event_type, cnt_ent->is_enabled,
				 cntbuf->pa);
	if (ret) {
		dev_err(ip_ent->drvdata->dev, "set_event failed: ip=%d cntr=%d event=%d on=%d: %d\n",
			ip_ent->ip, cnt_ent->counter_id, cnt_ent->event_id, cnt_ent->is_enabled,
			ret);
		return ret;
	}

	return size;
}

static ssize_t gem_counter_enable_read(struct file *filp, char __user *ubuf, size_t size,
				       loff_t *ppos)
{
	char buf[3];
	struct ip_export_counter_entry *cnt_ent = filp->private_data;

	if (*ppos)
		return 0;

	snprintf(buf, sizeof(buf), "%u\n", cnt_ent->is_enabled);
	return simple_read_from_buffer(ubuf, size, ppos, buf, strlen(buf));
}

static ssize_t gem_counter_value_read(struct file *filp, char __user *ubuf, size_t size,
				      loff_t *ppos)
{
	char buf[128];
	struct ip_export_counter_entry *cnt_ent = filp->private_data;
	struct cntbuf *cntbuf = &cnt_ent->cntbuf;

	if (*ppos)
		return 0;

	if (!cntbuf->va)
		return -EFAULT;

	snprintf(buf, sizeof(buf), "%llu\n", *cntbuf->va);
	return simple_read_from_buffer(ubuf, size, ppos, buf, strlen(buf));
}

static const struct file_operations gem_counter_enable_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gem_counter_enable_write,
	.read = gem_counter_enable_read,
};

static const struct file_operations gem_counter_value_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = gem_counter_value_read,
};

static ssize_t gem_ip_ctrl_write(struct file *filp, const char __user *ubuf, size_t size,
				 loff_t *ppos)
{
	const struct ip_export_entry *ip_ent = filp->private_data;
	int ret;
	bool enable;

	if (*ppos)
		return -ERANGE;

	ret = kstrtobool_from_user(ubuf, size, &enable);
	if (ret)
		return ret;

	ret = gem_set_ip_ctrl(ip_ent->drvdata, ip_ent->ip, enable);
	return ret ? ret : size;
}

static const struct file_operations gem_ip_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gem_ip_ctrl_write,
};

static void efcfg_remove_matched_event(struct gem_drvdata *drvdata, u8 ip, u8 event_id)
{
	struct event_filter_config *efconf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&drvdata->efcfg_lock, flags);
	list_for_each_entry_safe(efconf, tmp, &drvdata->efcfg_list, list_node) {
		if (efconf->ip != ip || efconf->event_id != event_id)
			continue;

		list_del(&efconf->list_node);
		devm_kfree(drvdata->dev, efconf);
	}
	spin_unlock_irqrestore(&drvdata->efcfg_lock, flags);
}

/*
 * Resolves arg token starting from the first non-space char from `pend`, until next space char or
 * \0 as end. Set the end to be a \0 char, and skips pend to the following non-space char. Returns
 * start position of the arg token if it's found, otherwise the pointer pointing to \0.
 */
static inline char *resolve_arg(char **pend)
{
	char *pos = skip_spaces(*pend);
	char *end = strchrnul(pos, ' ');

	if (*end) {
		*end = '\0';
		end = skip_spaces(end + 1);
	}

	*pend = end;
	return pos;
}

static ssize_t gem_set_event_filter_write(struct file *filp, const char __user *ubuf, size_t size,
					  loff_t *ppos)
{
	int ret;
	struct ip_export_entry *ip_ent = filp->private_data;
	struct gem_drvdata *drvdata = ip_ent->drvdata;
	const struct eventgrp *eventgrp = ip_ent->eventgrp;
	const struct ip_info_entry *info = ip_ent->ip_info;
	struct device *dev = drvdata->dev;
	char *buf, *pos, *end;
	const struct event_entry *event;
	const struct filter_entry *filter;
	u8 event_id, filter_id;
	u64 addr, mask;
	struct event_filter_config *efconf;
	unsigned long flags;

	if (*ppos)
		return -ERANGE;

	buf = strndup_user(ubuf, size + 1);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	// 0. Start from &buf[0].
	end = buf;

	// 1. Resolve event-name.
	pos = resolve_arg(&end);
	ret = -EINVAL;
	eventgrp_for_each_event(event, eventgrp) {
		if (!strcmp(pos, event->name)) {
			event_id = event->id;
			ret = 0;
			break;
		}
	}
	if (ret) {
		dev_err(dev, "failed to resolve event %s: %d\n", pos, ret);
		goto exit;
	}

	// 2. Identify filter-name from '-', a valid filter name, or else.
	pos = resolve_arg(&end);

	if (*pos == '-') {
		efcfg_remove_matched_event(drvdata, info->id, event_id);
		ret = 0;
		goto exit;
	}

	ret = -EINVAL;
	eventgrp_for_each_filter(filter, eventgrp) {
		if (!strcmp(pos, filter->name)) {
			filter_id = filter->id;
			ret = 0;
			break;
		}
	}
	if (ret) {
		dev_err(dev, "failed to resolve filter %s: %d\n", pos, ret);
		goto exit;
	}

	// 3. Resolve filter-value.
	pos = resolve_arg(&end);
	ret = kstrtou64(pos, 0, &addr);
	if (ret) {
		dev_err(dev, "failed to parse 'addr': %d\n", ret);
		goto exit;
	}

	// 4. Resolve filter-mask.
	pos = resolve_arg(&end);
	ret = kstrtou64(pos, 0, &mask);
	if (ret) {
		dev_err(dev, "failed to parse 'mask': %d\n", ret);
		goto exit;
	}

	// 5. Resolve end-of-args.
	if (*end) {
		ret = -EINVAL;
		goto exit;
	}

	efconf = devm_kzalloc(dev, sizeof(*efconf), GFP_KERNEL);
	if (!efconf) {
		ret = -ENOMEM;
		goto exit;
	}

	efconf->ip = info->id;
	efconf->event_id = event_id;
	efconf->filter_id = filter_id;
	efconf->addr = addr;
	efconf->mask = mask;

	spin_lock_irqsave(&drvdata->efcfg_lock, flags);
	list_add_tail(&efconf->list_node, &drvdata->efcfg_list);
	spin_unlock_irqrestore(&drvdata->efcfg_lock, flags);

exit:
	kfree(buf);
	return ret ? ret : size;
}

static const struct file_operations gem_set_event_filter_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gem_set_event_filter_write,
};

static ssize_t gem_counter_filter_enable_write(struct file *filp, const char __user *ubuf,
					       size_t size, loff_t *ppos)
{
	int ret;
	struct ip_export_filter_entry *fent = filp->private_data;

	if (*ppos)
		return 0;

	ret = kstrtobool_from_user(ubuf, size, &fent->is_enabled);
	if (ret)
		return ret;

	if (fent->is_enabled)
		ret = gem_set_filter_add(fent->drvdata, fent->ip, fent->counter_id, fent->type,
					 fent->value, fent->mask);
	else
		ret = gem_set_filter_disable(fent->drvdata, fent->ip, fent->counter_id, fent->type);

	if (ret) {
		dev_err(fent->drvdata->dev,
			"failed to %s filter: ip=%d cntr=%d type=%d value=%#llx mask=%#llx: %d\n",
			fent->is_enabled ? "enable" : "disable",
			fent->ip, fent->counter_id, fent->type, fent->value, fent->mask, ret);
		return ret;
	}

	return size;
}

static ssize_t gem_counter_filter_enable_read(struct file *filp, char __user *ubuf, size_t size,
					      loff_t *ppos)
{
	char buf[3];
	struct ip_export_filter_entry *filter_entry = filp->private_data;

	if (*ppos)
		return 0;

	snprintf(buf, sizeof(buf), "%u\n", filter_entry->is_enabled);
	return simple_read_from_buffer(ubuf, size, ppos, buf, strlen(buf));
}

static const struct file_operations gem_counter_filter_enable_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gem_counter_filter_enable_write,
	.read = gem_counter_filter_enable_read,
};

static ssize_t gem_trace_filter_enable_write(struct file *filp, const char __user *ubuf,
					     size_t size, loff_t *ppos)
{
	int ret;
	struct ip_trace_conf *trace_conf = filp->private_data;
	struct ip_export_entry *ip_ent = container_of(trace_conf, struct ip_export_entry,
						      trace_conf);
	struct gem_drvdata *drvdata = ip_ent->drvdata;
	struct trace_filter_entry *tent;
	bool enable;
	u8 types = 0;

	if (*ppos)
		return 0;

	ret = kstrtobool_from_user(ubuf, size, &enable);
	if (ret)
		return ret;

	if (trace_conf->is_enabled && enable)
		return -EBUSY;

	if (!trace_conf->is_enabled && !enable)
		return size;

	list_for_each_entry(tent, &ip_ent->trace_conf.filter_list, list_node) {
		if (!enable || !tent->is_enabled)
			continue;

		ret = gem_set_filter_ctrl(drvdata, ip_ent->ip, 0, GEM_FILTER_TYPE_TRACE,
					  GEM_FILTER_CMD_OP_ADD_LOW, tent->filter_id,
					  lower_32_bits(tent->value), lower_32_bits(tent->mask));
		if (ret)
			goto err_exit;
		ret = gem_set_filter_ctrl(drvdata, ip_ent->ip, 0, GEM_FILTER_TYPE_TRACE,
					  GEM_FILTER_CMD_OP_ADD_HIGH, tent->filter_id,
					  upper_32_bits(tent->value), upper_32_bits(tent->mask));
		if (ret)
			goto err_exit;
	}

	for (int i = 0; i < ARRAY_SIZE(trace_conf->types); i++) {
		if (trace_conf->types[i])
			types |= 1 << i;
	}

	ret = gem_set_trace_ctrl(drvdata, ip_ent->ip, enable, trace_conf->id, types);
	if (ret)
		goto err_exit;

	trace_conf->is_enabled = enable;
	return size;

err_exit:
	dev_err(drvdata->dev,
		"failed to %s trace filter: ip=%d id=%d types=%#08x: %d\n",
		enable ? "enable" : "disable", ip_ent->ip, trace_conf->id,
		types, ret);
	return ret;
}

static ssize_t gem_trace_filter_enable_read(struct file *filp, char __user *ubuf, size_t size,
					    loff_t *ppos)
{
	char buf[3];
	struct ip_trace_conf *trace_conf = filp->private_data;

	if (*ppos)
		return 0;

	snprintf(buf, sizeof(buf), "%u\n", trace_conf->is_enabled);
	return simple_read_from_buffer(ubuf, size, ppos, buf, strlen(buf));
}

static const struct file_operations gem_trace_filter_enable_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = gem_trace_filter_enable_write,
	.read = gem_trace_filter_enable_read,
};

static int create_ip_subfiles(struct ip_export_entry *ip_ent)
{
	int ret;
	struct gem_drvdata *drvdata = ip_ent->drvdata;
	struct dentry *ip_dir = ip_ent->ip_dir;
	const struct eventgrp *eventgrp = ip_ent->eventgrp;
	int ip = ip_ent->ip;
	const struct filter_entry *filter;
	struct dentry *trace_dir;
	struct dentry *trace_filters_dir;
	struct dentry *trace_types_dir;
	struct trace_filter_entry *tent, *ttmp;
	struct ip_export_filter_entry *fent, *ftmp;

	trace_dir = debugfs_create_dir("trace", ip_dir);
	debugfs_create_u8("ID", 0600, trace_dir, &ip_ent->trace_conf.id);

	trace_types_dir = debugfs_create_dir("types", trace_dir);
	for (size_t t = 0; t < ARRAY_SIZE(gem_trace_types); t++) {
		struct dentry *type_dir;

		type_dir = debugfs_create_dir(gem_trace_types[t].name, trace_types_dir);
		debugfs_create_bool("enable", 0600, type_dir, &ip_ent->trace_conf.types[t]);
		debugfs_create_u8("ID", 0600, type_dir, (unsigned char *)&gem_trace_types[t].id);
	}

	trace_filters_dir = debugfs_create_dir("filters", trace_dir);
	eventgrp_for_each_filter(filter, eventgrp) {
		struct dentry *filter_dir = debugfs_create_dir(filter->name,
							       trace_filters_dir);

		tent = devm_kzalloc(drvdata->dev, sizeof(*tent), GFP_KERNEL);
		if (!tent) {
			ret = -ENOMEM;
			goto err_exit;
		}

		tent->filter_id = filter->id;

		debugfs_create_bool("enable", 0600, filter_dir, &tent->is_enabled);
		debugfs_create_u8("ID", 0600, filter_dir, &tent->filter_id);
		debugfs_create_u64("value", 0600, filter_dir, &tent->value);
		debugfs_create_u64("mask", 0600, filter_dir, &tent->mask);

		list_add_tail(&tent->list_node, &ip_ent->trace_conf.filter_list);
	}
	debugfs_create_file("enable", 0600, trace_dir, &ip_ent->trace_conf,
			    &gem_trace_filter_enable_fops);

	for (int i = 0; i < ip_ent->num_counters; i++) {
		struct dentry *counter_dir;
		char namebuf[NAMEBUF_SZ];
		struct ip_export_counter_entry *cnt_ent = &ip_ent->counters[i];
		struct dentry *filters_dir;

		cnt_ent->ip_ent = ip_ent;
		cnt_ent->counter_id = i;

		snprintf(namebuf, sizeof(namebuf), COUNTER_FMT, i);
		counter_dir = debugfs_create_dir(namebuf, ip_dir);

		debugfs_create_u8("event", 0600, counter_dir, &cnt_ent->event_id);
		debugfs_create_u8("event_type", 0600, counter_dir, &cnt_ent->event_type);
		debugfs_create_file("enable", 0600, counter_dir, cnt_ent, &gem_counter_enable_fops);
		debugfs_create_file("value", 0400, counter_dir, cnt_ent, &gem_counter_value_fops);

		filters_dir = debugfs_create_dir("filters", counter_dir);
		eventgrp_for_each_filter(filter, eventgrp) {
			struct dentry *filter_dir = debugfs_create_dir(filter->name, filters_dir);

			fent = devm_kzalloc(drvdata->dev, sizeof(*fent), GFP_KERNEL);
			if (!fent) {
				ret = -ENOMEM;
				goto err_exit;
			}

			fent->drvdata = drvdata;
			fent->ip = ip;
			fent->counter_id = i;
			fent->type = filter->id;

			debugfs_create_file("enable", 0600, filter_dir, fent,
					    &gem_counter_filter_enable_fops);
			debugfs_create_u64("value", 0600, filter_dir, &fent->value);
			debugfs_create_u64("mask", 0600, filter_dir, &fent->mask);

			list_add_tail(&fent->list_node, &cnt_ent->filter_list);
		}
	}

	debugfs_create_file("set_event_filter", 0200, ip_dir, ip_ent,
			    &gem_set_event_filter_fops);
	debugfs_create_file("enable", 0200, ip_dir, ip_ent, &gem_ip_ctrl_fops);
	debugfs_create_u8("ID", 0400, ip_dir, (u8 *)&ip_ent->ip_info->id);
	debugfs_create_u8("num_counter", 0400, ip_dir, (u8 *)&ip_ent->ip_info->cntrs_num);
	return 0;

err_exit:
	list_for_each_entry_safe(tent, ttmp, &ip_ent->trace_conf.filter_list, list_node) {
		list_del(&tent->list_node);
		devm_kfree(drvdata->dev, tent);
	}
	for (int i = 0; i < ip_ent->num_counters; i++) {
		list_for_each_entry_safe(fent, ftmp, &ip_ent->counters[i].filter_list,
					 list_node) {
			list_del(&fent->list_node);
			devm_kfree(drvdata->dev, fent);
		}
	}
	return ret;
}

static void devm_release_ip_export_entry(struct device *dev, void *res)
{
	struct ip_export_entry *ip_ent = res;
	struct gem_drvdata *drvdata = ip_ent->drvdata;
	u32 ip = ip_ent->ip;

	gem_set_ip_ctrl(drvdata, ip, false);

	for (int i = 0; i < ip_ent->num_counters; i++) {
		gem_set_event_ctrl(drvdata, ip, i, 0, 0, false, 0);
		if (ip_ent->counters[i].cntbuf.va)
			free_cntbuf(drvdata->cntbuf_pool, &ip_ent->counters[i].cntbuf);
	}
}

static int devm_alloc_ip_export_entry(struct device *dev, struct gem_drvdata *drvdata,
				      struct dentry *ip_dir, const struct ip_info_entry *info,
				      const struct eventgrp *eventgrp)
{
	int ip = info->id;
	int num_counters = info->cntrs_num;
	int ret;
	struct ip_export_entry *ip_ent;

	ip_ent = devres_alloc(devm_release_ip_export_entry,
			      sizeof(*ip_ent) + num_counters * sizeof(*ip_ent->counters),
			      GFP_KERNEL | __GFP_ZERO);
	if (!ip_ent)
		return -ENOMEM;

	ip_ent->drvdata = drvdata;
	ip_ent->ip = ip;
	ip_ent->num_counters = num_counters;
	ip_ent->ip_dir = ip_dir;
	ip_ent->eventgrp = eventgrp;
	ip_ent->ip_info = info;
	INIT_LIST_HEAD(&ip_ent->trace_conf.filter_list);
	for (int i = 0; i < ip_ent->num_counters; i++)
		INIT_LIST_HEAD(&ip_ent->counters[i].filter_list);
	ret = create_ip_subfiles(ip_ent);
	if (ret) {
		dev_err(dev, "failed to create_ip_subfiles for ip %d: %d\n", ip, ret);
		devres_free(ip_ent);
		return ret;
	}

	devres_add(dev, ip_ent);
	return 0;
}

static const char *event_type_to_str(enum gem_event_type type)
{
	switch (type) {
	case GEM_EVENT_TYPE_ACCUMULATOR:
		return "accumulator";
	case GEM_EVENT_TYPE_HISTORICAL_HIGH:
		return "historical-high";
	case GEM_EVENT_TYPE_OUTSTANDING:
		return "outstanding";
	default:
		return "unknown";
	}
}

static ssize_t gem_show_event_type_read(struct file *filp, char __user *ubuf, size_t size,
					loff_t *ppos)
{
	char buf[128];
	enum gem_event_type type = (enum gem_event_type)filp->private_data;

	if (*ppos)
		return 0;

	snprintf(buf, sizeof(buf), "%s\n", event_type_to_str(type));
	return simple_read_from_buffer(ubuf, size, ppos, buf, strlen(buf));
}

static const struct file_operations gem_show_event_type_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = gem_show_event_type_read,
};

static int devm_create_ip_export_dir(struct gem_drvdata *drvdata, struct dentry *root_dir)
{
	struct dentry *dir;
	struct device *dev = drvdata->dev;
	struct eventgrp *eventgrp;
	int i = 0;

	dir = debugfs_create_dir("ip", root_dir);
	if (!dir)
		return -ENOMEM;

	list_for_each_entry(eventgrp, &drvdata->eventgrp_list, list_node) {
		char namebuf[NAMEBUF_SZ];
		const struct event_entry *event;
		const struct filter_entry *filter;
		const struct ip_info_entry *info;
		struct dentry *grp_dir, *events_dir, *filters_dir;

		snprintf(namebuf, sizeof(namebuf), EVENT_GRP_FMT, i);
		grp_dir = debugfs_create_dir(namebuf, root_dir);
		if (!grp_dir)
			return -ENOMEM;

		events_dir = debugfs_create_dir("events", grp_dir);
		if (!events_dir)
			return -ENOMEM;

		eventgrp_for_each_event(event, eventgrp) {
			struct dentry *event_dir = debugfs_create_dir(event->name, events_dir);

			debugfs_create_u8("ID", 0400, event_dir, (u8 *)&event->id);
			debugfs_create_file("type", 0400, event_dir, (void *)event->type,
					    &gem_show_event_type_fops);
		}

		filters_dir = debugfs_create_dir("filters", grp_dir);
		if (!filters_dir)
			return -ENOMEM;

		eventgrp_for_each_filter(filter, eventgrp) {
			struct dentry *filter_dir = debugfs_create_dir(filter->name, filters_dir);

			debugfs_create_u8("ID", 0400, filter_dir, (u8 *)&filter->id);
		}

		list_for_each_entry(info, &eventgrp->ip_info_list, list_node) {
			int ret;
			struct dentry *ip_dir;

			ip_dir = debugfs_create_dir(info->name, dir);
			if (!ip_dir)
				return -ENOMEM;

			snprintf(namebuf, sizeof(namebuf), "../../" EVENT_GRP_FMT "/events", i);
			debugfs_create_symlink("available_events", ip_dir, namebuf);

			snprintf(namebuf, sizeof(namebuf), "../../" EVENT_GRP_FMT "/filters", i);
			debugfs_create_symlink("available_filters", ip_dir, namebuf);

			ret = devm_alloc_ip_export_entry(dev, drvdata, ip_dir, info, eventgrp);
			if (ret) {
				dev_err(dev, "failed to devm_alloc_ip_export_entry(ip=%u): %d\n",
					info->id, ret);
				return ret;
			}
		}

		i++;
	}

	return 0;
}

/* PMU object to maintain counters usage of a GEM IP. */
struct gem_pmu {
	struct pmu pmu;
	struct gem_drvdata *drvdata;

	u8 ip;
	int num_counters;
	struct ida counter_ida;

	struct cntbuf cntbuf[];
};

static inline struct gem_pmu *to_gem_pmu(struct pmu *pmu)
{
	return container_of(pmu, struct gem_pmu, pmu);
}

static void gem_pmu_event_destroy(struct perf_event *event)
{
	int ret;
	struct gem_pmu *gpmu = to_gem_pmu(event->pmu);
	struct gem_drvdata *drvdata = gpmu->drvdata;
	struct device *dev = drvdata->dev;
	u8 ip = gpmu->ip;

	ret = gem_set_ip_ctrl(drvdata, ip, false);
	if (ret)
		dev_err(dev, "failed on gem_set_ip_ctrl(ip=%d, false): %d\n", ip, ret);

	ret = gem_del_event(drvdata, ip, event->hw.idx);
	if (ret)
		dev_err(dev, "failed on gem_del_event(ip=%d, counter_id=%d): %d\n",
			ip, event->hw.idx, ret);

	ret = gem_set_ip_ctrl(drvdata, ip, true);
	if (ret)
		dev_err(dev, "failed on gem_set_ip_ctrl(ip=%d, true): %d\n", ip, ret);

	free_cntbuf(drvdata->cntbuf_pool, &gpmu->cntbuf[event->hw.idx]);
	ida_free(&gpmu->counter_ida, event->hw.idx);
}

static int gem_pmu_apply_filters(struct gem_pmu *gpmu, int counter_id, u8 event_id)
{
	int ret = 0;
	struct gem_drvdata *drvdata = gpmu->drvdata;
	unsigned long flags;
	struct event_filter_config *efconf, *tmp;
	LIST_HEAD(event_filters);

	spin_lock_irqsave(&drvdata->efcfg_lock, flags);
	list_for_each_entry(efconf, &drvdata->efcfg_list, list_node) {
		if (efconf->ip != gpmu->ip || efconf->event_id != event_id)
			continue;

		tmp = kzalloc(sizeof(*tmp), GFP_ATOMIC);
		if (!tmp) {
			ret = -ENOMEM;
			break;
		}
		memcpy(tmp, efconf, sizeof(*tmp));
		INIT_LIST_HEAD(&tmp->list_node);
		list_add_tail(&tmp->list_node, &event_filters);
	}
	spin_unlock_irqrestore(&drvdata->efcfg_lock, flags);

	if (ret)
		goto exit;

	list_for_each_entry(efconf, &event_filters, list_node) {
		ret = gem_set_filter_add(drvdata, gpmu->ip, counter_id, efconf->filter_id,
					 efconf->addr, efconf->mask);
		if (ret)
			goto exit;
	}

exit:
	list_for_each_entry_safe(efconf, tmp, &event_filters, list_node) {
		list_del(&efconf->list_node);
		kfree(efconf);
	}
	return ret;
}

static int gem_pmu_event_init(struct perf_event *event)
{
	int ret;
	struct gem_pmu *gpmu = to_gem_pmu(event->pmu);
	u8 ip = gpmu->ip;
	struct gem_drvdata *drvdata = gpmu->drvdata;
	struct device *dev = drvdata->dev;
	int idx;
	u8 event_id;
	enum gem_event_type event_type;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (event->cpu < 0)
		return -EINVAL;

	// Alloc counter.
	idx = ida_alloc_max(&gpmu->counter_ida, gpmu->num_counters - 1, GFP_KERNEL);
	if (idx < 0)
		return -ENOSPC;

	ret = alloc_cntbuf(drvdata->cntbuf_pool, &gpmu->cntbuf[idx]);
	if (ret) {
		dev_err(dev, "failed on alloc_cntbuf(ip=%d, idx=%d): %d\n", ip, idx, ret);
		goto release_ida;
	}

	ret = gem_set_ip_ctrl(drvdata, ip, false);
	if (ret) {
		dev_err(dev, "failed on gem_set_ip_ctrl(ip=%d, false): %d\n", ip, ret);
		goto release_cntbuf;
	}

	ret = gem_set_mode_ctrl(drvdata, GEM_MODE_USERCTRL, DEFAULT_SCAN_PERIOD_MS);
	if (ret) {
		dev_err(dev, "failed on gem_set_mode_ctrl(): %d\n", ret);
		goto release_cntbuf;
	}

	*gpmu->cntbuf[idx].va = 0;
	event_id = FIELD_GET(EVENT_ID_FIELD_MASK, event->attr.config);
	event_type = FIELD_GET(EVENT_TYPE_FIELD_MASK, event->attr.config);
	ret = gem_add_event(drvdata, ip, idx, event_id, event_type, gpmu->cntbuf[idx].pa);
	if (ret) {
		dev_err(dev,
			"failed on gem_add_event(ip=%d, cntr_id=%d, event:id=%u:type=%s): %d\n",
			ip, idx, event_id, event_type_to_str(event_type), ret);
		goto release_cntbuf;
	}

	ret = gem_set_filter_reset(drvdata, ip, idx);
	if (ret)
		goto release_cntbuf;

	ret = gem_pmu_apply_filters(gpmu, idx, event_id);
	if (ret)
		goto release_cntbuf;

	ret = gem_set_ip_ctrl(drvdata, ip, true);
	if (ret) {
		dev_err(dev, "failed on gem_set_ip_ctrl(ip=%d, true): %d\n", ip, ret);
		goto release_cntbuf;
	}

	event->hw.idx = idx;
	event->destroy = gem_pmu_event_destroy;
	return 0;

release_cntbuf:
	free_cntbuf(drvdata->cntbuf_pool, &gpmu->cntbuf[idx]);
release_ida:
	ida_free(&gpmu->counter_ida, idx);
	return ret;
}

static void gem_pmu_start(struct perf_event *event, int pmu_flags)
{
	event->hw.state = 0;
}

static void gem_pmu_stop(struct perf_event *event, int pmu_flags)
{
	if (event->hw.state & PERF_HES_STOPPED)
		return;
	event->hw.state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static int gem_pmu_add(struct perf_event *event, int evflags)
{
	event->hw.state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (evflags & PERF_EF_START)
		gem_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	return 0;
}

static void gem_pmu_del(struct perf_event *event, int evflags)
{
	gem_pmu_stop(event, PERF_EF_UPDATE);

	perf_event_update_userpage(event);
}

static inline u64 gem_pmu_read_counter(struct perf_event *event)
{
	struct gem_pmu *gpmu = to_gem_pmu(event->pmu);

	return *gpmu->cntbuf[event->hw.idx].va;
}

static void gem_pmu_read(struct perf_event *event)
{
	local64_set(&event->count, gem_pmu_read_counter(event));
}

static ssize_t gem_pmu_sysfs_event_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct perf_pmu_events_attr *pmu_attr = container_of(attr, struct perf_pmu_events_attr,
							     attr);

	return sysfs_emit(buf, "event=%#llx\n", pmu_attr->id);
}

PMU_FORMAT_ATTR(event, "config:0-31");

static struct attribute *gem_pmu_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static const struct attribute_group gem_pmu_format_attr_group = {
	.name = "format",
	.attrs = gem_pmu_format_attrs,
};

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	cpumask_t cpu_mask;

	cpumask_set_cpu(0, &cpu_mask);
	return cpumap_print_to_pagebuf(true, buf, &cpu_mask);
}

static DEVICE_ATTR_RO(cpumask);

static struct attribute *gem_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group gem_pmu_cpumask_attr_group = {
	.attrs = gem_pmu_cpumask_attrs,
};

static void devm_del_gem_pmu(struct device *dev, void *res)
{
	struct gem_pmu *gpmu = res;

	perf_pmu_unregister(&gpmu->pmu);
	ida_destroy(&gpmu->counter_ida);
}

static int devm_add_gem_pmu(struct gem_drvdata *drvdata, u8 ip, const char *name, int num_counters,
			    const struct attribute_group **attr_groups)
{
	int ret;
	struct gem_pmu *gpmu;

	gpmu = devres_alloc(devm_del_gem_pmu,
			    sizeof(*gpmu) + (sizeof(*gpmu->cntbuf) * num_counters),
			    GFP_KERNEL | __GFP_ZERO);
	if (!gpmu)
		return -ENOMEM;

	gpmu->pmu = (struct pmu) {
		.module		= THIS_MODULE,
		.name		= name,
		.capabilities	= PERF_PMU_CAP_NO_INTERRUPT,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= gem_pmu_event_init,
		.add		= gem_pmu_add,
		.del		= gem_pmu_del,
		.start		= gem_pmu_start,
		.stop		= gem_pmu_stop,
		.read		= gem_pmu_read,
		.attr_groups	= attr_groups,
	};
	gpmu->ip = ip;
	gpmu->num_counters = num_counters;
	gpmu->drvdata = drvdata;
	ida_init(&gpmu->counter_ida);

	ret = perf_pmu_register(&gpmu->pmu, name, -1);
	if (ret) {
		dev_err(drvdata->dev, "failed to perf_pmu_register(%s): %d\n", name, ret);
		ida_destroy(&gpmu->counter_ida);
		devres_free(gpmu);
		return ret;
	}

	devres_add(drvdata->dev, gpmu);
	return 0;
}

/* Ask GDMC firmware to populate data_size byte buffer at data_pa with the ip list */
static int gem_list_ip(struct gem_drvdata *drvdata, phys_addr_t data_pa, size_t data_size)
{
	struct gdmc_msg msg_buf;

	msg_buf.ip_list.data_size = data_size;
	set_pa(data_pa, &msg_buf.ip_list.data_pa_low, &msg_buf.ip_list.data_pa_high);

	return gdmc_gem_ctrl(drvdata, GEMCTRL_CMD_IP_LIST, &msg_buf, NULL);
}

#define LK_ERR_NOT_ENOUGH_BUFFER -9

/* Query GDMC firmware for the size of the ip list, in bytes */
static int gem_list_ip_size_get(struct gem_drvdata *drvdata)
{
	int ret;
	struct gdmc_msg msg_buf = {};

	ret = gdmc_gem_ctrl(drvdata, GEMCTRL_CMD_IP_LIST, &msg_buf, &msg_buf);

	if (ret && (s16)goog_mba_nq_xport_get_data(&msg_buf.header) == LK_ERR_NOT_ENOUGH_BUFFER)
		return msg_buf.ip_list.resp.size;

	return -EBADMSG;
}

static const char *set_eventgrp(struct device *dev, const char *pos, const char *end,
				const struct eventgrp_desc *eventgrp_desc,
				struct eventgrp *eventgrp)
{
	if (!*end)
		return ERR_PTR(-EFAULT);

	for (const struct eventgrp_desc *eg = &eventgrp_desc[0]; eg->name; eg++) {
		if (strncmp(eg->name, pos, end - pos))
			continue;

		eventgrp->hw_desc = eg;
		return end;
	}

	return ERR_PTR(-ENOENT);
}

int consume_ip_info(struct device *dev, const char *pos, size_t size, void *data)
{
	int ret;
	struct ip_info_entry *info;
	struct list_head *ip_info_list = data;
	const char *argv[3];
	size_t namelen;

	ret = split_args(pos, size, ':', argv, ARRAY_SIZE(argv));
	if (ret)
		return ret;

	namelen = argv[2] - argv[1] - 1;
	info = devm_kzalloc(dev, sizeof(*info) + namelen + 1, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = kstrntou8(argv[0], argv[1] - argv[0] - 1, 0, &info->id);
	if (ret < 0) {
		dev_err(dev, "failed to parse 'id' for ip %10s...: %d\n", argv[0], ret);
		devm_kfree(dev, info);
		return -EINVAL;
	}

	ret = kstrntou8(argv[2], pos + size - argv[2], 0, &info->cntrs_num);
	if (ret < 0) {
		dev_err(dev, "failed to parse 'cntrs-num' for ip %10s...: %d\n", argv[2], ret);
		devm_kfree(dev, info);
		return -EINVAL;
	}

	strscpy(info->name, argv[1], namelen + 1);
	list_add_tail(&info->list_node, ip_info_list);
	return 0;
}
EXPORT_SYMBOL_GPL(consume_ip_info);

int gem_ip_info_parser(struct device *dev, const char *pos,
		       const struct eventgrp_desc *eventgrp_desc,
		       struct list_head *eventgrp_list)
{
	int ret;
	struct eventgrp *grp, *tmp_grp;
	struct ip_info_entry *info, *tmp_info;

	// Check version magic.
	if (memcmp(pos, IP_LIST_MAGIC, IP_LIST_MAGIC_SZ)) {
		dev_err(dev, "ip-list magic is invalid\n");
		print_hex_dump(KERN_ERR, "magic: ", DUMP_PREFIX_OFFSET, 16, 4,
			       pos, IP_LIST_MAGIC_SZ, true);
		return -EINVAL;
	}

	pos = pos + IP_LIST_MAGIC_SZ;
	while (true) {
		struct eventgrp *eventgrp;

		eventgrp = devm_kmalloc(dev, sizeof(*eventgrp), GFP_KERNEL);
		if (!eventgrp) {
			ret = -ENOMEM;
			goto err_exit;
		}

		INIT_LIST_HEAD(&eventgrp->ip_info_list);
		list_add_tail(&eventgrp->list_node, eventgrp_list);

		// 1. Parse name of event group.
		pos = set_eventgrp(dev, pos, strchrnul(pos, '|'), eventgrp_desc, eventgrp);
		if (IS_ERR(pos)) {
			ret = PTR_ERR(pos);
			dev_err(dev, "Error on parsing event group name: %d\n", ret);
			goto err_exit;
		}
		if (!*pos++) {
			ret = -EINVAL;
			goto err_exit;
		}

		// 2. Parse IPs of the ip-group.
		pos = parse_substr(dev, pos, strchrnul(pos, '|'), ',', consume_ip_info,
				   &eventgrp->ip_info_list);
		if (IS_ERR(pos)) {
			ret = PTR_ERR(pos);
			dev_err(dev, "Error on parsing ip-list: %d\n", ret);
			goto err_exit;
		}
		if (list_empty(&eventgrp->ip_info_list)) {
			ret = -ENOENT;
			goto err_exit;
		}
		if (!*pos++)
			break;
	}

	return list_empty(eventgrp_list) ? -ENOENT : 0;

err_exit:
	list_for_each_entry_safe(grp, tmp_grp, eventgrp_list, list_node) {
		list_for_each_entry_safe(info, tmp_info, &grp->ip_info_list,
					 list_node) {
			list_del(&info->list_node);
			devm_kfree(dev, info);
		}

		list_del(&grp->list_node);
		devm_kfree(dev, grp);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(gem_ip_info_parser);

static int gem_ip_info_init(struct gem_drvdata *drvdata)
{
	int data_sz;
	phys_addr_t data_pa;
	void *data;
	int ret;
	struct device *dev = drvdata->dev;

	data_sz = gem_list_ip_size_get(drvdata);
	if (data_sz < 0) {
		dev_err(dev, "failed to get list_ip size: %d\n", data_sz);
		return data_sz;
	}

	data = dma_alloc_coherent(dev, data_sz, &data_pa, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&drvdata->eventgrp_list);

	ret = gem_list_ip(drvdata, data_pa, data_sz);
	if (ret) {
		dev_err(dev, "failed to get list_ip: %d\n", ret);
		goto free_data;
	}

	ret = gem_ip_info_parser(dev, data, drvdata->eventgrp_desc, &drvdata->eventgrp_list);

free_data:
	dma_free_coherent(dev, data_sz, data, data_pa);
	return ret;
}

struct gem_pmu_attr_grp {
	const struct attribute_group *attr_groups[4];
	struct attribute_group events_attr_grp;
	struct perf_pmu_events_attr *pmu_event_attrs;
	struct attribute **event_attrs;
};

static const struct attribute_group **devm_event_attrgrp_alloc(struct device *dev,
							       struct eventgrp *eventgrp)
{
	int i;
	size_t events_num = 0;
	const struct event_entry *event;
	struct gem_pmu_attr_grp *grp;
	size_t grp_size;

	eventgrp_for_each_event(event, eventgrp)
		events_num++;

	/*
	 * attrgrp layout:
	 * - struct gem_pmu_attr_grp     grp;
	 * - struct perf_pmu_events_attr pattr[events_num];
	 * - struct attribute            *attr[events_num + 1];
	 */
	grp_size = sizeof(*grp);
	grp_size += sizeof(*grp->pmu_event_attrs) * events_num;
	grp_size += sizeof(*grp->event_attrs) * (events_num + 1);

	grp = devm_kzalloc(dev, grp_size, GFP_KERNEL);
	if (!grp)
		return NULL;

	grp->pmu_event_attrs = (struct perf_pmu_events_attr *)(grp + 1);
	grp->event_attrs = (struct attribute **)(grp->pmu_event_attrs + events_num);

	i = 0;
	eventgrp_for_each_event(event, eventgrp) {
		struct perf_pmu_events_attr *pp_attr = &grp->pmu_event_attrs[i];
		struct attribute *attr = &pp_attr->attr.attr;

		sysfs_attr_init(attr);

		attr->name = event->name;
		attr->mode = VERIFY_OCTAL_PERMISSIONS(0444);
		pp_attr->attr.show = gem_pmu_sysfs_event_show;
		pp_attr->id = FIELD_PREP(EVENT_ID_FIELD_MASK, event->id) |
			FIELD_PREP(EVENT_TYPE_FIELD_MASK, event->type);

		grp->event_attrs[i++] = attr;
	}

	grp->events_attr_grp.name = "events";
	grp->events_attr_grp.attrs = grp->event_attrs;

	grp->attr_groups[0] = &gem_pmu_cpumask_attr_group;
	grp->attr_groups[1] = &gem_pmu_format_attr_group;
	grp->attr_groups[2] = &grp->events_attr_grp;

	return grp->attr_groups;
}

static int google_gem_client_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct gem_drvdata *gem_drvdata;
	struct eventgrp *eventgrp;
	struct dentry *dir;

	gem_drvdata = devm_kzalloc(dev, sizeof(*gem_drvdata), GFP_KERNEL);
	if (!gem_drvdata)
		return -ENOMEM;

	gem_drvdata->eventgrp_desc = of_device_get_match_data(&pdev->dev);
	if (!gem_drvdata->eventgrp_desc)
		return -ENODEV;

	platform_set_drvdata(pdev, gem_drvdata);
	gem_drvdata->dev = dev;

	gem_drvdata->gdmc_iface = gdmc_iface_get(dev);
	ret = PTR_ERR_OR_ZERO(gem_drvdata->gdmc_iface);
	if (ret) {
		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "GDMC interface is not ready. Probe later. (ret: %d)", ret);
		else
			dev_err(dev, "Failed to get gdmc_iface: %d\n", ret);
		return ret;
	}

	ret = of_reserved_mem_device_init(dev);
	if (ret < 0) {
		dev_err(dev, "Failed to get reserved memory region: %d\n", ret);
		goto cleanup_gdmc_iface;
	}

	gem_drvdata->cntbuf_pool = dmam_pool_create("gem-cntbuf", dev, COUNTER_SIZE, COUNTER_SIZE,
						    0);
	if (!gem_drvdata->cntbuf_pool)
		return -ENOMEM;

	ret = gem_ip_info_init(gem_drvdata);
	if (ret) {
		dev_err(dev, "Failed to init GEM IP info: %d\n", ret);
		goto cleanup_rmem;
	}

	list_for_each_entry(eventgrp, &gem_drvdata->eventgrp_list, list_node) {
		struct ip_info_entry *info;
		const struct attribute_group **attr_groups;

		attr_groups = devm_event_attrgrp_alloc(dev, eventgrp);
		if (!attr_groups) {
			dev_err(dev, "Failed to alloc attr group\n");
			ret = -ENOMEM;
			goto cleanup_rmem;
		}

		list_for_each_entry(info, &eventgrp->ip_info_list, list_node) {
			ret = devm_add_gem_pmu(gem_drvdata, info->id, info->name, info->cntrs_num,
					       attr_groups);
			if (ret) {
				dev_err(dev, "Failed to init gem pmu for ip=%d:%s:%d: %d\n",
					info->id, info->name, info->cntrs_num, ret);
				goto cleanup_rmem;
			}
		}
	}

	spin_lock_init(&gem_drvdata->efcfg_lock);
	INIT_LIST_HEAD(&gem_drvdata->efcfg_list);

	dir = debugfs_create_dir("gem", NULL);
	if (!dir)
		dir = ERR_PTR(-ENOMEM);

	debugfs_create_file("mode_ctrl", 0200, dir, gem_drvdata, &mode_ctrl_fops);
	devm_create_ip_export_dir(gem_drvdata, dir);

	gem_drvdata->debugfs_dir = dir;

	return 0;

cleanup_rmem:
	of_reserved_mem_device_release(dev);
cleanup_gdmc_iface:
	gdmc_iface_put(gem_drvdata->gdmc_iface);
	return ret;
}

static void google_gem_client_remove(struct platform_device *pdev)
{
	struct gem_drvdata *gem_drvdata = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	debugfs_remove_recursive(gem_drvdata->debugfs_dir);
	of_reserved_mem_device_release(dev);
	gdmc_iface_put(gem_drvdata->gdmc_iface);
}

static const struct of_device_id google_gem_of_match_table[] = {
	{ .compatible = "google,gem", .data = gem_event_groups },
	{},
};
MODULE_DEVICE_TABLE(of, google_gem_of_match_table);

struct platform_driver google_gem_client_driver = {
	.probe = google_gem_client_probe,
	.remove = google_gem_client_remove,
	.driver = {
		.name = "google-gem",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_gem_of_match_table),
	},
};
module_platform_driver(google_gem_client_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GEM driver");
MODULE_LICENSE("GPL");
