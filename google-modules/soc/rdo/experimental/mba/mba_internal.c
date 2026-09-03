// SPDX-License-Identifier: GPL-2.0-only
#include "mba_internal.h"

#include "mba_client.h"
#include "mba_host.h"

static struct dentry *google_mba_debugfs_root;

int google_mba_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	google_mba_debugfs_root = debugfs_create_dir("google-mba", NULL);
	if (IS_ERR(google_mba_debugfs_root))
		return PTR_ERR(google_mba_debugfs_root);
	return 0;
}

void google_mba_debugfs_exit(void)
{
	debugfs_remove_recursive(google_mba_debugfs_root);
}

static void mba_init_debugfs(struct google_mba *mba)
{
	struct device *dev = mba->dev;

	mba->debugfs =
		debugfs_create_dir(dev_name(dev), google_mba_debugfs_root);
	if (IS_ERR(mba->debugfs)) {
		dev_warn(dev, "failed to create debugfs");
		return;
	}
	// Device-wide debugfs entries go here.
}

static void mba_exit_debugfs(struct google_mba *mba)
{
	debugfs_remove_recursive(mba->debugfs);
}

int google_mba_init(struct google_mba *mba)
{
	struct device *dev = mba->dev;
	int i;
	int err;

	mba_init_debugfs(mba);

	err = google_mba_host_init(&mba->host);
	if (err) {
		dev_err(dev, "failed to initialize host\n");
		return err;
	}

	for (i = 0; i < MBA_NUM_CLIENT; ++i) {
		mba->clients[i].idx = i;
		err = google_mba_client_init(&mba->clients[i]);
		if (err) {
			dev_err(dev, "failed to initialize client %d\n", i);
			return err;
		}
	}
	return 0;
}

void google_mba_exit(struct google_mba *mba)
{
	mba_exit_debugfs(mba);
}
