/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "dptx_utils.h"

/*****************************************************************************
 *                                                                           *
 *                         AG Configuration Registers                        *
 *                                                                           *
 *****************************************************************************/

#define AB_SRC_SEL			0x0000
#define AB_CLK_DOMAIN_EN		0x0004
#define EXT_AG_CONFIG0			0x0010
#define AG_CONFIG0_ENABLE_MASK		0x00000001
#define AG_CONFIG0_LAYOUT_MASK		0x00000010
#define AG_CONFIG0_SAMPLE_FLAT_MASK	0x00000020
#define AG_CONFIG0_SPEAKER_ALLOC_MASK	0x0000FF00
#define EXT_AG_CONFIG1			0x0014
#define EXT_AG_CONFIG2			0x0018
#define AG_CONFIG2_UDATA_LEFT_MASK	0x00000001
#define AG_CONFIG2_UDATA_RIGHT_MASK	0x00000002
#define AG_CONFIG2_CS_COPYRIGHT_MASK	0x00000004
#define AG_CONFIG2_CS_LPCMMODE_MASK	0x00000070
#define AG_CONFIG2_CS_CATEGORY_MASK	0x0000FF00
#define EXT_AG_CONFIG3			0x001C
#define AG_CONFIG3_CS_SRC_NUMBER_MASK	0x0000000F
#define AG_CONFIG3_CS_CH_NUMBER_MASK	0x000000F0
#define AG_CONFIG3_CS_SAMP_FREQ_MASK	0x000F0000
#define AG_CONFIG3_CS_WORD_LEN_MASK	0x00F00000
#define AG_CONFIG3_CS_ORIG_FREQ_MASK	0x0F000000
#define AG_CONFIG3_CS_CLK_ACC_MASK	0x30000000
#define AG_CONFIG3_CS_CGMSA_MASK	0xC0000000
#define AP_CONFIG0			0x0020
#define AP_CONFIG0_PAO_EN_MASK		0x00000001
#define AP_CONFIG0_I2S_EN_MASK		0x00000002
#define AP_CONFIG0_SPDIF_EN_MASK	0x00000004
#define AP_CONFIG0_I2S_BPCUV_EN_MASK	0x00000010
#define AP_CONFIG1			0x0024
#define AP_CONFIG2			0x0028
#define AP_CONFIG3			0x002C
#define AP_STATUS1			0x0030
#define AP_STATUS2			0x0034
#define AG_TIMER_BASE			0x0040

#define STREAM_OFFSET(stream) (0x100 * stream)

static void _ag_get_sample_rate(u32 freq_hz, uint32_t *sf, uint32_t *tb);
void audio_generator_config(struct dptx *dptx, int stream);
void audio_generator_disable(struct dptx *dptx);
