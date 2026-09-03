/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#ifndef __DPTX_REG_H__
#define __DPTX_REG_H__

/* Constants */
#define DPTX_MP_SINGLE_PIXEL		0
#define DPTX_MP_DUAL_PIXEL		1
#define DPTX_MP_QUAD_PIXEL		2

#define DPTX_ID_DEVICE_ID		0x9001U
#define DPTX_ID_VENDOR_ID		0x16c3U

/* MST */
#define DPTX_MST_VCP_TABLE_REG_N(n)	(0x210 + (n) * 4)

//#define DP_DPRX_ESI_LEN 14

#define DPTX_HPDSTS	0xD08

/* Video Registers. N=0-3 */
#define DPTX_VSAMPLE_CTRL_N(n)		(0x300  + 0x10000 * (n))
#define DPTX_VSAMPLE_RESERVED1_N(n)	(0x304  + 0x10000 * (n))
#define DPTX_VSAMPLE_RESERVED2_N(n)	(0x308  + 0x10000 * (n))
#define DPTX_VSAMPLE_POLARITY_CTRL_N(n)	(0x30C  + 0x10000 * (n))
#define DPTX_VIDEO_CONFIG1_N(n)		(0x310  + 0x10000 * (n))
#define DPTX_VIDEO_CONFIG2_N(n)		(0x314  + 0x10000 * (n))
#define DPTX_VIDEO_CONFIG3_N(n)		(0x318  + 0x10000 * (n))
#define DPTX_VIDEO_CONFIG4_N(n)		(0x31c  + 0x10000 * (n))
#define DPTX_VIDEO_CONFIG5_N(n)		(0x320  + 0x10000 * (n))
#define DPTX_VIDEO_MSA1_N(n)		(0x324  + 0x10000 * (n))
#define DPTX_VIDEO_MSA2_N(n)		(0x328  + 0x10000 * (n))
#define DPTX_VIDEO_MSA3_N(n)		(0x32C  + 0x10000 * (n))
#define DPTX_VG_CONFIG1_N(n)		(0x3804 + 0x10000 * (n))
#define DPTX_VG_CONFIG2_N(n)		(0x3808 + 0x10000 * (n))
#define DPTX_VG_CONFIG3_N(n)		(0x380C + 0x10000 * (n))
#define DPTX_VG_CONFIG4_N(n)		(0x3810 + 0x10000 * (n))
#define DPTX_VG_CONFIG5_N(n)		(0x3814 + 0x10000 * (n))
#define DPTX_VG_SWRST_N(n)		(0x3800 + 0x10000 * (n))

/* RAM Registers */

#define DPTX_VG_RAM_ADDR_N(n)		(0x381C + 0x10000 * (n))
#define DPTX_VG_WRT_RAM_CTR_N(n)	(0x3820 + 0x10000 * (n))
#define DPTX_VG_WRT_RAM_DATA_N(n)	(0x3824 + 0x10000 * (n))

#define DPTX_VG_RAM_ADDR_START_SHIFT	12
#define DPTX_VG_RAM_ADDR_START_MASK	GENMASK(12, 0)
#define DPTX_VG_RAM_CTR_START_SHIFT	0
#define DPTX_VG_RAM_CTR_START_MASK	BIT(0)
#define DPTX_VG_WRT_RAM_DATA_SHIFT	0
#define DPTX_VG_WRT_RAM_DATA_MASK	GENMASK(7, 0)

/* Register Bitfields */
#define DPTX_ID_DEVICE_ID_SHIFT		16
#define DPTX_ID_DEVICE_ID_MASK		GENMASK(31, 16)
#define DPTX_ID_VENDOR_ID_SHIFT		0
#define DPTX_ID_VENDOR_ID_MASK		GENMASK(15, 0)

#define DPTX_CONFIG1_MP_MODE_SINGLE	1
#define DPTX_CONFIG1_MP_MODE_DUAL	2
#define DPTX_CONFIG1_MP_MODE_QUAD	4

#define DPTX_CCTL_ENH_FRAME_EN		BIT(1)
#define DPTX_CCTL_ENABLE_MST_MODE	BIT(25)

