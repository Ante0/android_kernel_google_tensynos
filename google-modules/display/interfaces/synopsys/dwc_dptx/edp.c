// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "include/dptx.h"
#include "include/edp.h"

/*
 *       ALPM - Advanced Link Power Management
 */

int alpm_is_available(struct dptx *dptx)
{
	int retval;
	u8 alpm_cap;

	retval = dptx_read_dpcd(dptx, RECEIVER_ALPM_CAPABILITIES, &alpm_cap);
	if (retval)
		return retval;
	dptx_dbg(dptx, "ALPM Availability: %lu\n", alpm_cap & BIT(0));
	return (alpm_cap & BIT(0));
}

int alpm_get_status(struct dptx *dptx)
{
	int retval;
	u8 alpm_cfg;
	struct edp_alpm alpm;

	alpm = dptx->alpm;

	if (alpm.status == NOT_AVAILABLE) {
		retval = NOT_AVAILABLE;
	} else {
		retval = dptx_read_dpcd(dptx, RECEIVER_ALPM_CONFIGURATIONS, &alpm_cfg);
		if (!retval)
			retval = alpm_cfg & BIT(0);
	}

	return retval;
}

int alpm_set_status(struct dptx *dptx, int value)
{
	int retval;
	u8 alpm_cfg;

	dptx_read_dpcd(dptx, RECEIVER_ALPM_CONFIGURATIONS, &alpm_cfg);
	if (value)
		alpm_cfg |= BIT(0);
	else
		alpm_cfg &= ~BIT(0);

	retval = dptx_write_dpcd(dptx, RECEIVER_ALPM_CONFIGURATIONS, alpm_cfg);
	if (value)
		dptx_dbg(dptx, "Setting ALPM Status to ENABLED\n");
	else
		dptx_dbg(dptx, "Setting ALPM Status to DISABLED\n");

	return retval;
}

int alpm_get_state(struct dptx *dptx)
{
	int pm_sts;

	pm_sts = dptx_read_reg(dptx, dptx->regs[DPTX], PM_STS1);
	return (pm_sts & BIT(0));
}

int alpm_set_state(struct dptx *dptx, int state)
{
	u32 reg = 0;
	u32 count;
	int retval = 0;

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], PM_CTRL1);
	if (state)
		reg |= BIT(0);
	else
		reg &= ~BIT(0);

	dptx_write_reg(dptx, dptx->regs[DPTX], PM_CTRL1, reg);

	reg = dptx_read_reg(dptx, dptx->regs[DPTX], PM_STS1);
	reg &= BIT(0);

	count = 0;
	while (reg != state) {
		count++;
		if (count > 2000) {
			dptx_err(dptx, "Timeout waiting for ALPM State Update\n");
			retval = -EINVAL;
			return retval;
		}

		usleep_range(900, 1100);
		reg = dptx_read_reg(dptx, dptx->regs[DPTX], PM_STS1);
		reg &= BIT(0);
	}

	if (state)
		dptx_dbg(dptx, "Setting ALPM State to POWERED-OFF\n");
	else
		dptx_dbg(dptx, "Setting ALPM State to POWERED-ON\n");

	return retval;
}

/*
 *       Adaptive-Sync
 */

int fill_as_sdp_header(struct dptx *dptx, struct adaptive_sync_sdp_data *sdp)
{
	struct sdp_header *header;

	header = &sdp->header;
	header->HB0 = 0x00;	//Packet ID - Stream 0
	header->HB1 = 0x22;	//Adaptive-sync SDP Type
	header->HB2 = 0x01;	//Adaptive-Synce Version 1
	header->HB3 = 0x09;	//Payload Size (Version 1) - 9 bytes

	return 0;
}

int fill_as_sdp(struct dptx *dptx, struct adaptive_sync_sdp_data *sdp, u8 adaptive_sync_mode)
{

	u8 *payload;

	payload = sdp->payload;

	payload[0] = adaptive_sync_mode;
	payload[1] = 0;
	payload[2] = 0;
	payload[3] = 50;
	payload[4] = 0;
	payload[5] = 0;
	payload[6] = 0;
	payload[7] = 0;
	payload[8] = 0;

	return 0;
}
