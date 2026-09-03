// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2025 Google LLC.
 *
 */
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/completion.h>

#include "noa_wlan_cmd_dispatch.h"
#include "wlan/noa_wlan_hw.h"
#include "wlan/noa_wlan.h"
#include "noa_wlan_rpc.h"

#define WLAN_CMD_COMPLETION_TIMEOUT_MS 3000

enum wlan_cmd_type {
	WLAN_CMD_FW_REQUEST,
	WLAN_CMD_HW_RINGBELL,
};

struct wlan_command {
	struct list_head list;
	enum wlan_cmd_type type;
	u32 subtype;
	void *data;
	size_t data_len;
	struct completion cmd_completion;
	int result;
	bool async;
};

static struct workqueue_struct *noa_wlan_wq;
static struct work_struct noa_wlan_work;

static LIST_HEAD(noa_wlan_cmd_queue);
static LIST_HEAD(noa_wlan_cmd_wait_queue);
static spinlock_t noa_wlan_queue_lock;
static spinlock_t noa_wlan_wait_queue_lock;

static int _noa_wlan_fw_request_send(int cmd, void *msg, size_t len)
{
	if (IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT))
		return __noa_wlan_fw_request_send_rpc(cmd, msg, len);
	else
		return __noa_wlan_fw_request_send_sim(cmd, msg);
}

static void noa_wlan_command_worker(struct work_struct *work)
{
	struct wlan_command *cmd = NULL;
	unsigned long flags;
	bool async;

	spin_lock_irqsave(&noa_wlan_queue_lock, flags);
	if (!list_empty(&noa_wlan_cmd_queue)) {
		cmd = list_first_entry(&noa_wlan_cmd_queue, struct wlan_command, list);
		list_del_init(&cmd->list);
	}
	spin_unlock_irqrestore(&noa_wlan_queue_lock, flags);

	if (!cmd) {
		pr_err("%s(): no command found!\n", __func__);
		return;
	}

	// Record the async value to avoid use-after-free.
	async = cmd->async;

	// Commands are queued and held in a waiting state before being sent
	// to the firmware for driver mode execution.
	if (!async) {
		spin_lock_irqsave(&noa_wlan_wait_queue_lock, flags);
		list_add_tail(&cmd->list, &noa_wlan_cmd_wait_queue);
		spin_unlock_irqrestore(&noa_wlan_wait_queue_lock, flags);
	}

	// pr_info("Processing WLAN command type: %d, subtype: %d\n", cmd->type, cmd->subtype);
	switch (cmd->type) {
	case WLAN_CMD_FW_REQUEST:
		cmd->result = _noa_wlan_fw_request_send(cmd->subtype, cmd->data, cmd->data_len);
		break;
	case WLAN_CMD_HW_RINGBELL:
		cmd->result = noa_wlan_hw_ringbell_ncp(NULL, cmd->subtype);
		break;
	default:
		pr_err("Unknown WLAN command type: %d\n", cmd->type);
		cmd->result = -EINVAL;
		break;
	}

	// Asynchronous commands are freed here; synchronous commands are freed by their caller.
	if (async) {
		kfree(cmd->data);
		kfree(cmd);
	}
	spin_lock_irqsave(&noa_wlan_queue_lock, flags);
	if (!list_empty(&noa_wlan_cmd_queue)) {
		queue_work(noa_wlan_wq, &noa_wlan_work);
	}
	spin_unlock_irqrestore(&noa_wlan_queue_lock, flags);
}

static int _noa_wlan_fw_request_send_sync(u32 type, u32 subtype, void *data, size_t len)
{
	struct wlan_command *cmd;
	int ret = -ENOMEM;
	unsigned long flags;

	// pr_info("%s(): %d, type %d, subtype %d\n", __func__, __LINE__, type, subtype);
	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		pr_err("Failed to allocate WLAN command for FW request.\n");
		return ret;
	}
	cmd->data = kmemdup(data, len, GFP_KERNEL);
	if (!cmd->data && len > 0) {
		pr_err("Failed to allocate data for FW request command.\n");
		kfree(cmd);
		return ret;
	}
	cmd->type = type;
	cmd->subtype = subtype;
	cmd->data_len = len;
	cmd->async = false;
	init_completion(&cmd->cmd_completion);

	spin_lock_irqsave(&noa_wlan_queue_lock, flags);
	list_add_tail(&cmd->list, &noa_wlan_cmd_queue);
	queue_work(noa_wlan_wq, &noa_wlan_work);
	spin_unlock_irqrestore(&noa_wlan_queue_lock, flags);

	ret = wait_for_completion_timeout(&cmd->cmd_completion,
					  msecs_to_jiffies(WLAN_CMD_COMPLETION_TIMEOUT_MS));
	if (ret == 0) {
		ret = -ETIMEDOUT;
		spin_lock_irqsave(&noa_wlan_queue_lock, flags);
		spin_lock(&noa_wlan_wait_queue_lock);
		list_del_init(&cmd->list);
		spin_unlock(&noa_wlan_wait_queue_lock);
		spin_unlock_irqrestore(&noa_wlan_queue_lock, flags);
	} else {
		ret = cmd->result;
	}
	kfree(cmd->data);
	kfree(cmd);
	return ret;
}

