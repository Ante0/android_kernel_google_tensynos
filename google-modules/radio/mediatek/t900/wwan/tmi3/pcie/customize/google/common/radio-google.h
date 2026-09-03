/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __RADIO_GOOGLE_H__
#define __RADIO_GOOGLE_H__

#include "../pcie/link-exception.h"
#include "../pcie/md2ap-wakemon.h"
#include "../pcie/mtk-pcie-pm-user-internal.h"
#include "../pcie/mtk-pcie.h"
#include "../pcie/remote-wakeup.h"
#include "mtk_dev.h"
#include "mtk_google.h"

struct radio_google {
	struct mtk_md_dev *mdev;
	struct tmi_ops *tmi_ops;
	struct mtk_google_pcie *mtk_google_pcie;
	struct remote_wakeup *remote_wakeup;
	struct mtk_pci_user_pm *mtk_pci_user_pm;
	struct md2ap_wakemon *md2ap_wakemon;
	struct soc_qos_ext *soc_qos_ext;
	struct link_exception *link_exception;
};

int radio_google_early_init(void);

void radio_google_late_exit(void);

int radio_google_init(struct mtk_md_dev *mdev, struct tmi_ops *ops);

void radio_google_exit(struct mtk_md_dev *mdev);

#endif /* __RADIO_GOOGLE_H__ */
