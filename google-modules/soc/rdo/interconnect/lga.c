// SPDX-License-Identifier: GPL-2.0-only

#include <dt-bindings/interconnect/google,rdo.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "google_icc.h"
#include "google_icc_provider.h"
#include "google_irm_idx_internal.h"

enum {
	/* GMC */
	RDO_GMC = 0,
	RDO_SSWRP_APC,
	RDO_SSWRP_DPU,
	RDO_SSWRP_PCIE_0,
	RDO_SSWRP_PCIE_1,
	RDO_SSWRP_UFS,
	RDO_SSWRP_USB,
	RDO_SSWRP_ISP_SET_0,
	RDO_SSWRP_ISP_SET_1,
	RDO_SSWRP_EH,
	RDO_SSWRP_GPCA,
	RDO_SSWRP_GPU,
	RDO_SSWRP_CODEC_3P,
	RDO_SSWRP_G2D,
	RDO_SSWRP_GSW,
	RDO_SSWRP_GSA,
	/* GSLC */
	RDO_GSLC,
	RDO_SSWRP_CPU_GSLC,
	RDO_SSWRP_DPU_GSLC,
	RDO_SSWRP_PCIE_0_GSLC,
	RDO_SSWRP_PCIE_1_GSLC,
	RDO_SSWRP_UFS_GSLC,
	RDO_SSWRP_USB_GSLC,
	RDO_SSWRP_ISP_SET_0_GSLC,
	RDO_SSWRP_ISP_SET_1_GSLC,
	RDO_SSWRP_EH_GSLC,
	RDO_SSWRP_GPCA_GSLC,
	RDO_SSWRP_GPU_GSLC,
	RDO_SSWRP_CODEC_3P_GSLC,
	RDO_SSWRP_G2D_GSLC,
	RDO_SSWRP_GSW_GSLC,
	RDO_SSWRP_GSA_GSLC,
	/* ISPFE */
	RDO_ISPFE,
	/* ISPFE - GSLC */
	RDO_ISPFE_GSLC,
	/* ISPBE */
	RDO_ISPBE_TNR_ALIGN,
	RDO_ISPBE_BE_BAYER_TNR,
	RDO_ISPBE_BE_YUV,
	/* ISPBE - GSLC */
	RDO_ISPBE_TNR_ALIGN_GSLC,
	RDO_ISPBE_BE_BAYER_TNR_GSLC,
	RDO_ISPBE_BE_YUV_GSLC,
	/* GSW */
	RDO_GSW_GSE,
	RDO_GSW_GWE,
	/* GSW - GSLC */
	RDO_GSW_GSE_GSLC,
	RDO_GSW_GWE_GSLC,
};

DEFINE_GNODE(gmc, RDO_GMC, TYPE_GMC, 0);
DEFINE_GNODE(sswrp_apc, RDO_SSWRP_APC, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_APC), RDO_GMC);
DEFINE_GNODE(sswrp_dpu, RDO_SSWRP_DPU, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_DPU), RDO_GMC);
DEFINE_GNODE(sswrp_pcie_0, RDO_SSWRP_PCIE_0, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_PCIE_0), RDO_GMC);
DEFINE_GNODE(sswrp_pcie_1, RDO_SSWRP_PCIE_1, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_PCIE_1), RDO_GMC);
DEFINE_GNODE(sswrp_ufs, RDO_SSWRP_UFS, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_UFS), RDO_GMC);
DEFINE_GNODE(sswrp_usb, RDO_SSWRP_USB, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_USB), RDO_GMC);
DEFINE_GNODE(sswrp_isp_set_0, RDO_SSWRP_ISP_SET_0, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_ISP_SET_0), RDO_GMC);
DEFINE_GNODE(sswrp_isp_set_1, RDO_SSWRP_ISP_SET_1, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_ISP_SET_1), RDO_GMC);
DEFINE_GNODE(sswrp_eh, RDO_SSWRP_EH, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_EH), RDO_GMC);
DEFINE_GNODE(sswrp_gpca, RDO_SSWRP_GPCA, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_GPCA), RDO_GMC);
DEFINE_GNODE(sswrp_gpu, RDO_SSWRP_GPU, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_GPU), RDO_GMC);
DEFINE_GNODE(sswrp_codec_3p, RDO_SSWRP_CODEC_3P, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_CODEC_3P), RDO_GMC);
DEFINE_GNODE(sswrp_g2d, RDO_SSWRP_G2D, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_G2D), RDO_GMC);
DEFINE_GNODE(sswrp_gsw, RDO_SSWRP_GSW, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_GSW), RDO_GMC);
DEFINE_GNODE(sswrp_gsa, RDO_SSWRP_GSA, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_TYPE_GSLC | IRM_IDX_GSA), RDO_GMC);

