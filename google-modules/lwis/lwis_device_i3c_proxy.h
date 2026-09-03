/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS I3C Proxy Device Driver with I2C
 *
 * Copyright 2024 Google LLC.
 */

#ifndef LWIS_DEVICE_I3C_PROXY_H_
#define LWIS_DEVICE_I3C_PROXY_H_

struct lwis_i3c_ibi_config {
	uint32_t ibi_max_payload_len;
	uint32_t ibi_num_slots;
	bool ibi_configured_in_dt;
};

int lwis_i3c_proxy_device_init(void);
int lwis_i3c_proxy_device_deinit(void);

#endif /* LWIS_DEVICE_I3C_PROXY_H_ */
