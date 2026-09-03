/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_DEV_LINK_H__
#define __MTK_DEV_LINK_H__

#include "mtk_dev.h"

#ifdef CONFIG_MTK_DEVLINK

int mtk_devlink_init(struct mtk_md_dev *mdev);
void mtk_devlink_exit(struct mtk_md_dev *mdev);
#else /* !CONFIG_MTK_DEVLINK */

static inline int mtk_devlink_init(struct mtk_md_dev *mdev) { return 0; }
static inline void mtk_devlink_exit(struct mtk_md_dev *mdev) {}

#endif /* CONFIG_MTK_DEVLINK */
#endif /* __MTK_DEV_LINK_H__ */
