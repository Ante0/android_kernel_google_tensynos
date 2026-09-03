/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */
#include "vs_dc_dsc_rc_tables.h"
#include <linux/errno.h>
#include <linux/kernel.h>

struct rc_table_config {
	u16 rc_buf_thresh[DSC_NUM_BUF_RANGES - 1];
	u8 rc_min_qp[DSC_NUM_BUF_RANGES];
	u8 rc_max_qp[DSC_NUM_BUF_RANGES];
	u8 rc_offset[DSC_NUM_BUF_RANGES];
};

#define RC_TABLE(bpc, bpp, color_space, thresh_arr, minqp_arr, maxqp_arr, offset_arr) \
	static const struct rc_table_config RC_TABLE_NAME(bpc, bpp, color_space) = {  \
		.rc_buf_thresh = thresh_arr,                                          \
		.rc_min_qp = minqp_arr,                                               \
		.rc_max_qp = maxqp_arr,                                               \
		.rc_offset = offset_arr,                                              \
	}
#define RC_TABLE_NAME(bpc, bpp, color_space) _##bpc##_##bpp##_##color_space
#define RC_TABLE_ADDR(bpc, bpp, color_space) (&RC_TABLE_NAME(bpc, bpp, color_space))
#define RC_BUF_THRESH(...)  \
	{                   \
		__VA_ARGS__ \
	}
#define RC_MINQP(...)       \
	{                   \
		__VA_ARGS__ \
	}
#define RC_MAXQP(...)       \
	{                   \
		__VA_ARGS__ \
	}
#define RC_OFFSET(...)      \
	{                   \
		__VA_ARGS__ \
	}
#define RC_TABLE_ID(bpc, bpp, color_space) (((bpc) << 24) | ((bpp) << 16) | (color_space))

RC_TABLE(8, 6, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 9, 12),
	 RC_MAXQP(4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(8, 6, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 7936,
		       8000),
	 RC_MINQP(0, 1, 3, 4, 5, 5, 6, 6, 7, 8, 9, 10, 10, 11, 13),
	 RC_MAXQP(4, 6, 8, 8, 9, 9, 9, 10, 11, 12, 12, 12, 12, 12, 14),
	 RC_OFFSET(0, -2, -2, -4, -6, -6, -6, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(8, 7, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 11),
	 RC_MAXQP(3, 4, 5, 6, 7, 7, 7, 8, 9, 9, 10, 10, 11, 11, 12),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12));

RC_TABLE(8, 8, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 10),
	 RC_MAXQP(2, 4, 5, 6, 7, 7, 7, 8, 8, 9, 9, 9, 9, 10, 11),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(8, 8, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 1, 1, 3, 3, 3, 3, 3, 4, 5, 5, 5, 8, 12),
	 RC_MAXQP(4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 10, 11, 11, 12, 13),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12));

RC_TABLE(8, 10, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 4, 4, 5, 7, 8),
	 RC_MAXQP(0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9),
	 RC_OFFSET(10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -10, -12, -12, -12));

RC_TABLE(8, 10, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 11),
	 RC_MAXQP(3, 4, 5, 6, 7, 7, 7, 8, 9, 9, 10, 10, 11, 11, 12),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12));

RC_TABLE(8, 12, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 10),
	 RC_MAXQP(2, 4, 5, 6, 7, 7, 7, 8, 8, 9, 9, 9, 9, 10, 11),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(8, 15, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 4, 4, 5, 7, 8),
	 RC_MAXQP(0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9),
	 RC_OFFSET(10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -10, -12, -12, -12));

RC_TABLE(10, 6, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 13, 16),
	 RC_MAXQP(8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(10, 6, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 7936,
		       8000),
	 RC_MINQP(0, 3, 7, 8, 9, 9, 10, 10, 11, 12, 13, 14, 14, 15, 17),
	 RC_MAXQP(8, 10, 12, 12, 13, 13, 13, 14, 15, 16, 16, 16, 16, 16, 18),
	 RC_OFFSET(0, -2, -2, -4, -6, -6, -6, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(10, 7, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 4, 5, 6, 7, 7, 7, 7, 7, 7, 9, 9, 9, 11, 15),
	 RC_MAXQP(7, 8, 9, 10, 11, 11, 11, 12, 13, 13, 14, 14, 15, 15, 16),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12));

