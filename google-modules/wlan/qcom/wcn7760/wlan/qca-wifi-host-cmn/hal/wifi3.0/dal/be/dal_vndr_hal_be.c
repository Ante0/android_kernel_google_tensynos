/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */
#include "dal_vndr_hal_be.h"

void dal_vndr_hal_default_ops_attach_be(struct dal_vndr_hal_soc *hal_soc)
{
	hal_soc->ops->dal_vndr_hal_tx_desc_set_lmac_id =
				dal_vndr_hal_tx_desc_set_lmac_id_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_buf_addr =
				dal_vndr_hal_tx_desc_set_buf_addr_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_buf_length =
				dal_vndr_hal_tx_desc_set_buf_length_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_buf_offset =
				dal_vndr_hal_tx_desc_set_buf_offset_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_l3_checksum_en =
				dal_vndr_hal_tx_desc_set_l3_checksum_en_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_l4_checksum_en =
				dal_vndr_hal_tx_desc_set_l4_checksum_en_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_bank_id =
				dal_vndr_hal_tx_desc_set_bank_id_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_vdev_id =
				dal_vndr_hal_tx_desc_set_vdev_id_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_hlos_tid =
				dal_vndr_hal_tx_desc_set_hlos_tid_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_flow_override_enable =
			dal_vndr_hal_tx_desc_set_flow_override_enable_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_flow_override =
				dal_vndr_hal_tx_desc_set_flow_override_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_who_classify_info_sel =
			dal_vndr_hal_tx_desc_set_who_classify_info_sel_be;
	hal_soc->ops->dal_vndr_hal_tx_desc_set_tx_notify_frame =
				dal_vndr_hal_tx_desc_set_tx_notify_frame_be;
	hal_soc->ops->dal_vndr_hal_tx_comp_get_status =
				dal_vndr_hal_tx_comp_get_status_generic_be;
	hal_soc->ops->dal_vndr_hal_tx_comp_get_paddr =
				dal_vndr_hal_tx_comp_get_paddr_be;
	hal_soc->ops->dal_vndr_hal_rx_error_status_get =
				dal_vndr_hal_rx_error_status_get_be;
	hal_soc->ops->dal_vndr_hal_rx_reo_buf_cookie_get =
				dal_vndr_hal_rx_reo_buf_cookie_get_be;
	hal_soc->ops->dal_vndr_hal_rx_ret_buf_manager_get =
				dal_vndr_hal_rx_ret_buf_manager_get_be;
	hal_soc->ops->dal_vndr_hal_rx_reo_get_details =
				dal_vndr_hal_rx_reo_get_details_be;
	hal_soc->ops->dal_vndr_hal_rx_reo_buf_paddr_get =
				dal_vndr_hal_rx_reo_buf_paddr_get_be;
	hal_soc->ops->dal_vndr_hal_rx_msdu_start_nss_get =
				dal_vndr_hal_rx_tlv_nss_get_be;
	hal_soc->ops->dal_vndr_hal_rx_tlv_sgi_get =
				dal_vndr_hal_rx_tlv_sgi_get_be;
	hal_soc->ops->dal_vndr_hal_rx_tlv_rate_mcs_get =
				dal_vndr_hal_rx_tlv_rate_mcs_get_be;
	hal_soc->ops->dal_vndr_hal_rx_tlv_bw_get =
				dal_vndr_hal_rx_tlv_bw_get_be;
	hal_soc->ops->dal_vndr_hal_rx_tlv_get_pkt_type =
				dal_vndr_hal_rx_tlv_get_pkt_type_be;
	hal_soc->ops->dal_vndr_hal_rx_get_l3_pad_bytes =
				dal_vndr_hal_rx_get_l3_pad_bytes_be;
	hal_soc->ops->dal_vndr_hal_rx_tlv_msdu_done_get =
				dal_vndr_hal_rx_tlv_msdu_done_get_be;
	hal_soc->ops->dal_vndr_hal_rx_msdu_desc_info_get =
				dal_vndr_hal_rx_msdu_desc_info_get_be;
	hal_soc->ops->dal_vndr_hal_rxdma_buff_addr_info_set =
				dal_vndr_hal_rxdma_buff_addr_info_set_be;
	hal_soc->ops->dal_vndr_hal_rxbm_sync =
				dal_vndr_hal_rxbm_sync_be;
}
