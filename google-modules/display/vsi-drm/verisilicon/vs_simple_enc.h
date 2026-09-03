/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_SIMPLE_ENC_H_
#define __VS_SIMPLE_ENC_H_

#define VS_SIMPLE_ENC_OUTPUT_ID_MASK 0xFF00
#define VS_SIMPLE_ENC_ENCODER_ID_MASK 0x00FF

#define VS_SIMPLE_ENC_MUX_ID(output_id, encoder_id) (((output_id) << 8) | (encoder_id))

#define VS_SIMPLE_ENC_OUTPUT_ID(mux_id) (((mux_id) & VS_SIMPLE_ENC_OUTPUT_ID_MASK) >> 8)

#define VS_SIMPLE_ENC_ENCODER_ID(mux_id) ((mux_id) & VS_SIMPLE_ENC_ENCODER_ID_MASK)

struct simple_encoder_priv {
	u8 encoder_type;
	u32 output_mode;
	u32 max_w;
	u32 max_h;
};

struct simple_encoder {
	struct drm_encoder encoder;
	u16 mux_id;
	struct device *dev;
	const struct simple_encoder_priv *priv;
};

extern struct platform_driver simple_encoder_driver;

struct drm_device;

#endif /* __VS_SIMPLE_ENC_H_ */
