/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */

#ifndef __DT_BINDINGS_INTERCONNECT_GOOGLE_MBU_H
#define __DT_BINDINGS_INTERCONNECT_GOOGLE_MBU_H

/*
 * These macros are used as indexes in dtsi and platform specific source code to assemble
 * struct icc_onecell_data.
 */

/* clang-format off */

#define GMC			0
#define SSWRP_APC		1
#define SSWRP_DPU		2
#define SSWRP_PCIE_0		3
#define SSWRP_PCIE_1		4
#define SSWRP_UFS		5
#define SSWRP_USB		6
#define SSWRP_ISP_SET_0		7
#define SSWRP_ISP_SET_1		8
#define SSWRP_HSION		9
#define SSWRP_GPCA		10
#define SSWRP_GPU		11
#define SSWRP_CODEC_3P		12
#define SSWRP_G2D		13
#define SSWRP_GCV		14
#define SSWRP_GSA		15
#define SSWRP_DPA		16
#define SSWRP_JPU		17
#define SSWRP_VPU		18

#define ISPFE_TOP		0
#define ISPFE_CSISFE		1
#define ISPFE_CORE0		2
#define ISPFE_CORE1		3
#define ISPFE_CORE2		4

#define ISPBE_MSA_TNR		0
#define ISPBE_MSA_HDR		1
#define ISPBE_BTR		2
#define ISPBE_ITCC		3
#define ISPBE_YUV		4
#define ISPBE_PDMA		5
#define ISPBE_MSA_TNR_PDMA	6
#define ISPBE_MSA_HDR_PDMA	7
#define ISPBE_PIPE		8
#define ISPBE_MSA_TNR_PIPE	9
#define ISPBE_MSA_HDR_PIPE	10


#define GCV_GSE			0
#define GCV_GWE			1
#define GCV_CRE			2
#define GCV_GSW_PDMA		3
#define GCV_CRE_PDMA		4
#define GCV_GSW_PIPE		5
#define GCV_CRE_PIPE		6

/* clang-format on */

#endif // __DT_BINDINGS_INTERCONNECT_GOOGLE_MBU_H
