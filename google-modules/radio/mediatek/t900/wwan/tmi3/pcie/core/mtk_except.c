// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/pm_runtime.h>
#include <linux/timer.h>

#include "mtk_debug.h"
#include "mtk_except.h"
#include "mtk_fsm.h"
#include "mtk_pcimsg.h"
#include "mtk_pm.h"

#ifndef CONFIG_UT_PCIE_EXCEPT
#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
#include "mtk_pwrctl.h"
#endif
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
#include "pcie-mediatek-gen3.h"
#endif
#else
#include "ut_except_fake.h"
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "pcie/link-exception.h"
#endif

#define TAG			"EXCEPT"

#define MTK_EXCEPTION_HOST_RESET_TIME		(2)
#define MTK_EXCEPTION_SELF_RESET_TIME		(35)
#define MTK_EXCEPTION_COLD_RESET_TIME		(13)
#define MTK_EXCEPTION_DUMP_PREPARE_TIME		(5)
#define MTK_EXCEPTION_MAX_CHECK_COUNT		(60)
#define MTK_EXCEPTION_INIT_FLAG		(0)
#define MTK_EXCEPTION_LINK_ERR_IGNORE		(1)
#define MTK_EXCEPTION_RESET_START		(2)
#define MTK_EXCEPTION_RESET_TYPE		(0xC000000)
#define MTK_EXCEPTION_RESET_TYPE_PLDR		BIT(26)
#define MTK_EXCEPTION_RESET_TYPE_FLDR		BIT(27)
#define MTK_EXCEPTION_SUPPORT_REBOOTINT			BIT(0)
#define MTK_EXCEPTION_SUPPORT_AEE_REBOOT		BIT(1)
#define MTK_EXCEPTION_SUPPORT_MD_ASSERT			BIT(2)

#ifndef DEV_PIN_SUPPORT_MAPPING
/* GPIO pin support mapping: MD_ASSERT: bit 2, AEE_REBOOT: bit 1, REBOOTINT: bit 0 */
#define DEV_PIN_SUPPORT_MAPPING (0)
#endif

static void mtk_exception_start_monitor(struct mtk_md_dev *mdev, unsigned long expires)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;

	except = priv->except;

	if (!timer_pending(&except->check_link_timer) && !mtk_pci_get_hp_status(mdev)) {
		except->check_link_timer.expires = jiffies + expires;
		add_timer(&except->check_link_timer);
		MTK_INFO(mdev, "Add timer to monitor PCI link\n");
	} else {
		MTK_INFO(mdev, "Add timer exists or HotPlug enabled\n");
	}
}

static int mtk_exception_rgu_handler(struct mtk_md_dev *mdev, int aee_type)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_fsm *fsm = mdev->fsm;
	struct mtk_md_except *except;
	u32 dev_cfg;
	int val;

	except = priv->except;

	/* delay some time to make sure device ready for reset */
	msleep(60);

	val = mtk_dev_get_dev_state(mdev);
	if (!(val & MTK_EXCEPTION_RESET_TYPE) && fsm->state < FSM_STATE_READY) {
		MTK_INFO(mdev, "RGU ignored,dev_state:0x%x\n", val);
		return -EINVAL;
	}
	MTK_INFO(mdev, "dev_state:0x%x, fsm state:%d\n", val, fsm->state);

	if (except->dev_pin_cap & MTK_EXCEPTION_SUPPORT_AEE_REBOOT) {
		spin_lock_bh(&except->exception_lock);
		if (!test_and_set_bit(MTK_EXCEPTION_RESET_START, &except->flag)) {
			except->type = RESET_AEE_REBOOT;
			MTK_INFO(mdev, "Receiving RGU before REBOOTINT\n");
		} else {
			if (del_timer_sync(&except->guard_timer) &&
			    !(aee_type & MTK_EXCEPTION_SUPPORT_ONLINE_DBG)) {
				except->type = RESET_AEE_REBOOT;
				MTK_INFO(mdev, "Deleting guardtimer when receive RGU\n");
			} else {
				spin_unlock_bh(&except->exception_lock);
				MTK_INFO(mdev, "Failed to delete guard timer or online debug\n");
				return -EINVAL;
			}
		}
		spin_unlock_bh(&except->exception_lock);
	} else {
		dev_cfg = mtk_dev_get_dev_cfg(mdev);
		MTK_INFO(mdev, "RGU reboot reason is 0x%x\n", (dev_cfg & 0x1F));

		/* Invalid dev state will trigger PLDR */
		if (val & MTK_EXCEPTION_RESET_TYPE_PLDR) {
			except->type = RESET_PLDR;
		} else if (val & MTK_EXCEPTION_RESET_TYPE_FLDR) {
			except->type = RESET_FLDR;
		} else {
			MTK_INFO(mdev, "HW reboot\n");
			except->type = RESET_NONE;
		}
	}

	return 0;
}