#define DPTX_SRST_CTRL_CONTROLLER	BIT(0)
#define DPTX_SRST_CTRL_PHY		BIT(1)
#define DPTX_SRST_CTRL_HDCP		BIT(2)
#define DPTX_SRST_CTRL_AUDIO_SAMPLER	BIT(3)
#define DPTX_SRST_CTRL_AUX		BIT(4)
#define DPTX_SRST_VIDEO_RESET_N(n)	BIT(5 + n)
#define DPTX_SRST_CTRL_ALL (DPTX_SRST_CTRL_CONTROLLER |		\
			    DPTX_SRST_CTRL_HDCP |		\
			    DPTX_SRST_CTRL_AUDIO_SAMPLER |	\
			    DPTX_SRST_CTRL_AUX)

#define DPTX_PHYIF_CTRL_TPS_NONE		0
#define DPTX_PHYIF_CTRL_TPS_1			1
#define DPTX_PHYIF_CTRL_TPS_2			2
#define DPTX_PHYIF_CTRL_TPS_3			3
#define DPTX_PHYIF_CTRL_TPS_4			4
#define DPTX_PHYIF_CTRL_TPS_SYM_ERM		5
#define DPTX_PHYIF_CTRL_TPS_PRBS7		6
#define DPTX_PHYIF_CTRL_TPS_CUSTOM80		7
#define DPTX_PHYIF_CTRL_TPS_CP2520_1		8
#define DPTX_PHYIF_CTRL_TPS_CP2520_2		9
#define DPTX_PHYIF_CTRL_RATE_RBR		0x0
#define DPTX_PHYIF_CTRL_RATE_HBR		0x1
#define DPTX_PHYIF_CTRL_RATE_HBR2		0x2
#define DPTX_PHYIF_CTRL_RATE_HBR3		0x3
#define DPTX_PHYIF_CTRL_RATE_MASK		GENMASK(5, 4)
#define DPTX_PHYIF_CTRL_RATE_SHIFT		4
#define DPTX_PHYIF_CTRL_RATE_VAL(rate)		(rate << DPTX_PHYIF_CTRL_RATE_SHIFT)
#define DPTX_PHYIF_CTRL_LANES_MASK		GENMASK(7, 6)
#define DPTX_PHYIF_CTRL_LANES_SHIFT		6
#define DPTX_PHYIF_CTRL_LANES_VAL(lanes)	((lanes >> 1) << DPTX_PHYIF_CTRL_LANES_SHIFT)
#define DPTX_PHYIF_CTRL_XMIT_EN(lane)		BIT(8 + lane)
#define DPTX_PHYIF_CTRL_BUSY(lane)		BIT(12 + lane)
#define DPTX_PHYIF_CTRL_SSC_MASK		BIT(16)
#define DPTX_PHYIF_CTRL_SSC_SHIFT		16
#define DPTX_PHYIF_CTRL_SSC_VAL(ssc)		(ssc << DPTX_PHYIF_CTRL_SSC_SHIFT)
#define DPTX_PHYIF_CTRL_RATE_LANES_SSC_MASK	(DPTX_PHYIF_CTRL_RATE_MASK | \
						 DPTX_PHYIF_CTRL_LANES_MASK | \
						 DPTX_PHYIF_CTRL_SSC_MASK)

#define DPTX_POWER_DOWN_CTRL_LANE_MASK(lane)	(0x0000000F << (4 * lane))
#define DPTX_PHY_POWER_ON			0x0
#define DPTX_PHY_INTER_P2_POWER			0x2
#define DPTX_PHY_POWER_DOWN			0x3
#define DPTX_PHY_P4_POWER_STATE			0xc

#define DPTX_PHY_TX_EQ_PREEMP_SHIFT(lane)	(6 * lane)
#define DPTX_PHY_TX_EQ_PREEMP_MASK(lane)	GENMASK(6 * lane + 1, 6 * lane)
#define DPTX_PHY_TX_EQ_VSWING_SHIFT(lane)	(6 * lane + 2)
#define DPTX_PHY_TX_EQ_VSWING_MASK(lane)	GENMASK(6 * lane + 3, 6 * lane + 2)

#define DPTX_AUX_CMD_REQ_LEN_SHIFT	0
#define DPTX_AUX_CMD_REQ_LEN_MASK	GENMASK(3, 0)
#define DPTX_AUX_CMD_I2C_ADDR_ONLY	BIT(4)
#define DPTX_AUX_CMD_ADDR_SHIFT		8
#define DPTX_AUX_CMD_ADDR_MASK		GENMASK(27, 8)
#define DPTX_AUX_CMD_TYPE_SHIFT		28
#define DPTX_AUX_CMD_TYPE_MASK		GENMASK(31, 28)
#define DPTX_AUX_CMD_TYPE_WRITE		0x0
#define DPTX_AUX_CMD_TYPE_READ		0x1
#define DPTX_AUX_CMD_TYPE_WSU		0x2
#define DPTX_AUX_CMD_TYPE_MOT		0x4
#define DPTX_AUX_CMD_TYPE_NATIVE	0x8

