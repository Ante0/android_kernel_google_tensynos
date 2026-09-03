/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */
#ifndef _UFS_GOOGLE_DBG_H_
#define _UFS_GOOGLE_DBG_H_

#include <ufs/ufshcd.h>

int ufs_google_init_dbg(struct ufs_hba *hba);
void ufs_google_remove_dbg(struct ufs_hba *hba);

int ufs_google_init_debugfs(struct ufs_hba *hba);
void ufs_google_remove_debugfs(struct ufs_hba *hba);

void ufs_google_dbg_register_dump(struct ufs_hba *hba);

#endif /* _UFS_GOOGLE_DBG_H_ */