int mtk_exception_report_evt(struct mtk_md_dev *mdev, enum mtk_except_evt evt)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;
	int aee_type;

	except = priv->except;
	MTK_INFO(mdev, "%ps report evt:%d, except flag:%d\n", __builtin_return_address(0),
		 evt, except->flag);

	if (!test_bit(MTK_EXCEPTION_INIT_FLAG, &except->flag))
		return -EFAULT;

	aee_type = (except->config_info >> MTK_EXCEPTION_CONFIG_OFFSET) & MTK_EXCEPTION_CONFIG_INFO;

	switch (evt) {
	case EXCEPTION_LINK_ERR:
		if (!test_and_set_bit(MTK_EXCEPTION_LINK_ERR_IGNORE, &except->flag)) {
#if IS_ENABLED(CONFIG_GOOGLE_B528903481_DEBUG)
			if (mtk_pci_mmio_check(mdev)) {
				mtk_pci_mmio_hw_check(mdev);
				MTK_INFO(mdev, "mmio check is %d in reporting event\n",
					 mtk_pci_mmio_check(mdev));
			}
#else
			if (mtk_pci_mmio_check(mdev))
				MTK_INFO(mdev, "mmio check is %d in reporting event\n",
					 mtk_pci_mmio_check(mdev));
#endif
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
			mtk_pcie_disable_data_trans(MTK_PCIE_PORT_NUM);
#endif
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
			radio_google_link_error_handler(mdev->google);
#endif
			if (aee_type & MTK_EXCEPTION_SUPPORT_ONLINE_DBG) {
				mtk_fsm_evt_submit(mdev, FSM_EVT_EXCEPTION_NOTIFY,
						   FSM_F_EXCEPT_INT, NULL, 0, 0);
				mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT);
			} else {
				if (except->dev_pin_cap & MTK_EXCEPTION_SUPPORT_MD_ASSERT)
					mtk_fsm_evt_submit(mdev, FSM_EVT_LINKDOWN,
							   FSM_F_LINK_EXCEPTION, NULL, 0, 0);
				else
					mtk_fsm_evt_submit(mdev, FSM_EVT_LINKDOWN, FSM_F_DFLT,
							   NULL, 0, 0);
			}
		}
		break;
	case EXCEPTION_RGU:
		if (!mtk_exception_rgu_handler(mdev, aee_type)) {
			if (aee_type & MTK_EXCEPTION_SUPPORT_ONLINE_DBG) {
				mtk_fsm_evt_submit(mdev, FSM_EVT_EXCEPTION_NOTIFY,
						   FSM_F_EXCEPT_INT, NULL, 0, 0);
			} else {
				mtk_fsm_evt_submit(mdev, FSM_EVT_DEV_RESET_REQ,
						   FSM_F_DFLT, NULL, 0, 0);
			}
		}
		break;
	case EXCEPTION_REBOOTINT:
		spin_lock_bh(&except->exception_lock);
		if (!test_and_set_bit(MTK_EXCEPTION_RESET_START, &except->flag)) {
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
			if (mtk_pci_link_check_silent(mdev)) {
#endif
			mtk_fsm_evt_submit(mdev, FSM_EVT_EXCEPTION_NOTIFY,
					   FSM_F_EXCEPT_INT, NULL, 0, 0);
#if IS_ENABLED(CONFIG_GOOGLE_LINK_DOWN_MMIO_PROTECT)
			}
#endif
			except->guard_timer.expires = jiffies + MTK_EXCEPTION_DUMP_PREPARE_TIME
						      * HZ;
			add_timer(&except->guard_timer);
			MTK_INFO(mdev, "Receiving REBOOTINT,flag: %d, pin_cap: %d\n",
				 except->flag, except->dev_pin_cap);
		} else {
			MTK_INFO(mdev, "Ignores REBOOTINT\n");
		}
		spin_unlock_bh(&except->exception_lock);
		break;
	case EXCEPTION_AER_DETECTED:
#if IS_ENABLED(CONFIG_GOOGLE_B528903481_DEBUG)
		if (mtk_pci_mmio_check(mdev)) {
			mtk_pci_mmio_hw_check(mdev);
			MTK_INFO(mdev, "mmio check is %d in reporting event\n",
				 mtk_pci_mmio_check(mdev));
		}
#else
		if (mtk_pci_mmio_check(mdev))
			MTK_INFO(mdev, "mmio check is %d in reporting event\n",
				 mtk_pci_mmio_check(mdev));
#endif
#if IS_ENABLED(CONFIG_DEVICE_MODULES_PCIE_MEDIATEK_GEN3) || IS_ENABLED(CONFIG_PCIE_MEDIATEK_GEN3)
		mtk_pcie_disable_data_trans(MTK_PCIE_PORT_NUM);
#endif

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		radio_google_uncor_error_handler(mdev->google);
#endif
		if (aee_type & MTK_EXCEPTION_SUPPORT_ONLINE_DBG) {
			mtk_fsm_evt_submit(mdev, FSM_EVT_EXCEPTION_NOTIFY, FSM_F_EXCEPT_INT,
					   NULL, 0, EVT_MODE_BLOCKING_WITHOUT_TIMEOUT);
			mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT);
		} else {
			if (except->dev_pin_cap & MTK_EXCEPTION_SUPPORT_MD_ASSERT)
				mtk_fsm_evt_submit(mdev, FSM_EVT_BUS_ERR, FSM_F_LINK_EXCEPTION,
						   NULL, 0, EVT_MODE_BLOCKING_WITHOUT_TIMEOUT);
			else
				mtk_fsm_evt_submit(mdev, FSM_EVT_BUS_ERR, FSM_F_DFLT, NULL, 0,
						   EVT_MODE_BLOCKING_WITHOUT_TIMEOUT);
	}
		break;
	default:
		break;
	}

	return 0;
}

void mtk_exception_start(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;

	except = priv->except;
	mtk_pci_unmask_irq(mdev, except->pci_ext_irq_id);
}

void mtk_exception_stop(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;

	except = priv->except;
	mtk_pci_mask_irq(mdev, except->pci_ext_irq_id);
}

static bool mtk_exception_reinit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;
	bool link_up;

	except = priv->except;

	link_up = mtk_pci_link_check(mdev);
	if (link_up) {
		MTK_INFO(mdev, "Append FSM reinit\n");
		mtk_fsm_evt_submit(mdev, FSM_EVT_REINIT, FSM_F_FULL_REINIT, NULL, 0, 0);
		except->check_cnt = 0;
	}

	return link_up;
}

#ifdef CONFIG_MTK_WWAN_PWRCTL_SUPPORT
static void mtk_exception_pwrctl_handler(enum pwrctl_evt evt, void *data)
{
	struct mtk_md_except *except = data;
	struct mtk_md_dev *mdev;
	int ret;

	mdev = except->mdev;
	MTK_INFO(mdev, "pwrctl event id:%d\n", evt);

	switch (evt) {
	case PWRCTL_EVT_RESET:
		del_timer_sync(&except->check_link_timer);
		mtk_fsm_evt_submit(mdev, FSM_EVT_PWROFF, FSM_F_DFLT, NULL, 0,
				   EVT_MODE_BLOCKING);
		fallthrough;
	case PWRCTL_EVT_PWRON:
		if (except->dev_pin_cap & MTK_EXCEPTION_SUPPORT_REBOOTINT)
			mtk_exception_reinit(mdev);
		else
			mtk_exception_start_monitor(mdev, MTK_EXCEPTION_HOST_RESET_TIME * HZ);
		break;
	case PWRCTL_EVT_PWROFF:
		pm_runtime_resume(mdev->dev);
		del_timer_sync(&except->check_link_timer);
		mtk_fsm_evt_submit(mdev, FSM_EVT_PWROFF, FSM_F_DFLT, NULL, 0,
				   EVT_MODE_BLOCKING);
		break;
	case PWRCTL_EVT_PRERST:
		if (mtk_pm_allow_smart_suspend(mdev))
			mtk_pm_set_smart_suspend_wake(mdev, true);
		pm_runtime_resume(mdev->dev);
		ret = mtk_exception_report_evt(mdev, EXCEPTION_REBOOTINT);
		if (ret)
			MTK_ERR(mdev, "Failed to report exception with EXCEPTION_REBOOTINT\n");
		break;
	default:
		break;
	}
}
#endif