#define DPTX_AUX_STS_STATUS_SHIFT	4
#define DPTX_AUX_STS_STATUS_ACK		0x0
#define DPTX_AUX_STS_STATUS_NACK	0x1
#define DPTX_AUX_STS_STATUS_DEFER	0x2
#define DPTX_AUX_STS_STATUS_I2C_NACK	0x4
#define DPTX_AUX_STS_STATUS_I2C_DEFER	0x8

#define DPTX_ISTS_HPD			BIT(0)
#define DPTX_ISTS_AUX_REPLY		BIT(1)
#define DPTX_ISTS_HDCP			BIT(2)
#define DPTX_ISTS_AUX_CMD_INVALID	BIT(3)
#define DPTX_ISTS_SDP			BIT(4)
#define DPTX_ISTS_AUDIO_FIFO_OVERFLOW	BIT(5)
#define DPTX_ISTS_VIDEO_FIFO_OVERFLOW	BIT(6)
#define DPTX_ISTS_ALL_INTR	(DPTX_ISTS_HPD |			\
				 DPTX_ISTS_AUX_REPLY |			\
				 DPTX_ISTS_HDCP |			\
				 DPTX_ISTS_AUX_CMD_INVALID |		\
				 DPTX_ISTS_SDP |			\
				 DPTX_ISTS_AUDIO_FIFO_OVERFLOW |	\
				 DPTX_ISTS_VIDEO_FIFO_OVERFLOW)
#define DPTX_IEN_HPD			BIT(0)
#define DPTX_IEN_AUX_REPLY		BIT(1)
#define DPTX_IEN_HDCP			BIT(2)
#define DPTX_IEN_AUX_CMD_INVALID	BIT(3)
#define DPTX_IEN_SDP			BIT(4)
#define DPTX_IEN_AUDIO_FIFO_OVERFLOW	BIT(5)
#define DPTX_IEN_VIDEO_FIFO_OVERFLOW	BIT(6)
#define DPTX_IEN_ALL_INTR	(DPTX_IEN_HPD |			\
				 DPTX_IEN_AUX_REPLY |		\
				 DPTX_IEN_HDCP |		\
				 DPTX_IEN_AUX_CMD_INVALID |	\
				 DPTX_IEN_SDP)

#define DPTX_HPDSTS_IRQ			BIT(0)
#define DPTX_HPDSTS_HOT_PLUG		BIT(1)
#define DPTX_HPDSTS_HOT_UNPLUG		BIT(2)

#define DPTX_HPD_IEN_IRQ_EN		DPTX_HPDSTS_IRQ
#define DPTX_HPD_IEN_HOT_PLUG_EN	DPTX_HPDSTS_HOT_PLUG
#define DPTX_HPD_IEN_HOT_UNPLUG_EN	DPTX_HPDSTS_HOT_UNPLUG

#define DPTX_VSAMPLE_CTRL_STREAM_EN	BIT(5)

#define DPTX_AUD_CONFIG1_DATA_EN_IN_SHIFT		1
#define DPTX_AUD_CONFIG1_DATA_EN_IN_MASK		GENMASK(4, 1)