RC_TABLE(10, 8, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 2, 3, 4, 6, 7, 7, 7, 7, 7, 9, 9, 9, 11, 14),
	 RC_MAXQP(2, 5, 7, 8, 9, 10, 11, 12, 12, 13, 13, 13, 13, 14, 15),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(10, 8, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 4, 5, 5, 7, 7, 7, 7, 7, 7, 9, 9, 9, 13, 16),
	 RC_MAXQP(8, 8, 9, 10, 11, 11, 11, 12, 13, 14, 14, 15, 15, 16, 17),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(10, 10, 422,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 2, 3, 4, 5, 5, 5, 6, 6, 7, 8, 8, 9, 11, 12),
	 RC_MAXQP(2, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 11, 11, 12, 13),
	 RC_OFFSET(10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -10, -12, -12, -12));

RC_TABLE(10, 10, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 4, 5, 6, 7, 7, 7, 7, 7, 7, 9, 9, 9, 11, 15),
	 RC_MAXQP(7, 8, 9, 10, 11, 11, 11, 12, 13, 13, 14, 14, 15, 15, 16),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -10, -12, -12, -12));

RC_TABLE(10, 12, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 2, 3, 4, 6, 7, 7, 7, 7, 7, 9, 9, 9, 11, 14),
	 RC_MAXQP(2, 5, 7, 8, 9, 10, 11, 12, 12, 13, 13, 13, 13, 14, 15),
	 RC_OFFSET(2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12));

RC_TABLE(10, 15, 444,
	 RC_BUF_THRESH(896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000,
		       8064),
	 RC_MINQP(0, 2, 3, 4, 5, 5, 5, 6, 6, 7, 8, 8, 9, 11, 12),
	 RC_MAXQP(2, 5, 5, 6, 6, 7, 7, 8, 9, 9, 10, 11, 11, 12, 13),
	 RC_OFFSET(10, 8, 6, 4, 2, 0, -2, -4, -6, -8, -10, -10, -12, -12, -12));

int vs_dc_dsc_update_rc_table(struct drm_dsc_config *dsc)
{
	const struct rc_table_config *table = NULL;
	/* Not support 420 */
	u32 id = RC_TABLE_ID(dsc->bits_per_component, dsc->bits_per_pixel,
			     dsc->native_422 ? 422 : 444);
	int i;

#define MATCH_RC_TABLE(bpc, bpp, color_space, table)          \
	case RC_TABLE_ID(bpc, bpp, color_space):              \
		table = RC_TABLE_ADDR(bpc, bpp, color_space); \
		break;
	switch (id) {
		MATCH_RC_TABLE(8, 6, 422, table);
		MATCH_RC_TABLE(8, 6, 444, table);
		MATCH_RC_TABLE(8, 7, 422, table);
		MATCH_RC_TABLE(8, 8, 422, table);
		MATCH_RC_TABLE(8, 8, 444, table);
		MATCH_RC_TABLE(8, 10, 422, table);
		MATCH_RC_TABLE(8, 10, 444, table);
		MATCH_RC_TABLE(8, 12, 444, table);
		MATCH_RC_TABLE(8, 15, 444, table);
		MATCH_RC_TABLE(10, 6, 422, table);
		MATCH_RC_TABLE(10, 6, 444, table);
		MATCH_RC_TABLE(10, 7, 422, table);
		MATCH_RC_TABLE(10, 8, 422, table);
		MATCH_RC_TABLE(10, 8, 444, table);
		MATCH_RC_TABLE(10, 10, 422, table);
		MATCH_RC_TABLE(10, 10, 444, table);
		MATCH_RC_TABLE(10, 12, 444, table);
		MATCH_RC_TABLE(10, 15, 444, table);
	default:
		pr_err("%s: No table for bpc(%u), bpp(%u), color_space %s\n", __func__,
		       dsc->bits_per_component, dsc->bits_per_pixel,
		       dsc->native_422 ? "4:2:2" : "4:4:4");
		break;
	}
#undef MATCH_RC_TABLE
	if (!table)
		return -EINVAL;

	for (i = 0; i < DSC_NUM_BUF_RANGES - 1; i++)
		dsc->rc_buf_thresh[i] = table->rc_buf_thresh[i];

	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		dsc->rc_range_params[i].range_min_qp = table->rc_min_qp[i];
		dsc->rc_range_params[i].range_max_qp = table->rc_max_qp[i];
		dsc->rc_range_params[i].range_bpg_offset = table->rc_offset[i];
	}
	return 0;
}