static void mtk_exception_fsm_pre_handler(struct mtk_fsm_param *param, void *data)
{
	struct mtk_md_except *except = data;
	struct mtk_md_dev *mdev;

	mdev = except->mdev;

	switch (param->to) {
	case FSM_STATE_OFF:
		mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT);
		break;
	case FSM_STATE_BOOTUP:
		if (param->fsm_flag == FSM_F_DFLT) {
			except->config_info = mtk_dev_get_dev_cfg(mdev);
			MTK_INFO(mdev, "AEE Config is 0x%x,Online Debug is 0x%x\n",
				 (except->config_info >> MTK_EXCEPTION_CONFIG_OFFSET)
				  & MTK_EXCEPTION_AEE_CONFIG,
				 (except->config_info >> MTK_EXCEPTION_CONFIG_OFFSET)
				  & MTK_EXCEPTION_SUPPORT_ONLINE_DBG);
		}
		break;
	case FSM_STATE_EXCEPTION:
		if (param->fsm_flag == FSM_F_MDEE_INIT)
			mtk_pci_dump(mdev);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		else if (param->fsm_flag == FSM_F_LINK_EXCEPTION)
			mtk_pcimsg_send_msg_to_user(mdev, MTK_PCIMSG_H2C_EXCEPT);
#endif
		break;
	default:
		break;
	}
}

static void mtk_exception_fsm_post_handler(struct mtk_fsm_param *param, void *data)
{
	struct mtk_md_except *except = data;
	enum mtk_reset_type reset_type;
	struct mtk_md_dev *mdev;
	unsigned long expires;
	int err;

	mdev = except->mdev;

	switch (param->to) {
	case FSM_STATE_POSTDUMP:
		mtk_pci_mask_irq(mdev, except->pci_ext_irq_id);
		mtk_pci_clear_irq(mdev, except->pci_ext_irq_id);
		mtk_pci_unmask_irq(mdev, except->pci_ext_irq_id);
		break;
	case FSM_STATE_EXCEPTION:
		if (param->fsm_flag & FSM_F_LINK_EXCEPTION) {
#ifdef CONFIG_MTK_WWAN_PWRCTL_SUPPORT
			mtk_pwrctl_force_md_assert();
#endif
			clear_bit(MTK_EXCEPTION_LINK_ERR_IGNORE, &except->flag);
			MTK_INFO(mdev, "Exception flag: 0x%x\n", param->fsm_flag);
		}
		break;
	case FSM_STATE_OFF:
		mtk_pci_reset_sys_irq(mdev);
		del_timer_sync(&except->guard_timer);
		clear_bit(MTK_EXCEPTION_LINK_ERR_IGNORE, &except->flag);
		clear_bit(MTK_EXCEPTION_RESET_START, &except->flag);
		if (param->evt_id == FSM_EVT_DEV_RESET_REQ) {
			reset_type = except->type;
		} else if (param->evt_id == FSM_EVT_LINKDOWN ||
		    param->evt_id == FSM_EVT_BUS_ERR) {
			if (except->dev_pin_cap & MTK_EXCEPTION_SUPPORT_AEE_REBOOT)
				reset_type = RESET_AEE_REBOOT;
			else
				reset_type = RESET_FLDR;
		} else {
			break;
		}

		if (reset_type == RESET_NONE) {
			expires = MTK_EXCEPTION_SELF_RESET_TIME * HZ;
		} else {
			/* ensure no external user is using PCI */
			mtk_pcimsg_wait_pci_user_inactive(mdev);
			err = mtk_pci_reset(mdev, reset_type);
			if (reset_type == RESET_AEE_REBOOT) {
				if (!err) {
					if (mtk_exception_reinit(mdev))
						break;
					expires = HZ;
				} else {
					MTK_INFO(mdev, "HW reset fail\n");
					break;
				}
			} else {
				if (err && reset_type == RESET_PLDR)
					expires = MTK_EXCEPTION_COLD_RESET_TIME * HZ;
				else if (err)
					expires = MTK_EXCEPTION_SELF_RESET_TIME * HZ;
				else
					expires = MTK_EXCEPTION_HOST_RESET_TIME * HZ;
			}
		}
		mtk_exception_start_monitor(mdev, expires);
		break;
	default:
		break;
	}
}

