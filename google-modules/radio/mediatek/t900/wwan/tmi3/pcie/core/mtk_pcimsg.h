/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_PCIMSG_H__
#define __MTK_PCIMSG_H__

#include "mtk_dev.h"

enum mtk_msg_id {
	MTK_PCIMSG_H2C_READY        = 1,
	MTK_PCIMSG_H2C_BAR          = 2,
	MTK_PCIMSG_H2C_EXCEPT       = 3,
	MTK_PCIMSG_H2C_EXCEPT_ACK   = 4,
	MTK_PCIMSG_H2C_SMEM         = 5,
	MTK_PCIMSG_H2C_NORMAL       = 6,
	MTK_PCIMSG_C2H_REQ_BAR      = 0xA1,
	MTK_PCIMSG_C2H_EXCEPT       = 0xA2,
	MTK_PCIMSG_C2H_EXCEPT_ACK   = 0xA3,
	MTK_PCIMSG_C2H_REQ_SMEM     = 0xA4,
	MTK_PCIMSG_C2H_REQ_STATE    = 0xA5
};

#if IS_ENABLED(CONFIG_MTK_AUDIODSP_SUPPORT)
int mtk_pcimsg_messenger_init(struct mtk_md_dev *mdev);
int mtk_pcimsg_messenger_exit(struct mtk_md_dev *mdev);
int mtk_pcimsg_send_msg_to_user(struct mtk_md_dev *mdev, int msg_id);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev, bool is_suspend);
#else
bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev);
#endif
int mtk_pcimsg_wait_pci_user_inactive(struct mtk_md_dev *mdev);
#elif IS_ENABLED(CONFIG_GOOGLE_RADIO_BRIDGE)
int mtk_pcimsg_send_msg_to_user(struct mtk_md_dev *mdev, int msg_id);
int mtk_pcimsg_wait_pci_user_inactive(struct mtk_md_dev *mdev);
int mtk_pcimsg_messenger_init(struct mtk_md_dev *mdev);
int mtk_pcimsg_messenger_exit(struct mtk_md_dev *mdev);
bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev, bool is_suspend);
#else
static inline int mtk_pcimsg_send_msg_to_user(struct mtk_md_dev *mdev, int msg_id)
{
	return 0;
}

static inline int mtk_pcimsg_messenger_init(struct mtk_md_dev *mdev)
{
	return 0;
}

static inline int mtk_pcimsg_messenger_exit(struct mtk_md_dev *mdev)
{
	return 0;
}

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
static inline bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev, bool is_suspend)
{
	return false;
}
#else
static inline bool mtk_pcimsg_pci_user_is_busy(struct mtk_md_dev *mdev)
{
	return false;
}
#endif

static inline int mtk_pcimsg_wait_pci_user_inactive(struct mtk_md_dev *mdev)
{
	return 0;
}
#endif /* CONFIG_MTK_AUDIODSP_SUPPORT */
#endif /* __MTK_PCIMSG_H__ */
