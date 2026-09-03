/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

void dptx_set_vcp_allocation(struct dptx *dptx, int stream, int vc_payload_size);
void dptx_clear_vcpid_table(struct dptx *dptx);
void dptx_set_vcpid_table_slot(struct dptx *dptx, u32 slot, u32 stream);
void dptx_set_vcpid_table_range(struct dptx *dptx, u32 start, u32 count, u32 stream);
void dptx_dpcd_clear_vcpid_table(struct dptx *dptx);
void dptx_dpcd_set_vcpid_table(struct dptx *dptx, u32 start, u32 count, u32 stream);
int dptx_remove_stream_vcpid_table(struct dptx *dptx, int stream);
void dptx_print_vcpid_table(struct dptx *dptx);
void dptx_initiate_mst_act(struct dptx *dptx);
int print_buf(u8 *buf, int len);