#define DPTX_VSAMPLE_CTRL_VMAP_BPC_SHIFT		16
#define DPTX_VSAMPLE_CTRL_VMAP_BPC_MASK			GENMASK(20, 16)
#define DPTX_VSAMPLE_CTRL_MULTI_PIXEL_SHIFT		21
#define DPTX_VSAMPLE_CTRL_MULTI_PIXEL_MASK		GENMASK(22, 21)
#define DPTX_VIDEO_VMSA2_BPC_SHIFT			29
#define DPTX_VIDEO_VMSA2_BPC_MASK			GENMASK(31, 29)
#define DPTX_VIDEO_VMSA2_COL_SHIFT			25
#define DPTX_VIDEO_VMSA2_COL_MASK			GENMASK(28, 25)
#define DPTX_VIDEO_VMSA3_PIX_ENC_SHIFT			31
#define DPTX_VIDEO_VMSA3_PIX_ENC_YCBCR420_SHIFT		30 // ignore MSA
#define DPTX_VIDEO_VMSA3_PIX_ENC_MASK			GENMASK(31, 30)
#define DPTX_POL_CTRL_V_SYNC_POL_EN			BIT(0)
#define DPTX_POL_CTRL_H_SYNC_POL_EN			BIT(1)
#define DPTX_VIDEO_CONFIG1_IN_OSC_EN			BIT(0)
#define DPTX_VIDEO_CONFIG1_O_IP_EN			BIT(1)
#define DPTX_VIDEO_H_BLANK_SHIFT			2
#define DPTX_VIDEO_H_ACTIVE_SHIFT			16
#define DPTX_VIDEO_V_BLANK_SHIFT			16
#define DPTX_VIDEO_V_ACTIVE_SHIFT			0
#define DPTX_VIDEO_H_FRONT_PORCH			0
#define DPTX_VIDEO_H_SYNC_WIDTH				16
#define DPTX_VIDEO_V_FRONT_PORCH			0
#define DPTX_VIDEO_V_SYNC_WIDTH				16
#define DPTX_VIDEO_MSA1_H_START_SHIFT			0
#define DPTX_VIDEO_MSA1_V_START_SHIFT			16
#define DPTX_VIDEO_CONFIG5_TU_SHIFT			0
#define DPTX_VIDEO_CONFIG5_TU_MASK			GENMASK(6, 0)
#define DPTX_VIDEO_CONFIG5_TU_FRAC_SHIFT_MST		14
#define DPTX_VIDEO_CONFIG5_TU_FRAC_MASK_MST		GENMASK(19, 14)
#define DPTX_VIDEO_CONFIG5_TU_FRAC_SHIFT_SST		16
#define DPTX_VIDEO_CONFIG5_TU_FRAC_MASK_SST		GENMASK(19, 16)
#define DPTX_VIDEO_CONFIG5_INIT_THRESHOLD_SHIFT		7
#define DPTX_VIDEO_CONFIG5_INIT_THRESHOLD_MASK		GENMASK(13, 7)

#define DPTX_VG_CONFIG1_BPC_SHIFT			12
#define DPTX_VG_CONFIG1_BPC_MASK			GENMASK(14, 12)
#define DPTX_VG_CONFIG1_PATTERN_SHIFT			17
#define DPTX_VG_CONFIG1_PATTERN_MASK			GENMASK(18, 17)
#define DPTX_VG_CONFIG1_MULTI_PIXEL_SHIFT		19
#define DPTX_VG_CONFIG1_MULTI_PIXEL_MASK		GENMASK(20, 19)
#define DPTX_VG_CONFIG1_ODE_POL_EN			BIT(0)
#define DPTX_VG_CONFIG1_OH_SYNC_POL_EN			BIT(1)
#define DPTX_VG_CONFIG1_OV_SYNC_POL_EN			BIT(2)
#define DPTX_VG_CONFIG1_OIP_EN				BIT(3)
#define DPTX_VG_CONFIG1_BLANK_IN_OSC_EN			BIT(5)
#define DPTX_VG_CONFIG1_YCC_422_EN			BIT(6)
#define DPTX_VG_CONFIG1_YCC_PATTERN_GEN_EN		BIT(7)
#define DPTX_VG_CONFIG1_YCC_420_EN			BIT(15)
#define DPTX_VG_CONFIG2_H_ACTIVE_SHIFT			0
#define DPTX_VG_CONFIG2_H_BLANK_SHIFT			16

#define DPTX_EN_AUDIO_CH_1				1
#define DPTX_EN_AUDIO_CH_2				1
#define DPTX_EN_AUDIO_CH_3				3
#define DPTX_EN_AUDIO_CH_4				9
#define DPTX_EN_AUDIO_CH_5				7
#define DPTX_EN_AUDIO_CH_6				7
#define DPTX_EN_AUDIO_CH_7				0xF
#define DPTX_EN_AUDIO_CH_8				0xF

#define DPTX_HDCP22GPIOSTS				0x3628
#define DPTX_HDCP22GPIOOUTCHNGSTS			0x362c

//-----------------------------------------
// NEW REGMAPS ARCHITECTURE
//-----------------------------------------