DEFINE_GNODE(ispfe, RDO_ISPFE, TYPE_SOURCE, 0, RDO_SSWRP_ISP_SET_0);

DEFINE_GNODE(ispbe_tnr_align, RDO_ISPBE_TNR_ALIGN, TYPE_SOURCE, 0,
	     RDO_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_be_bayer_tnr, RDO_ISPBE_BE_BAYER_TNR, TYPE_SOURCE, 0,
	     RDO_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_be_yuv, RDO_ISPBE_BE_YUV, TYPE_SOURCE, 0,
	     RDO_SSWRP_ISP_SET_1);

DEFINE_GNODE(gsw_gse, RDO_GSW_GSE, TYPE_SOURCE, 0, RDO_SSWRP_GSW);
DEFINE_GNODE(gsw_gwe, RDO_GSW_GWE, TYPE_SOURCE, 0, RDO_SSWRP_GSW);

static struct google_icc_node *const gmc_nodes[] = {
	[GMC] = &gmc,
	[SSWRP_APC] = &sswrp_apc,
	[SSWRP_DPU] = &sswrp_dpu,
	[SSWRP_PCIE_0] = &sswrp_pcie_0,
	[SSWRP_PCIE_1] = &sswrp_pcie_1,
	[SSWRP_UFS] = &sswrp_ufs,
	[SSWRP_USB] = &sswrp_usb,
	[SSWRP_ISP_SET_0] = &sswrp_isp_set_0,
	[SSWRP_ISP_SET_1] = &sswrp_isp_set_1,
	[SSWRP_EH] = &sswrp_eh,
	[SSWRP_GPCA] = &sswrp_gpca,
	[SSWRP_GPU] = &sswrp_gpu,
	[SSWRP_CODEC_3P] = &sswrp_codec_3p,
	[SSWRP_G2D] = &sswrp_g2d,
	[SSWRP_GSW] = &sswrp_gsw,
	[SSWRP_GSA] = &sswrp_gsa,
};

static const struct google_icc_desc rdo_gmc = {
	.nodes = gmc_nodes,
	.num_nodes = ARRAY_SIZE(gmc_nodes),
};

static struct google_icc_node *const rdo_ispfe_nodes[] = {
	[ISPFE] = &ispfe,
};

static const struct google_icc_desc rdo_ispfe = {
	.nodes = rdo_ispfe_nodes,
	.num_nodes = ARRAY_SIZE(rdo_ispfe_nodes),
};

static struct google_icc_node *const rdo_ispbe_nodes[] = {
	[ISPBE_TNR_ALIGN] = &ispbe_tnr_align,
	[ISPBE_BE_BAYER_TNR] = &ispbe_be_bayer_tnr,
	[ISPBE_BE_YUV] = &ispbe_be_yuv,
};

static const struct google_icc_desc rdo_ispbe = {
	.nodes = rdo_ispbe_nodes,
	.num_nodes = ARRAY_SIZE(rdo_ispbe_nodes),
};

static struct google_icc_node *const rdo_gsw_nodes[] = {
	[GSW_GSE] = &gsw_gse,
	[GSW_GWE] = &gsw_gwe,
};

static const struct google_icc_desc rdo_gsw = {
	.nodes = rdo_gsw_nodes,
	.num_nodes = ARRAY_SIZE(rdo_gsw_nodes),
};

static const struct of_device_id google_icc_of_match_table[] = {
	{ .compatible = "google,icc-gmc", .data = &rdo_gmc },
	{ .compatible = "google,icc-ispfe", .data = &rdo_ispfe },
	{ .compatible = "google,icc-ispbe", .data = &rdo_ispbe },
	{ .compatible = "google,icc-gsw", .data = &rdo_gsw },
	{}
};
MODULE_DEVICE_TABLE(of, google_icc_of_match_table);

static struct platform_driver google_icc_platform_driver = {
	.probe = google_icc_platform_probe,
	.remove = google_icc_platform_remove,
	.driver = {
		.name = "google-icc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_icc_of_match_table),
	},
};
module_platform_driver(google_icc_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google RDO interconnect driver");
MODULE_LICENSE("GPL");
