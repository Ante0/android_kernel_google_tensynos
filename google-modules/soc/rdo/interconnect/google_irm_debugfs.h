/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_IRM_DEBUGFS_H
#define _GOOGLE_IRM_DEBUGFS_H

#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "google_irm.h"

struct irm_dbg_client;
struct irm_dbg;

#define get_irm_client(dbg_client) \
	(&((dbg_client))->dbg->irm_dev->client[(dbg_client)->id])

#define DEFINE_IRM_DEBUGFS_ATTRIBUTE(__name, __offset)                       \
static int __name ## _set(void *data, u64 val)                               \
{                                                                            \
	struct irm_dbg_client *dbg_client = (struct irm_dbg_client *)data;   \
	struct irm_dbg *dbg = dbg_client->dbg;                               \
	struct irm_dev *irm_dev = dbg->irm_dev;                              \
	struct irm_client *client = get_irm_client(dbg_client);              \
	u32 addr;                                                            \
	mutex_lock(&client->mutex);                                          \
	if (dbg_client->ctl) {                                               \
		addr = client->base + (__offset);                            \
		regmap_write(irm_dev->regmap, addr, (unsigned int)val);      \
	}                                                                    \
	mutex_unlock(&client->mutex);                                        \
	return 0;                                                            \
}                                                                            \
static int __name ## _get(void *data, u64 *val)                              \
{                                                                            \
	struct irm_dbg_client *dbg_client = (struct irm_dbg_client *)data;   \
	struct irm_dbg *dbg = dbg_client->dbg;                               \
	struct irm_dev *irm_dev = dbg->irm_dev;                              \
	struct irm_client *client = get_irm_client(dbg_client);              \
	u32 tmp;                                                             \
	u32 addr;                                                            \
	addr = client->base + (__offset);                                    \
	mutex_lock(&client->mutex);                                          \
	regmap_read(irm_dev->regmap, addr, &tmp);                            \
	mutex_unlock(&client->mutex);                                        \
	*val = (u64)tmp;                                                     \
	return 0;                                                            \
}                                                                            \
DEFINE_DEBUGFS_ATTRIBUTE(__name, __name ## _get, __name ## _set, "0x%08llx\n")

#define dump_register(__name)                           \
{                                                       \
	.name   = __stringify(__name),                  \
	.offset = __name,                               \
}

struct irm_dbg_client {
	struct dentry *dir;
	struct irm_dbg *dbg;
	u8 id;
	u8 ctl;
	struct debugfs_regset32 *regset;
	const char *name;
};

struct irm_dbg {
	struct dentry *base_dir; /* Debugfs base directory */
	struct irm_dev *irm_dev;
	size_t num_client;
	struct irm_dbg_client *client;
};

int irm_create_debugfs(struct irm_dev *irm_dev, int size, const char **client_name);
void irm_remove_debugfs(struct irm_dev *irm_dev);

#endif /* _GOOGLE_IRM_DEBUGFS_H */
