// SPDX-License-Identifier: GPL-2.0-only

#include <dt-bindings/interconnect/google,mbu.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "google_icc.h"
#include "google_icc_provider.h"
#include "google_irm_idx_internal.h"

/*
 * The macro indexes are used to identify google_icc_node internally, they can be any integer,
 * but for the sake of simplicity, we just use enum integers.
 */
enum {
	/* GMC */
	MBU_GMC = 0,
	MBU_SSWRP_APC,
	MBU_SSWRP_DPU,
	MBU_SSWRP_PCIE_0,
	MBU_SSWRP_PCIE_1,
	MBU_SSWRP_UFS,
	MBU_SSWRP_USB,
	MBU_SSWRP_ISP_SET_0,
	MBU_SSWRP_ISP_SET_1,
	MBU_SSWRP_HSION,
	MBU_SSWRP_GPCA,
	MBU_SSWRP_GPU,
	MBU_SSWRP_CODEC_3P,
	MBU_SSWRP_G2D,
	MBU_SSWRP_GCV,
	MBU_SSWRP_GSA,
	MBU_SSWRP_DPA,
	MBU_SSWRP_JPU,
	MBU_SSWRP_VPU,
	/* ISPFE */
	MBU_ISPFE_TOP,
	MBU_ISPFE_CSISFE,
	MBU_ISPFE_CORE0,
	MBU_ISPFE_CORE1,
	MBU_ISPFE_CORE2,
	/* ISPBE */
	MBU_ISPBE_MSA_TNR,
	MBU_ISPBE_MSA_HDR,
	MBU_ISPBE_BTR,
	MBU_ISPBE_ITCC,
	MBU_ISPBE_YUV,
	MBU_ISPBE_PDMA,
	MBU_ISPBE_MSA_TNR_PDMA,
	MBU_ISPBE_MSA_HDR_PDMA,
	MBU_ISPBE_PIPE,
	MBU_ISPBE_MSA_TNR_PIPE,
	MBU_ISPBE_MSA_HDR_PIPE,
	/* GCV */
	MBU_GCV_GSE,
	MBU_GCV_GWE,
	MBU_GCV_CRE,
	MBU_GCV_GSW_PDMA,
	MBU_GCV_CRE_PDMA,
	MBU_GCV_GSW_PIPE,
	MBU_GCV_CRE_PIPE,
};

DEFINE_GNODE(gmc, MBU_GMC, TYPE_GMC, 0);
DEFINE_GNODE(sswrp_apc, MBU_SSWRP_APC, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_APC), MBU_GMC);
DEFINE_GNODE(sswrp_dpu, MBU_SSWRP_DPU, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_DPU), MBU_GMC);
DEFINE_GNODE(sswrp_pcie_0, MBU_SSWRP_PCIE_0, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_PCIE_0), MBU_GMC);
DEFINE_GNODE(sswrp_pcie_1, MBU_SSWRP_PCIE_1, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_PCIE_1), MBU_GMC);
DEFINE_GNODE(sswrp_ufs, MBU_SSWRP_UFS, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_UFS), MBU_GMC);
DEFINE_GNODE(sswrp_usb, MBU_SSWRP_USB, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_USB), MBU_GMC);
DEFINE_GNODE(sswrp_isp_set_0, MBU_SSWRP_ISP_SET_0, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_ISP_SET_0), MBU_GMC);
DEFINE_GNODE(sswrp_isp_set_1, MBU_SSWRP_ISP_SET_1, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_ISP_SET_1), MBU_GMC);
DEFINE_GNODE(sswrp_hsion, MBU_SSWRP_HSION, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_HSION), MBU_GMC);
DEFINE_GNODE(sswrp_gpca, MBU_SSWRP_GPCA, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_GPCA), MBU_GMC);
DEFINE_GNODE(sswrp_gpu, MBU_SSWRP_GPU, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_GPU), MBU_GMC);
DEFINE_GNODE(sswrp_codec_3p, MBU_SSWRP_CODEC_3P, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_CODEC_3P), MBU_GMC);
DEFINE_GNODE(sswrp_g2d, MBU_SSWRP_G2D, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_G2D), MBU_GMC);
DEFINE_GNODE(sswrp_gcv, MBU_SSWRP_GCV, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_GCV), MBU_GMC);
DEFINE_GNODE(sswrp_gsa, MBU_SSWRP_GSA, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_GSA), MBU_GMC);
DEFINE_GNODE(sswrp_dpa, MBU_SSWRP_DPA, TYPE_SOURCE,
	     (IRM_TYPE_GMC | IRM_IDX_DPA), MBU_GMC);

DEFINE_GNODE(sswrp_jpu, MBU_SSWRP_JPU, TYPE_SOURCE, 0, MBU_SSWRP_CODEC_3P);
DEFINE_GNODE(sswrp_vpu, MBU_SSWRP_VPU, TYPE_SOURCE, 0, MBU_SSWRP_CODEC_3P);

DEFINE_GNODE(ispfe_top, MBU_ISPFE_TOP, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_0);
DEFINE_GNODE(ispfe_csisfe, MBU_ISPFE_CSISFE, TYPE_SOURCE, 0,
	     MBU_SSWRP_ISP_SET_0);
DEFINE_GNODE(ispfe_core0, MBU_ISPFE_CORE0, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_0);
DEFINE_GNODE(ispfe_core1, MBU_ISPFE_CORE1, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_0);
DEFINE_GNODE(ispfe_core2, MBU_ISPFE_CORE2, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_0);

