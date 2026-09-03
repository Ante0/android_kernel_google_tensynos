// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/device.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include "mtk_bm.h"
#include "mtk_ctrl_plane.h"
#include "mtk_debug.h"
#include "mtk_port.h"
#ifdef CONFIG_TX00_UT_CTRL
#include "ut_ctrl_fake.h"
#endif

#define TAG			"CTRL"

static void mtk_ctrl_trans_fsm_state_handler(struct mtk_fsm_param *param,
					     struct mtk_ctrl_blk *ctrl_blk)
{
	struct mtk_md_dev *mdev = ctrl_blk->mdev;

	switch (param->to) {
	case FSM_STATE_OFF:
		ctrl_blk->ops->fsm_indication(mdev, param);
		ctrl_blk->ops->exit(mdev);
		break;
	case FSM_STATE_ON:
		ctrl_blk->ops->init(mdev);
		fallthrough;
	default:
		ctrl_blk->ops->fsm_indication(mdev, param);
		break;
	}
}

static void mtk_ctrl_fsm_state_listener(struct mtk_fsm_param *param, void *data)
{
	struct mtk_ctrl_blk *ctrl_blk = data;

	mtk_port_mngr_fsm_state_handler(param, ctrl_blk->port_mngr);
	mtk_ctrl_trans_fsm_state_handler(param, ctrl_blk);
	mtk_port_mngr_fsm_state_handler_late(param, ctrl_blk->port_mngr);
}

int mtk_ctrl_dump(struct mtk_md_dev *mdev)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;

	mtk_ports_dump(mdev);
	return ctrl_blk->ops->dump(mdev);
}

int mtk_ctrl_init(struct mtk_md_dev *mdev, struct mtk_ctrl_hif_ops *ops, struct mtk_ctrl_cfg *cfg)
{
	struct mtk_ctrl_blk *ctrl_blk;
	int err;

	BUILD_BUG_ON(sizeof(struct trb) > MAX_USER_CB_SIZE);
	ctrl_blk = devm_kzalloc(mdev->dev, sizeof(*ctrl_blk), GFP_KERNEL);
	if (!ctrl_blk)
		return -ENOMEM;

	ctrl_blk->mdev = mdev;
	mdev->ctrl_blk = ctrl_blk;
	ctrl_blk->ops = ops;
	ctrl_blk->cfg = cfg;
	mtk_fsm_cfg_info_update(mdev, cfg->fsm_cfg);

	err = mtk_port_mngr_init(ctrl_blk, cfg->port_layer_cfg->port_cfg,
				 cfg->port_layer_cfg->get_port_cnt());
	if (err)
		goto err_free_mem;

	err = mtk_fsm_notifier_register(mdev, MTK_USER_CTRL, mtk_ctrl_fsm_state_listener,
					ctrl_blk, FSM_PRIO_1, false);
	if (err) {
		MTK_ERR(mdev, "Fail to register fsm notification(ret = %d)\n", err);
		goto err_port_exit;
	}

	return 0;

err_port_exit:
	mtk_port_mngr_exit(ctrl_blk);
err_free_mem:
	devm_kfree(mdev->dev, ctrl_blk);

	return err;
}
EXPORT_SYMBOL(mtk_ctrl_init);

int mtk_ctrl_exit(struct mtk_md_dev *mdev)
{
	struct mtk_ctrl_blk *ctrl_blk = mdev->ctrl_blk;

	mtk_fsm_notifier_unregister(mdev, MTK_USER_CTRL);
	mtk_port_mngr_exit(ctrl_blk);
	devm_kfree(mdev->dev, ctrl_blk);

	return 0;
}
EXPORT_SYMBOL(mtk_ctrl_exit);

