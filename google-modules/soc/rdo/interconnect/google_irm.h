/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_H
#define _GOOGLE_IRM_H

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <soc/google/goog_mba_cpm_iface.h>

#include "google_icc_vote.h"

enum {
	IRM_CLIENT_PROP_REG_OFFSET = 0,
	IRM_CLIENT_PROP_CPM_IRM_CLIENT_ID,
	IRM_CLIENT_PROP_TYPE,
	NF_IRM_CLIENT_PROP,
};

struct irm_client {
	u32 base;
	u32 sync_type;
	/* Accept GMC vote only, or both GMC and GSLC votes */
	u32 irm_type;
	/* protect internal struct/register access */
	struct mutex mutex;
	struct completion cpm_done;
	/* backup client votes when control switch to debugfs */
	struct icc_vote vote_backup;
	u32 num_fabric;
	const char **fabric_name_arr;
};

struct irm_dev_ops;

struct irm_mbox {
	struct cpm_iface_client *client;
};

struct irm_dev {
	struct device *dev;
	void __iomem *base_addr;
	struct regmap *regmap;
	/*
	 * client id map between CPM and IRM driver
	 * client_map[cpm_id] = irm_driver_id
	 */
	s32 *client_map;
	struct irm_client *client;
	struct irm_dbg *dbg; /* Debug related info */
	struct irm_mbox mbox;
	struct irm_dev_ops *ops;
};

struct irm_dev_ops {
	int (*irm_vote)(struct irm_dev *irm_dev, u32 attr,
			const struct icc_vote *vote);
};

int irm_vote_commit(struct irm_dev *irm_dev, u8 id);

#endif /* _GOOGLE_IRM_H */