static void mtk_exception_link_monitor(struct timer_list *timer)
{
	struct mtk_md_except *except = container_of(timer, struct mtk_md_except, check_link_timer);
	struct mtk_md_dev *mdev = except->mdev;

	if (mtk_exception_reinit(mdev)) {
		del_timer(&except->check_link_timer);
	} else if (except->check_cnt < MTK_EXCEPTION_MAX_CHECK_COUNT) {
		mod_timer(timer, jiffies + HZ);
		except->check_cnt++;
	} else {
		del_timer(&except->check_link_timer);
		except->check_cnt = 0;
		MTK_INFO(mdev, "Stop link check timer\n");
	}
}

static void mtk_exception_guardtimer_handler(struct timer_list *timer)
{
	struct mtk_md_except *except = container_of(timer, struct mtk_md_except, guard_timer);
	struct mtk_md_dev *mdev = except->mdev;
	int aee_type;

	except->type = RESET_AEE_REBOOT;
	aee_type = (except->config_info >> MTK_EXCEPTION_CONFIG_OFFSET) & MTK_EXCEPTION_CONFIG_INFO;

	if (!(aee_type & MTK_EXCEPTION_SUPPORT_ONLINE_DBG))
		mtk_fsm_evt_submit(mdev, FSM_EVT_DEV_RESET_REQ, FSM_F_DFLT, NULL, 0, 0);
}

int mtk_exception_init(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;

	except = devm_kzalloc(mdev->dev, sizeof(*except), GFP_KERNEL);

	if (!except)
		return -ENOMEM;

	except->mdev = mdev;
	except->pci_ext_irq_id = mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_SAP_RGU);
	except->dev_pin_cap = DEV_PIN_SUPPORT_MAPPING;
	except->config_info = 0;
	priv->except = except;

	spin_lock_init(&except->exception_lock);

	timer_setup(&except->guard_timer, mtk_exception_guardtimer_handler, 0);
	timer_setup(&except->check_link_timer, mtk_exception_link_monitor, 0);

#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
	mtk_pwrctl_event_register_callback(mtk_exception_pwrctl_handler, except);
#endif
	mtk_fsm_notifier_register(mdev, MTK_USER_EXCEPT,
				  mtk_exception_fsm_pre_handler, except, FSM_PRIO_1, true);
	mtk_fsm_notifier_register(mdev, MTK_USER_EXCEPT,
				  mtk_exception_fsm_post_handler, except, FSM_PRIO_0, false);
	set_bit(MTK_EXCEPTION_INIT_FLAG, &except->flag);

	return 0;
}

int mtk_exception_exit(struct mtk_md_dev *mdev)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;
	struct mtk_md_except *except;

	except = priv->except;

	clear_bit(MTK_EXCEPTION_INIT_FLAG, &except->flag);
	del_timer(&except->check_link_timer);
	del_timer(&except->guard_timer);
	mtk_fsm_notifier_unregister(mdev, MTK_USER_EXCEPT);
#if IS_ENABLED(CONFIG_MTK_WWAN_PWRCTL_SUPPORT)
	mtk_pwrctl_event_unregister_callback(mtk_exception_pwrctl_handler);
#endif

	devm_kfree(mdev->dev, except);
	return 0;
}