DEFINE_GNODE(ispbe_msa_tnr, MBU_ISPBE_MSA_TNR, TYPE_SOURCE, 0,
	     MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_msa_hdr, MBU_ISPBE_MSA_HDR, TYPE_SOURCE, 0,
	     MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_btr, MBU_ISPBE_BTR, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_itcc, MBU_ISPBE_ITCC, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_yuv, MBU_ISPBE_YUV, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_pdma, MBU_ISPBE_PDMA, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_msa_tnr_pdma, MBU_ISPBE_MSA_TNR_PDMA, TYPE_SOURCE, 0,
	     MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_msa_hdr_pdma, MBU_ISPBE_MSA_HDR_PDMA, TYPE_SOURCE, 0,
	     MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_pipe, MBU_ISPBE_PIPE, TYPE_SOURCE, 0, MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_msa_tnr_pipe, MBU_ISPBE_MSA_TNR_PIPE, TYPE_SOURCE, 0,
	     MBU_SSWRP_ISP_SET_1);
DEFINE_GNODE(ispbe_msa_hdr_pipe, MBU_ISPBE_MSA_HDR_PIPE, TYPE_SOURCE, 0,
	     MBU_SSWRP_ISP_SET_1);

DEFINE_GNODE(gcv_gse, MBU_GCV_GSE, TYPE_SOURCE, 0, MBU_SSWRP_GCV);
DEFINE_GNODE(gcv_gwe, MBU_GCV_GWE, TYPE_SOURCE, 0, MBU_SSWRP_GCV);
DEFINE_GNODE(gcv_cre, MBU_GCV_CRE, TYPE_SOURCE, 0, MBU_SSWRP_GCV);
DEFINE_GNODE(gcv_gsw_pdma, MBU_GCV_GSW_PDMA, TYPE_SOURCE, 0, MBU_SSWRP_GCV);
DEFINE_GNODE(gcv_cre_pdma, MBU_GCV_CRE_PDMA, TYPE_SOURCE, 0, MBU_SSWRP_GCV);
DEFINE_GNODE(gcv_gsw_pipe, MBU_GCV_GSW_PIPE, TYPE_SOURCE, 0, MBU_SSWRP_GCV);
DEFINE_GNODE(gcv_cre_pipe, MBU_GCV_CRE_PIPE, TYPE_SOURCE, 0, MBU_SSWRP_GCV);

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
	[SSWRP_HSION] = &sswrp_hsion,
	[SSWRP_GPCA] = &sswrp_gpca,
	[SSWRP_GPU] = &sswrp_gpu,
	[SSWRP_CODEC_3P] = &sswrp_codec_3p,
	[SSWRP_G2D] = &sswrp_g2d,
	[SSWRP_GCV] = &sswrp_gcv,
	[SSWRP_GSA] = &sswrp_gsa,
	[SSWRP_DPA] = &sswrp_dpa,
	[SSWRP_JPU] = &sswrp_jpu,
	[SSWRP_VPU] = &sswrp_vpu,
};

static const struct google_icc_desc mbu_gmc = {
	.nodes = gmc_nodes,
	.num_nodes = ARRAY_SIZE(gmc_nodes),
};

static struct google_icc_node *const mbu_ispfe_nodes[] = {
	[ISPFE_TOP] = &ispfe_top,     [ISPFE_CSISFE] = &ispfe_csisfe,
	[ISPFE_CORE0] = &ispfe_core0, [ISPFE_CORE1] = &ispfe_core1,
	[ISPFE_CORE2] = &ispfe_core2,
};

static const struct google_icc_desc mbu_ispfe = {
	.nodes = mbu_ispfe_nodes,
	.num_nodes = ARRAY_SIZE(mbu_ispfe_nodes),
};

static struct google_icc_node *const mbu_ispbe_nodes[] = {
	[ISPBE_MSA_TNR] = &ispbe_msa_tnr,
	[ISPBE_MSA_HDR] = &ispbe_msa_hdr,
	[ISPBE_BTR] = &ispbe_btr,
	[ISPBE_ITCC] = &ispbe_itcc,
	[ISPBE_YUV] = &ispbe_yuv,
	[ISPBE_PDMA] = &ispbe_pdma,
	[ISPBE_MSA_TNR_PDMA] = &ispbe_msa_tnr_pdma,
	[ISPBE_MSA_HDR_PDMA] = &ispbe_msa_hdr_pdma,
	[ISPBE_PIPE] = &ispbe_pipe,
	[ISPBE_MSA_TNR_PIPE] = &ispbe_msa_tnr_pipe,
	[ISPBE_MSA_HDR_PIPE] = &ispbe_msa_hdr_pipe,
};

static const struct google_icc_desc mbu_ispbe = {
	.nodes = mbu_ispbe_nodes,
	.num_nodes = ARRAY_SIZE(mbu_ispbe_nodes),
};

static struct google_icc_node *const mbu_gcv_nodes[] = {
	[GCV_GSE] = &gcv_gse,		[GCV_GWE] = &gcv_gwe,
	[GCV_CRE] = &gcv_cre,		[GCV_GSW_PDMA] = &gcv_gsw_pdma,
	[GCV_CRE_PDMA] = &gcv_cre_pdma, [GCV_GSW_PIPE] = &gcv_gsw_pipe,
	[GCV_CRE_PIPE] = &gcv_cre_pipe,
};

static const struct google_icc_desc mbu_gcv = {
	.nodes = mbu_gcv_nodes,
	.num_nodes = ARRAY_SIZE(mbu_gcv_nodes),
};

static const struct of_device_id google_icc_of_match_table[] = {
	{ .compatible = "google,icc-gmc", .data = &mbu_gmc },
	{ .compatible = "google,icc-ispfe", .data = &mbu_ispfe },
	{ .compatible = "google,icc-ispbe", .data = &mbu_ispbe },
	{ .compatible = "google,icc-gcv", .data = &mbu_gcv },
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
MODULE_DESCRIPTION("Google MBU interconnect driver");
MODULE_LICENSE("GPL");
