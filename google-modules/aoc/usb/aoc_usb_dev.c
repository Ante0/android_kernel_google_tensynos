// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2020 Google LLC. All Rights Reserved.
 *
 * Interface to the AoC USB control service
 */

#define pr_fmt(fmt) "aoc_usb_control: " fmt

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/usb/google-role-sw.h>
#include "aoc.h"
#include "aoc-interface.h"
#include "aoc_usb.h"
#include "usb_offload.h"

#define AOC_USB_NAME "aoc_usb"
#define COMPLETION_TIMEOUT_MS 1000
#define GVOTABLE_POLLING_MS 100
#define WORK_MAX_RETRIES 3
#define AOC_USB_WAKELOCK_NAME_LENGTH (AOC_SERVICE_NAME_LENGTH + 8)

enum { NONBLOCKING = 0, BLOCKING = 1 };

enum {
	TYPE_SCRATCH_PAD = 0,
	TYPE_DEVICE_CONTEXT,
	TYPE_END_OF_SETUP,
	TYPE_DCBAA
};

static DECLARE_COMPLETION(work_done);
static DECLARE_COMPLETION(exit_host);
static BLOCKING_NOTIFIER_HEAD(aoc_usb_notifier_list);

int register_aoc_usb_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&aoc_usb_notifier_list, nb);
}

int unregister_aoc_usb_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&aoc_usb_notifier_list, nb);
}

int xhci_setup_done(void)
{
	if (!usb_offload_get_aoc_ready())
		return -EAGAIN;

	blocking_notifier_call_chain(&aoc_usb_notifier_list, SETUP_DONE, NULL);
	return 0;
}

int xhci_sync_conn_stat(unsigned int bus_id, unsigned int dev_num, unsigned int slot_id,
			       unsigned int conn_stat)
{
	struct conn_stat_args args;

	if (!usb_offload_get_aoc_ready())
		return -EAGAIN;

	args.bus_id = bus_id;
	args.dev_num = dev_num;
	args.slot_id = slot_id;
	args.conn_stat = conn_stat;
	blocking_notifier_call_chain(&aoc_usb_notifier_list, SYNC_CONN_STAT, &args);

	return 0;
}

int usb_host_mode_state_notify(enum aoc_usb_state usb_state)
{
	return xhci_sync_conn_stat(0, 0, 0, usb_state);
}

static ssize_t aoc_usb_send_command(struct aoc_usb_drvdata *drvdata,
				    void *in_cmd, size_t in_size, void *out_cmd,
				    size_t out_size)
{
	struct aoc_service_dev *adev = drvdata->adev;
	ssize_t ret;

	ret = mutex_lock_interruptible(&drvdata->lock);
	if (ret != 0)
		return ret;

	__pm_stay_awake(drvdata->ws);

	if (aoc_service_flush_read_data(adev))
		dev_err(&drvdata->adev->dev, "Previous response left in channel\n");

	dev_dbg(&drvdata->adev->dev, "send cmd id [%u]\n", ((struct CMD_CORE_GENERIC *)in_cmd)->parent.id);

	ret = aoc_service_write_timeout(adev, in_cmd, in_size, drvdata->service_timeout);
	if (ret != in_size) {
		dev_err(&drvdata->adev->dev, "aoc service write error, errno = %zd\n", ret);
		goto out;
	}

	ret = aoc_service_read_timeout(adev, out_cmd, out_size, drvdata->service_timeout);
	if (ret != out_size)
		dev_err(&drvdata->adev->dev, "aoc service read error, errno = %zd\n", ret);

out:
	__pm_relax(drvdata->ws);
	mutex_unlock(&drvdata->lock);
	return ret;
}