#define DPTX_VERSION_NUMBER                                         0x0000
#define DPTX_VERSION_TYPE                                           0x0004
#define DPTX_ID                                                     0x0008
#define DPTX_CONFIG_REG1                                            0x0100
#define DPTX_CONFIG_REG3                                            0x0108
#define CCTL                                                        0x0200
#define SOFT_RESET_CTRL                                             0x0204
#define VSAMPLE_CTRL                                                0x0300
#define VSAMPLE_STUFF_CTRL1                                         0x0304
#define VSAMPLE_STUFF_CTRL2                                         0x0308
#define VINPUT_POLARITY_CTRL                                        0x030c
#define VIDEO_CONFIG1                                               0x0310
#define VIDEO_CONFIG2                                               0x0314
#define VIDEO_CONFIG3                                               0x0318
#define VIDEO_CONFIG4                                               0x031c
#define VIDEO_CONFIG5                                               0x0320
#define VIDEO_MSA1                                                  0x0324
#define VIDEO_MSA2                                                  0x0328
#define VIDEO_MSA3                                                  0x032c
#define VIDEO_HBLANK_INTERVAL                                       0x0330
#define MVID_CONFIG1                                                0x0338
#define MVID_CONFIG2                                                0x033c
#define AUD_CONFIG1                                                 0x0400
#define SDP_VERTICAL_CTRL                                           0x0500
#define SDP_HORIZONTAL_CTRL                                         0x0504
#define SDP_STATUS_REGISTER                                         0x0508
#define SDP_MANUAL_CTRL                                             0x050c
#define SDP_STATUS_EN                                               0x0510
#define SDP_CONFIG1                                                 0x0520
#define SDP_CONFIG2                                                 0x0524
#define SDP_CONFIG3                                                 0x0528
#define SDP_REGISTER_BANK_0                                         0x0600
#define SDP_REGISTER_BANK_1                                         0x0604
#define SDP_REGISTER_BANK_2                                         0x0608
#define SDP_REGISTER_BANK_3                                         0x060c
#define PHYIF_CTRL                                                  0x0a00
#define PHY_TX_EQ                                                   0x0a04
#define CUSTOMPAT0                                                  0x0a08
#define CUSTOMPAT1                                                  0x0a0c
#define CUSTOMPAT2                                                  0x0a10
#define HBR2_COMPLIANCE_SCRAMBLER_RESET                             0x0a14
#define PHYIF_PWRDOWN_CTRL					    0x0a18
#define AUX_CMD                                                     0x0b00
#define AUX_STATUS                                                  0x0b04
#define AUX_DATA0                                                   0x0b08
#define AUX_DATA1                                                   0x0b0c
#define AUX_DATA2                                                   0x0b10
#define AUX_DATA3                                                   0x0b14
#define AUX_250US_CNT_LIMIT                                         0x0b40
#define AUX_2000US_CNT_LIMIT                                        0x0b44
#define AUX_100000US_CNT_LIMIT                                      0x0b48
#define TYPEC_CTRL                                                  0x0c08
#define COMBO_PHY_CTRL1                                             0x0c0c
#define COMBO_PHY_STATUS1                                           0x0c10
#define COMBO_PHY_OVR                                               0x0c14
#define COMBO_PHY_OVR_MPLL_CTRL0                                    0x0c18
#define COMBO_PHY_OVR_MPLL_CTRL1                                    0x0c1c
#define COMBO_PHY_OVR_MPLL_CTRL2                                    0x0c20
#define COMBO_PHY_GEN2_OVR_MPLL_CTRL0                               0x0c24
#define COMBO_PHY_GEN2_OVR_MPLL_CTRL1                               0x0c28
#define COMBO_PHY_GEN2_OVR_MPLL_CTRL2                               0x0c2c
#define COMBO_PHY_GEN2_OVR_MPLL_CTRL3                               0x0c30
#define COMBO_PHY_GEN2_OVR_MPLL_CTRL4                               0x0c34
#define COMBO_PHY_GEN2_OVR_MPLL_CTRL5                               0x0c38
#define COMBO_PHY_OVR_TERM_CTRL                                     0x0c44
#define COMBO_PHY_OVR_TX_EQ_G1_CTRL                                 0x0c48
#define COMBO_PHY_OVR_TX_EQ_G2_CTRL                                 0x0c4c
#define COMBO_PHY_OVR_TX_EQ_G3_CTRL                                 0x0c50
#define COMBO_PHY_OVR_TX_EQ_G4_CTRL                                 0x0c54
#define COMBO_PHY_OVR_TX_EQ_G5_CTRL                                 0x0c58
#define COMBO_PHY_OVR_TX_EQ_G6_CTRL                                 0x0c5c
#define COMBO_PHY_OVR_TX_EQ_G7_CTRL                                 0x0c60
#define COMBO_PHY_OVR_TX_EQ_G8_CTRL                                 0x0c64
#define COMBO_PHY_OVR_TX_LANE0_CTRL                                 0x0c68
#define COMBO_PHY_OVR_TX_LANE1_CTRL                                 0x0c6c
#define COMBO_PHY_OVR_TX_LANE2_CTRL                                 0x0c70
#define COMBO_PHY_OVR_TX_LANE3_CTRL                                 0x0c74
#define GENERAL_INTERRUPT                                           0x0d00
#define GENERAL_INTERRUPT_ENABLE                                    0x0d04
#define HPD_STATUS                                                  0x0d08
#define HPD_INTERRUPT_ENABLE                                        0x0d0c