static int _noa_wlan_fw_request_send_async(u32 type, u32 subtype, void *data, size_t len)
{
	struct wlan_command *cmd;
	unsigned long flags;

	// pr_info("%s(): %d, type %d, subtype %d\n", __func__, __LINE__, type, subtype);
	cmd = kmalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd) {
		pr_err("Failed to allocate WLAN command for HW ringbell.\n");
		return -ENOMEM;
	}
	cmd->data = kmemdup(data, len, GFP_ATOMIC);
	if (!cmd->data && len > 0) {
		pr_err("Failed to allocate data for HW ringbell command.\n");
		kfree(cmd);
		return -ENOMEM;
	}
	cmd->type = type;
	cmd->subtype = subtype;
	cmd->data_len = len;
	cmd->async = true;
	init_completion(&cmd->cmd_completion);
	spin_lock_irqsave(&noa_wlan_queue_lock, flags);
	list_add_tail(&cmd->list, &noa_wlan_cmd_queue);
	queue_work(noa_wlan_wq, &noa_wlan_work);
	spin_unlock_irqrestore(&noa_wlan_queue_lock, flags);
	return 0;
}

int noa_wlan_fw_request_send_sync(u32 cmd, void *data, size_t len)
{
	return _noa_wlan_fw_request_send_sync(WLAN_CMD_FW_REQUEST, cmd, data, len);
}

int noa_wlan_fw_request_send_async(u32 cmd, void *data, size_t len)
{
	return _noa_wlan_fw_request_send_async(WLAN_CMD_FW_REQUEST, cmd, data, len);
}

int noa_wlan_fw_request_doorbell_sync(u32 cmd)
{
	return _noa_wlan_fw_request_send_sync(WLAN_CMD_HW_RINGBELL, cmd, NULL, 0);
}

int noa_wlan_fw_request_doorbell_async(u32 cmd)
{
	return _noa_wlan_fw_request_send_async(WLAN_CMD_HW_RINGBELL, cmd, NULL, 0);
}

int noa_wlan_fw_event_cmd_completion(void *msg)
{
	struct wlan_event_cmd_completion *event = msg;
	struct wlan_command *cmd;
	unsigned long flags;

	spin_lock_irqsave(&noa_wlan_wait_queue_lock, flags);
	while (!list_empty(&noa_wlan_cmd_wait_queue)) {
		cmd = list_first_entry(&noa_wlan_cmd_wait_queue, struct wlan_command, list);
		if (cmd->subtype != event->cmd) {
			pr_err("%s(): command id %d is not matched in cmd wait queue id %d.\n",
			       __func__, event->cmd, cmd->subtype);
			cmd->result = -EINVAL;
		} else {
			cmd->result = event->result;
		}
		// pr_info("Completion WLAN command type: %d, subtype: %d\n", cmd->type,
		// cmd->subtype);
		complete(&cmd->cmd_completion);
		list_del_init(&cmd->list);
	}
	spin_unlock_irqrestore(&noa_wlan_wait_queue_lock, flags);
	return 0;
}

void noa_wlan_cmd_wq_init(void *data)
{
	/* initial command workqueue */
	spin_lock_init(&noa_wlan_queue_lock);
	spin_lock_init(&noa_wlan_wait_queue_lock);
	noa_wlan_wq = system_wq;
	INIT_WORK(&noa_wlan_work, noa_wlan_command_worker);
}

static void noa_wlan_cmd_queue_free(struct list_head *queue, spinlock_t *lock)
{
	struct wlan_command *cmd, *next_cmd;
	spin_lock_irq(lock);
	list_for_each_entry_safe(cmd, next_cmd, queue, list) {
		list_del(&cmd->list);
		kfree(cmd->data);
		kfree(cmd);
	}
	spin_unlock_irq(lock);
}

void noa_wlan_cmd_wq_exit(void *data)
{
	flush_work(&noa_wlan_work);
	noa_wlan_cmd_queue_free(&noa_wlan_cmd_queue, &noa_wlan_queue_lock);
	noa_wlan_cmd_queue_free(&noa_wlan_cmd_wait_queue, &noa_wlan_wait_queue_lock);
}