static int aoc_usb_setup_done(struct aoc_usb_drvdata *drvdata)
{
	int ret;
	struct CMD_USB_CONTROL_SETUP *cmd;

	cmd = kzalloc(sizeof(struct CMD_USB_CONTROL_SETUP), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	AocCmdHdrSet(&cmd->parent,
		     CMD_USB_CONTROL_SETUP_ID,
		     sizeof(*cmd));

	cmd->type = TYPE_END_OF_SETUP;
	cmd->ctx_idx = 0;
	cmd->spbuf_idx = 0;
	cmd->length = 0;
	ret = aoc_usb_send_command(drvdata, cmd, sizeof(*cmd), cmd, sizeof(*cmd));
	if (ret < 0) {
		kfree(cmd);
		return ret;
	}

	kfree(cmd);

	return 0;
}

static int aoc_usb_notify_conn_stat(struct aoc_usb_drvdata *drvdata, void *data)
{
	int ret = 0;
	struct CMD_USB_CONTROL_NOTIFY_CONN_STAT_V2 *cmd;
	struct conn_stat_args *args = data;

	cmd = kzalloc(sizeof(struct CMD_USB_CONTROL_NOTIFY_CONN_STAT_V2), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	AocCmdHdrSet(&cmd->parent,
		     CMD_USB_CONTROL_NOTIFY_CONN_STAT_V2_ID,
		     sizeof(*cmd));

	cmd->bus_id = args->bus_id;
	cmd->dev_num = args->dev_num;
	cmd->slot_id = args->slot_id;
	cmd->conn_state = args->conn_stat;

	if (cmd->bus_id == 0 && cmd->dev_num == 0 && cmd->slot_id == 0) {
		drvdata->usb_state = cmd->conn_state;
		if (drvdata->recovery_type == EXIT_HOST_MODE) {
			if (drvdata->usb_state == USB_CONNECTED)
				init_completion(&exit_host);
			else
				complete(&exit_host);
		}
	}

	ret = aoc_usb_send_command(drvdata, cmd, sizeof(*cmd), cmd, sizeof(*cmd));
	if (ret < 0) {
		kfree(cmd);
		return ret;
	}

	kfree(cmd);

	return 0;
}

static int aoc_usb_notify(struct notifier_block *this,
			  unsigned long code, void *data)
{
	struct aoc_usb_drvdata *drvdata =
		container_of(this, struct aoc_usb_drvdata, nb);
	int ret;

	switch (code) {
	case SETUP_DONE:
		ret = aoc_usb_setup_done(drvdata);
		break;
	case SYNC_CONN_STAT:
		ret = aoc_usb_notify_conn_stat(drvdata, data);
		break;
	default:
		dev_warn(&drvdata->adev->dev, "Code %lu is not supported\n", code);
		ret = -EINVAL;
		break;
	}

	if (ret < 0)
		dev_err(&drvdata->adev->dev, "Fail to handle code %lu, ret = %d", code, ret);
	return ret;
}

static void usb_host_aoc_ready_work(struct work_struct *ws)
{
	int ret = 0;
	struct aoc_usb_drvdata *drvdata =
				container_of(ws, struct aoc_usb_drvdata, aoc_ready_work.work);
	bool aoc_ready = usb_offload_get_aoc_ready();

	drvdata->usb_data_role_votable =
		gvotable_election_get_handle(VOTABLE_USB_DATA_ROLE);

	if (drvdata->usb_data_role_votable) {
		ret = gvotable_cast_vote(drvdata->usb_data_role_votable, AOC_VOTER,
			(void *)(long)(aoc_ready ? USB_ROLE_HOST : USB_ROLE_NONE), 1);
		if (ret < 0) {
			dev_err(&drvdata->adev->dev,
				"Fail to cast vote: %s (ret = %d), retry count: %d (%s)\n",
				aoc_ready ? "USB_ROLE_HOST" : "USB_ROLE_NONE", ret,
				drvdata->aoc_ready_work_retries,
				drvdata->aoc_ready_work_retries < WORK_MAX_RETRIES ?
				"retry again later" : "retry limit exceeded");
			if (drvdata->aoc_ready_work_retries++ < WORK_MAX_RETRIES) {
				mod_delayed_work(system_wq, &drvdata->aoc_ready_work,
						 msecs_to_jiffies(GVOTABLE_POLLING_MS));
			} else {
				if (!aoc_ready)
					dev_err(&drvdata->adev->dev,
					"ssr not updated to role-switch driver, might ramdump!\n");
				complete(&work_done);
			}
		} else {
			complete(&work_done);
		}
	} else {
		dev_info(&drvdata->adev->dev, "gvotable %s doesn't exist, vote again later\n",
					     VOTABLE_USB_DATA_ROLE);
		mod_delayed_work(system_wq, &drvdata->aoc_ready_work,
				 msecs_to_jiffies(GVOTABLE_POLLING_MS));
	}
}

static int aoc_usb_probe(struct aoc_service_dev *adev)
{
	struct device *dev = &adev->dev;
	struct aoc_usb_drvdata *drvdata;
	char ws_name[AOC_USB_WAKELOCK_NAME_LENGTH];
	struct device_node *np = NULL;
	int ret;

	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		return -ENOMEM;
	}

	drvdata->adev = adev;
	mutex_init(&drvdata->lock);
	np = of_find_compatible_node(np, NULL, "google,usb-role-sw");
	if (!np) {
		dev_err(dev, "device node missing: google,usb-role-sw\n");
		drvdata->recovery_type = EXIT_HOST_MODE;
	} else {
		if (of_find_property(np, "host-mode-aoc-optional", NULL))
			drvdata->recovery_type = DISABLE_OFFLOAD;
		else
			drvdata->recovery_type = EXIT_HOST_MODE;
		of_node_put(np);
	}

	scnprintf(ws_name, sizeof(ws_name), "aoc_%s", dev_name(dev));
	drvdata->ws = wakeup_source_register(dev, ws_name);
	if (!drvdata->ws) {
		dev_err(&drvdata->adev->dev, "wakeup_source_register failed!\n");
		kfree(drvdata);
		return -ENOMEM;
	}

	drvdata->service_timeout = msecs_to_jiffies(100);
	drvdata->nb.notifier_call = aoc_usb_notify;
	ret = usb_offload_set_aoc_ready(true);
	if (ret)
		dev_err(&drvdata->adev->dev, "fail to set aoc status (ret = %d)\n", ret);
	register_aoc_usb_notifier(&drvdata->nb);
	dev_set_drvdata(dev, drvdata);

	drvdata->usb_data_role_votable = gvotable_election_get_handle(VOTABLE_USB_DATA_ROLE);
	if (gvotable_get_int_vote(drvdata->usb_data_role_votable, AOC_VOTER) == USB_ROLE_HOST) {
		usb_audio_offload_resume();
	} else {
		/* AoC is ready to switch to USB data role host. */
		INIT_DEFERRABLE_WORK(&drvdata->aoc_ready_work, usb_host_aoc_ready_work);
		init_completion(&exit_host);
		complete(&exit_host);
		init_completion(&work_done);
		drvdata->aoc_ready_work_retries = 0;
		mod_delayed_work(system_wq, &drvdata->aoc_ready_work, 0);
	}

	return 0;
}

static int aoc_usb_remove(struct aoc_service_dev *adev)
{
	struct aoc_usb_drvdata *drvdata = dev_get_drvdata(&adev->dev);
	int ret = 0;

	ret = usb_offload_set_aoc_ready(false);
	if (ret)
		dev_err(&drvdata->adev->dev, "fail to set aoc status (ret = %d)\n", ret);

	if (drvdata->recovery_type == DISABLE_OFFLOAD) {
		usb_audio_offload_pause();
	} else {
		/* AoC is not ready to switch to USB data role host. */
		init_completion(&work_done);
		drvdata->aoc_ready_work_retries = 0;
		mod_delayed_work(system_wq, &drvdata->aoc_ready_work, 0);
		ret = wait_for_completion_timeout(&work_done,
						  msecs_to_jiffies(COMPLETION_TIMEOUT_MS));
		if (!ret) {
			cancel_delayed_work_sync(&drvdata->aoc_ready_work);
			dev_err(&drvdata->adev->dev, "Timedout executing aoc_ready_work.\n");
		}

		/* System needs to completely exit USB host mode before proceed. */
		/*
		 * TODO: Race condition exists when system switch to host after this completion
		 * check. In this case, usb data role will still be host during aoc offtime,
		 * leading to possible ramdump when xhci driver try to access aoc sram during its
		 * offtime. Need to discover means for eliminating/shrinking the race window.
		 */
		ret = wait_for_completion_timeout(&exit_host,
						  msecs_to_jiffies(COMPLETION_TIMEOUT_MS));
		if (!ret) {
			dev_err(&drvdata->adev->dev,
				"Timedout exiting usb host, ramdump might occur!\n");
			ret = -ETIMEDOUT;
		} else {
			ret = 0;
		}
	}

	unregister_aoc_usb_notifier(&drvdata->nb);
	wakeup_source_unregister(drvdata->ws);
	mutex_destroy(&drvdata->lock);

	kfree(drvdata);

	return ret;
}

static const char *const aoc_usb_service_names[] = {
	"usb_control",
	NULL,
};

static struct aoc_driver aoc_usb_driver = {
	.drv = {
		.name = AOC_USB_NAME,
	},
	.service_names = aoc_usb_service_names,
	.probe = aoc_usb_probe,
	.remove = aoc_usb_remove,
};

static int __init aoc_usb_init(void)
{
	usb_offload_helper_init();

	return aoc_driver_register(&aoc_usb_driver);
}

static void __exit aoc_usb_exit(void)
{
	usb_offload_helper_exit();
	aoc_driver_unregister(&aoc_usb_driver);
}

module_init(aoc_usb_init);
module_exit(aoc_usb_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Howard Yen (Google)");
MODULE_DESCRIPTION("USB driver for AoC");