#if IS_ENABLED(CONFIG_SOC_LGA)
#define HDCPCFG                                                     0x0e00
#define HDCPOBS                                                     0x0e04
#define HDCPAPIINTCLR                                               0x0e08
#define HDCPAPIINTSTAT                                              0x0e0c
#define HDCPAPIINTMSK                                               0x0e10
#define HDCPKSVMEMCTRL                                              0x0e18
#define HDCPREG_BKSV0                                               0x3600
#define HDCPREG_BKSV1                                               0x3604
#define HDCPREG_ANCONF                                              0x3608
#define HDCPREG_AN0                                                 0x360c
#define HDCPREG_AN1                                                 0x3610
#define HDCPREG_RMLCTL                                              0x3614
#define HDCPREG_RMLSTS                                              0x3618
#define HDCPREG_SEED                                                0x361c
#define HDCPREG_DPK0                                                0x3620
#define HDCPREG_DPK1                                                0x3624
#define HDCP2GPIOSTS                                                0x3628
#define HDCP2GPIOCHNGSTS                                            0x362c
#define HDCPREG_DPK_CRC                                             0x3630
#else
#define HDCPCFG                                                     0x10e00
#define HDCPOBS                                                     0x10e04
#define HDCPAPIINTCLR                                               0x10e08
#define HDCPAPIINTSTAT                                              0x10e0c
#define HDCPAPIINTMSK                                               0x10e10
#define HDCPKSVMEMCTRL                                              0x10e18
#define HDCPREG_BKSV0                                               0x13600
#define HDCPREG_BKSV1                                               0x13604
#define HDCPREG_ANCONF                                              0x13608
#define HDCPREG_AN0                                                 0x1360c
#define HDCPREG_AN1                                                 0x13610
#define HDCPREG_RMLCTL                                              0x13614
#define HDCPREG_RMLSTS                                              0x13618
#define HDCPREG_SEED                                                0x1361c
#define HDCPREG_DPK0                                                0x13620
#define HDCPREG_DPK1                                                0x13624
#define HDCP2GPIOSTS                                                0x13628
#define HDCP2GPIOCHNGSTS                                            0x1362c
#define HDCPREG_DPK_CRC                                             0x13630
#endif

#define VG_SWRST                                                    0x3800
#define VG_CONFIG1                                                  0x3804
#define VG_CONFIG2                                                  0x3808
#define VG_CONFIG3                                                  0x380c
#define VG_CONFIG4                                                  0x3810
#define VG_CONFIG5                                                  0x3814
#define VG_CONFIG6                                                  0x3818
#define VG_RAM_ADDR                                                 0x381c
#define VG_WRT_RAM_CTRL                                             0x3820
#define VG_WRT_RAM_DATA                                             0x3824
#define VG_WRT_RAM_STOP_ADDR                                        0x3828
#define VG_CB_PATTERN_CONFIG                                        0x382c
#define VG_CB_COLOR_A_1                                             0x3830
#define VG_CB_COLOR_A_2                                             0x3834
#define VG_CB_COLOR_B_1                                             0x3838
#define VG_CB_COLOR_B_2                                             0x383c
#define AG_SWRSTZ                                                   0x3900
#define AG_CONFIG1                                                  0x3904
#define AG_CONFIG2                                                  0x3908
#define AG_CONFIG3                                                  0x390c
#define AG_CONFIG4                                                  0x3910
#define AG_CONFIG5                                                  0x3914
#define AG_CONFIG6                                                  0x3918
#define PM_CONFIG1						    0x0350
#define PM_CONFIG2						    0x0354
#define PM_CTRL1						    0x0360
#define PM_STS1							    0x0370

#endif
