// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/module.h>

#include "mtk_bm.h"
#include "mtk_debugfs.h"
#include "mtk_dev.h"
#include "mtk_port.h"
#include "mtk_port_io.h"
#include "mtk_utility.h"

#define TAG "DEV"

struct sock *mtk_netlink_sock;

int mtk_dev_dump(struct mtk_md_dev *mdev)
{
	int ret1, ret2;

	ret1 = mtk_dev_dbg_dump(mdev);
	ret2 = mtk_ctrl_dump(mdev);
	if (ret1 == -EFAULT || ret2 == -EIO)
		return -EIO;
	return 0;
}

struct mtk_md_dev *mtk_dev_alloc(struct device *pdev, const struct mtk_dev_ops *dev_ops)
{
	struct mtk_md_dev *mdev;

	mdev = devm_kzalloc(pdev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return NULL;

	mdev->dev_ops = dev_ops;
	mdev->dev = pdev;
	return mdev;
}
EXPORT_SYMBOL(mtk_dev_alloc);

void mtk_dev_free(struct mtk_md_dev *mdev)
{
	struct device *dev = mdev->dev;

	devm_kfree(dev, mdev);
}
EXPORT_SYMBOL(mtk_dev_free);

void mtk_dev_except(struct mtk_md_dev *mdev)
{
	mtk_fsm_evt_submit(mdev, FSM_EVT_DEV_RESET_REQ, FSM_F_DFLT, NULL, 0, 0);
}
EXPORT_SYMBOL(mtk_dev_except);

static int __init mtk_common_drv_init(void)
{
	int ret;

	pr_info("mtk_common_driver_init\n");
	ret = mtk_port_io_init();
	if (ret)
		goto err_init_devid;
	mtk_drv_dbgfs_init();
	mtk_netlink_init();

err_init_devid:
	return ret;
}
module_init(mtk_common_drv_init);

static void __exit mtk_common_drv_exit(void)
{
	pr_info("mtk_common_driver_exit\n");
	mtk_fsm_kernel_notifier_cleanup();
	mtk_netlink_uninit();
	mtk_drv_dbgfs_exit();
	mtk_port_io_exit();
	mtk_port_stale_list_grp_cleanup();
}
module_exit(mtk_common_drv_exit);

MODULE_LICENSE("Dual BSD/GPL");
