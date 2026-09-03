/* SPDX-License-Identifier: GPL-2.0-only */
//
// Copyright 2020 Google LLC

#ifndef _UFS_PIXEL_CRYPTO_H_
#define _UFS_PIXEL_CRYPTO_H_

#define CRYPTO_DATA_UNIT_SIZE 4096

#if defined(CONFIG_SCSI_UFS_CRYPTO) && IS_ENABLED(CONFIG_UFS_PIXEL_CRYPTO)
int pixel_ufs_crypto_init(struct ufs_hba *hba);
int pixel_ufs_crypto_resume(struct ufs_hba *hba);
#else
static inline int pixel_ufs_crypto_init(struct ufs_hba *hba)
{
	return 0;
}
static inline int pixel_ufs_crypto_resume(struct ufs_hba *hba)
{
	return 0;
}
#endif

#endif /* _UFS_PIXEL_CRYPTO_H_ */
