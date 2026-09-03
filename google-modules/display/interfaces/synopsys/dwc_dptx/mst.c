// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "regmaps/ctrl_fields.h"

void dptx_set_vcp_allocation(struct dptx *dptx, int stream, int vc_payload_size)
{
	int i;
	int n, shift_position;
	u32 reg = 0;

	n = dptx->active_mst_vc_payload / 8;
	shift_position = dptx->active_mst_vc_payload % 8;
	reg = dptx_read_reg(dptx, dptx->regs[DPTX], DPTX_MST_VCP_TABLE_REG_N(n));

	for (i = 0; i < vc_payload_size; i++) {
		reg |= (stream) << shift_position * 4;
		dev_dbg(dptx->dev, "%s: slot %d reserved: reg %d\n", __func__, shift_position, n);
		shift_position++;
		if (shift_position == 8) {
			dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_MST_VCP_TABLE_REG_N(n), reg);
			dev_dbg(dptx->dev, "%s: MST_VCP_REG %d: %08X\n", __func__, n, reg);
			n++;
			shift_position = 0;
			reg = 0;
		}
	}
	dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_MST_VCP_TABLE_REG_N(n), reg);
	dev_dbg(dptx->dev, "%s: MST_VCP_REG %d: %08X\n", __func__, n, reg);

	dptx->active_mst_vc_payload += vc_payload_size;

	dptx_video_ts_change(dptx, stream);

	dev_dbg(dptx->dev, "----- %s: FINISHED STREAM VCP ALLOCATION - STREAM %d -----\n", __func__, stream);
}

void dptx_clear_vcpid_table(struct dptx *dptx)
{
	int i;

	for (i = 0; i < 8; i++)
		dptx_write_reg(dptx, dptx->regs[DPTX], DPTX_MST_VCP_TABLE_REG_N(i), 0);
}

void dptx_set_vcpid_table_slot(struct dptx *dptx, u32 slot, u32 stream)
{
	u32 offset;
	u32 reg;
	u32 lsb;
	u32 mask;

	if (WARN(slot > 63, "Invalid slot number > 63"))
		return;

	offset = DPTX_MST_VCP_TABLE_REG_N(slot >> 3);
	reg = dptx_read_reg(dptx, dptx->regs[DPTX], offset);

	lsb = (slot & 0x7) * 4;
	mask = GENMASK(lsb + 3, lsb);

	reg &= ~mask;
	reg |= (stream << lsb) & mask;

	dptx_dbg(dptx, "%s: Writing 0x%08x val=0x%08x\n", __func__, offset, reg);
	dptx_write_reg(dptx, dptx->regs[DPTX], offset, reg);
}

void dptx_set_vcpid_table_range(struct dptx *dptx, u32 start, u32 count, u32 stream)
{
	int i;

	if (WARN((start + count) > 64, "Invalid slot number > 63"))
		return;

	for (i = 0; i < count; i++) {
		dptx_dbg(dptx, "--------- %s: setting slot %d for stream %d\n",
			 __func__, start + i, stream);
		dptx_set_vcpid_table_slot(dptx, start + i, stream);
	}
}

void dptx_dpcd_clear_vcpid_table(struct dptx *dptx)
{
	u8 bytes[] = { 0x00, 0x00, 0x3f, };
	u8 status;
	int count = 0;

	dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
	dptx_write_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, DP_PAYLOAD_TABLE_UPDATED);
	dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);

	dptx_write_bytes_to_dpcd(dptx, DP_PAYLOAD_ALLOCATE_SET, bytes, 3);

	status = 0;
	while (!(status & DP_PAYLOAD_TABLE_UPDATED)) {
		dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
		count++;
		if (WARN(count > 2000, "Timeout waiting for DPCD VCPID table update\n"))
			break;

		usleep_range(900, 1100);
	}

	dptx_write_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, DP_PAYLOAD_TABLE_UPDATED);
	dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
}

void dptx_dpcd_set_vcpid_table(struct dptx *dptx, u32 start, u32 count, u32 stream)
{
	u8 bytes[3];
	u8 status = 0;
	int tries = 0;

	bytes[0] = stream;
	bytes[1] = start;
	bytes[2] = count;

	dptx_write_bytes_to_dpcd(dptx, DP_PAYLOAD_ALLOCATE_SET, bytes, 3);

	while (!(status & DP_PAYLOAD_TABLE_UPDATED)) {
		dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
		tries++;
		if (WARN(tries > 2000, "Timeout waiting for DPCD VCPID table update\n"))
			break;
	}

	dptx_write_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, DP_PAYLOAD_TABLE_UPDATED);
	dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
}

int dptx_remove_stream_vcpid_table(struct dptx *dptx, int stream)
{
	int time_slot;
	int start_slot = -1;
	int tries = 0;
	u8 bytes[64] = { 0, };
	u8 vc_id_info[3];
	u8 status = 0;

	dptx_read_bytes_from_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, bytes, 64);

	for (time_slot = 1; time_slot < 64; time_slot++) {
		if (bytes[time_slot] == stream) {
			start_slot = time_slot;
			break;
		}
	}

	if (start_slot < 0) {
		dptx_err(dptx, "ERROR: Stream %d wasn't defined\n", stream);
		return -EINVAL;
	}

	vc_id_info[0] = stream;
	vc_id_info[1] = start_slot;
	vc_id_info[2] = 0;	//To clear this stream VC Payload
	dptx_write_bytes_to_dpcd(dptx, DP_PAYLOAD_ALLOCATE_SET, vc_id_info, 3);

	while (!(status & DP_PAYLOAD_TABLE_UPDATED)) {
		dptx_read_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, &status);
		tries++;
		if (WARN(tries > 2000, "Timeout waiting for DPCD VCPID table update\n"))
			break;
	}

	return 0;
}

void dptx_print_vcpid_table(struct dptx *dptx)
{
	u8 bytes[64] = { 0, };
	int print_ret = 0;

	dptx_read_bytes_from_dpcd(dptx, DP_PAYLOAD_TABLE_UPDATE_STATUS, bytes, 64);
	print_ret = print_buf(bytes, 64);

	if (print_ret)
		dptx_err(dptx, "ERROR: Not possible to print VCP ID Table");
}

void dptx_initiate_mst_act(struct dptx *dptx)
{
	int count = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	dptx_write_regfield(dptx, ctrl_fields->field_initiate_mst_act_seq, 1);

	while (1) {
		if (!dptx_read_regfield(dptx, ctrl_fields->field_initiate_mst_act_seq))
			break;

		count++;
		if (WARN(count > 100, "CCTL.ACT timeout\n"))
			break;

		mdelay(10);
	}
}

int print_buf(u8 *buf, int len)
{
	int i;
	#define PRINT_BUF_SIZE 1024
	char str[PRINT_BUF_SIZE];
	int written = 0;

	written += snprintf(&str[written], PRINT_BUF_SIZE - written, "Buffer:");

	for (i = 0; i < len; i++) {
		if (!(i % 16)) {
			written += snprintf(&str[written],
					    PRINT_BUF_SIZE - written,
					    "\n%04x:", i);

			if (written >= PRINT_BUF_SIZE)
				break;
		}

		written += snprintf(&str[written],
				    PRINT_BUF_SIZE - written,
				    " %02x", buf[i]);

		if (written >= PRINT_BUF_SIZE)
			break;
	}

	return 0;
}
