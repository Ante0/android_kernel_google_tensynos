/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2010 Google LLC
 */

#ifndef __GOOGLE_GS_CHIPDID_H_
#define __GOOGLE_GS_CHIPDID_H_

#if IS_ENABLED(CONFIG_GS_CHIPID)
u32 gs_chipid_get_type(void);
s32 gs_chipid_get_dvfs_version(void);
u32 gs_chipid_get_revision(void);
u32 gs_chipid_get_product_id(void);
#else
static inline u32 gs_chipid_get_type(void)
{
	return 0;
}
static inline s32 gs_chipid_get_dvfs_version(void)
{
	return 0;
}
static inline u32 gs_chipid_get_revision(void)
{
	return 0;
}
static inline u32 gs_chipid_get_product_id(void)
{
	return 0;
}
#endif

#endif /* __GOOGLE_GS_CHIPDID_H_ */
