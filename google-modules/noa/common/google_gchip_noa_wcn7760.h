// SPDX-License-Identifier: Google-proprietary
// Copyright 2024 Google LLC
#ifndef GOOGLE_GCHIP_NOA_WCN7760_H
#define GOOGLE_GCHIP_NOA_WCN7760_H

#include <wlan_ipa_obj_mgmt_api.h>
#include <qdf_types.h>
#include <qdf_lock.h>
#include <qdf_net_types.h>
#include <qdf_lro.h>
#include <qdf_module.h>
#include <hal_hw_headers.h>
#include <hal_api.h>
#include <hif.h>
#include <htt.h>
#include <wdi_event.h>
#include <queue.h>
#include "dp_types.h"
#include "dp_rings.h"
#include "dp_internal.h"
#include "dp_tx.h"
#include "dp_tx_desc.h"
#include "dp_rx.h"
#ifdef WLAN_FEATURE_UL_JITTER
#include "dp_hist.h"
#endif
#ifdef DP_RATETABLE_SUPPORT
#include "dp_ratetable.h"
#endif
#include <cdp_txrx_handle.h>
#include <wlan_cfg.h>
#include <wlan_utility.h>
#include "cdp_txrx_cmn_struct.h"
#include "cdp_txrx_stats_struct.h"
#include "cdp_txrx_cmn_reg.h"
#include <qdf_util.h>
#include "dp_peer.h"
#include "htt_stats.h"
#include "dp_htt.h"
#include "htt_ppdu_stats.h"
#include "qdf_mem.h"   /* qdf_mem_malloc,free */
#include "cfg_ucfg_api.h"
#include <wlan_module_ids.h>
#ifdef QCA_MULTIPASS_SUPPORT
#include <enet.h>
#endif
#include <common/core.h>
#include <common/wlan/noa_wlan.h>
#include <wlan/noa_wlan_client.h>
#include <wlan/noa_wlan_nep_helper.h>
#include <common/ring_id.h>
#include <common/wlan_ring_id.h>
//#include <wlan/google_plat_internal.h>
#include <wlan/google_plat.h>

enum dhd_mapper_pool_id {
WCN_MAPPER_POOL_APC_RX,
WCN_MAPPER_POOL_APC_TX,

__WCN_MAPPER_POOL_MAX,
};
#define MAX_PKTID_TX		(36 * 1024)
#define PKTID_MAX_MAP_SZ_RXCPLRING (8 * 1024)

typedef struct dp_rx_replenish_priv_def {
struct dp_soc *dp_soc;
uint32_t mac_id;
struct dp_srng *dp_rxdma_srng;
struct rx_desc_pool *rx_desc_pool;
uint32_t num_req_buffers;
union dp_rx_desc_list_elem_t **desc_list;
union dp_rx_desc_list_elem_t **tail;
bool req_only;
bool force_replenish;
const char *func_name;
}dp_rx_replenish_priv;
void wcn_noa_init(void* osdev, void* soc);
int noa_wlan_rxbm_sync(void *priv, u32 cnt, bool init);
void noa_update_rx_buff_count(u32 count);
void noa_update_rx_ppt_id_start(u32 ppt_id_start);
int _noa_wlan_rxbm_sync(void *priv, void *bufs, int cnt);
void noa_wlan_client_feedback_ring_update(uint32_t cookie);
#endif /* GOOGLE_GCHIP_NOA_WCN7760_H */
