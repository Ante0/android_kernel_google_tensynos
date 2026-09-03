// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS IOCTL Handler
 *
 * Copyright (c) 2024 Google, LLC
 */

#include "lwis_ioctl_past.h"
#include "lwis_commands.h"
#include <linux/build_bug.h>

static void populate_transaction_info_from_cmd_v6(void *_cmd,
						  struct lwis_transaction *k_transaction)
{
	struct lwis_cmd_transaction_info_v6 *cmd = _cmd;

	k_transaction->info.trigger_event_id = cmd->info.trigger_event_id;
	k_transaction->info.trigger_event_counter = cmd->info.trigger_event_counter;
	BUILD_BUG_ON(sizeof(k_transaction->info.trigger_condition) !=
		     sizeof(cmd->info.trigger_condition));
	memcpy(&k_transaction->info.trigger_condition, &cmd->info.trigger_condition,
	       sizeof(struct lwis_transaction_trigger_condition));
	k_transaction->info.create_completion_fence_fd = cmd->info.create_completion_fence_fd;
	k_transaction->info.create_completion_fence_signal_fd =
		cmd->info.create_completion_fence_signal_fd;
	k_transaction->info.num_io_entries = cmd->info.num_io_entries;
	k_transaction->info.io_entries = (void *)cmd->info.io_entries;
	k_transaction->info.run_in_event_context = cmd->info.run_in_event_context;
	k_transaction->info.reserved = cmd->info.reserved;
	k_transaction->info.emit_success_event_id = cmd->info.emit_success_event_id;
	k_transaction->info.emit_error_event_id = cmd->info.emit_error_event_id;
	k_transaction->info.is_level_triggered = cmd->info.is_level_triggered;
	k_transaction->info.is_high_priority_transaction = cmd->info.is_high_priority_transaction;
	BUILD_BUG_ON(sizeof(k_transaction->info.transaction_name) !=
		     sizeof(cmd->info.transaction_name));
	memcpy(k_transaction->info.transaction_name, cmd->info.transaction_name,
	       sizeof(k_transaction->info.transaction_name));
	k_transaction->info.num_nested_transactions = cmd->info.num_nested_transactions;
	BUILD_BUG_ON(sizeof(k_transaction->info.nested_transaction_ids) !=
		     sizeof(cmd->info.nested_transaction_ids));
	memcpy(k_transaction->info.nested_transaction_ids, cmd->info.nested_transaction_ids,
	       sizeof(cmd->info.nested_transaction_ids));
	k_transaction->info.num_completion_fences = cmd->info.num_completion_fences;
	BUILD_BUG_ON(sizeof(k_transaction->info.completion_fence_fds) !=
		     sizeof(cmd->info.completion_fence_fds));
	memcpy(k_transaction->info.completion_fence_fds, cmd->info.completion_fence_fds,
	       sizeof(k_transaction->info.completion_fence_fds));
	k_transaction->info.id = cmd->info.id;
	k_transaction->info.current_trigger_event_counter = cmd->info.current_trigger_event_counter;
	k_transaction->info.submission_timestamp_ns = cmd->info.submission_timestamp_ns;

	k_transaction->info.num_device_io_entries = 0;
	k_transaction->info.device_io_entries = NULL;
	k_transaction->info.skip_device_io_entries_if_powered_off = false;
	k_transaction->device_io_fps = NULL;
}

static void populate_cmd_v6_info_from_transaction(void *_cmd,
						  struct lwis_transaction *k_transaction, int error)
{
	struct lwis_cmd_transaction_info_v6 *cmd = _cmd;

	cmd->info.trigger_event_id = k_transaction->info.trigger_event_id;
	cmd->info.trigger_event_counter = k_transaction->info.trigger_event_counter;
	BUILD_BUG_ON(sizeof(cmd->info.trigger_condition) !=
		     sizeof(k_transaction->info.trigger_condition));
	memcpy(&cmd->info.trigger_condition, &k_transaction->info.trigger_condition,
	       sizeof(struct lwis_transaction_trigger_condition));
	cmd->info.create_completion_fence_fd = k_transaction->info.create_completion_fence_fd;
	cmd->info.create_completion_fence_signal_fd =
		k_transaction->info.create_completion_fence_signal_fd;
	cmd->info.num_io_entries = k_transaction->info.num_io_entries;
	cmd->info.io_entries = (void *)k_transaction->info.io_entries;
	cmd->info.run_in_event_context = k_transaction->info.run_in_event_context;
	cmd->info.reserved = k_transaction->info.reserved;
	cmd->info.emit_success_event_id = k_transaction->info.emit_success_event_id;
	cmd->info.emit_error_event_id = k_transaction->info.emit_error_event_id;
	cmd->info.is_level_triggered = k_transaction->info.is_level_triggered;
	cmd->info.is_high_priority_transaction = k_transaction->info.is_high_priority_transaction;
	BUILD_BUG_ON(sizeof(cmd->info.transaction_name) !=
		     sizeof(k_transaction->info.transaction_name));
	memcpy(cmd->info.transaction_name, k_transaction->info.transaction_name,
	       sizeof(cmd->info.transaction_name));
	cmd->info.num_nested_transactions = k_transaction->info.num_nested_transactions;
	BUILD_BUG_ON(sizeof(cmd->info.nested_transaction_ids) !=
		     sizeof(k_transaction->info.nested_transaction_ids));
	memcpy(cmd->info.nested_transaction_ids, k_transaction->info.nested_transaction_ids,
	       sizeof(cmd->info.nested_transaction_ids));
	BUILD_BUG_ON(sizeof(cmd->info.completion_fence_fds) !=
		     sizeof(k_transaction->info.completion_fence_fds));
	memcpy(cmd->info.completion_fence_fds, k_transaction->info.completion_fence_fds,
	       sizeof(cmd->info.completion_fence_fds));
	cmd->info.id = k_transaction->info.id;
	cmd->info.current_trigger_event_counter = k_transaction->info.current_trigger_event_counter;
	cmd->info.submission_timestamp_ns = k_transaction->info.submission_timestamp_ns;

	if (error != 0)
		cmd->info.id = LWIS_ID_INVALID;
}

struct cmd_transaction_submit_ops transaction_cmd_v6_ops = {
	.cmd_size = sizeof(struct lwis_cmd_transaction_info_v6),
	.populate_transaction_info_from_cmd = populate_transaction_info_from_cmd_v6,
	.populate_cmd_info_from_transaction = populate_cmd_v6_info_from_transaction,
};

static void populate_transaction_info_from_cmd_v7(void *_cmd,
						  struct lwis_transaction *k_transaction)
{
	struct lwis_cmd_transaction_info_v7 *cmd = _cmd;

	k_transaction->info.trigger_event_id = cmd->info.trigger_event_id;
	k_transaction->info.trigger_event_counter = cmd->info.trigger_event_counter;
	BUILD_BUG_ON(sizeof(k_transaction->info.trigger_condition) !=
		     sizeof(cmd->info.trigger_condition));
	memcpy(&k_transaction->info.trigger_condition, &cmd->info.trigger_condition,
	       sizeof(struct lwis_transaction_trigger_condition));
	k_transaction->info.create_completion_fence_fd = cmd->info.create_completion_fence_fd;
	k_transaction->info.create_completion_fence_signal_fd =
		cmd->info.create_completion_fence_signal_fd;
	k_transaction->info.num_io_entries = cmd->info.num_io_entries;
	k_transaction->info.io_entries = (void *)cmd->info.io_entries;
	k_transaction->info.run_in_event_context = cmd->info.run_in_event_context;
	k_transaction->info.reserved = cmd->info.reserved;
	k_transaction->info.emit_success_event_id = cmd->info.emit_success_event_id;
	k_transaction->info.emit_error_event_id = cmd->info.emit_error_event_id;
	k_transaction->info.is_level_triggered = cmd->info.is_level_triggered;
	k_transaction->info.is_high_priority_transaction = cmd->info.is_high_priority_transaction;
	BUILD_BUG_ON(sizeof(k_transaction->info.transaction_name) !=
		     sizeof(cmd->info.transaction_name));
	memcpy(k_transaction->info.transaction_name, cmd->info.transaction_name,
	       sizeof(k_transaction->info.transaction_name));
	k_transaction->info.num_nested_transactions = cmd->info.num_nested_transactions;
	BUILD_BUG_ON(sizeof(k_transaction->info.nested_transaction_ids) !=
		     sizeof(cmd->info.nested_transaction_ids));
	memcpy(k_transaction->info.nested_transaction_ids, cmd->info.nested_transaction_ids,
	       sizeof(cmd->info.nested_transaction_ids));
	k_transaction->info.num_completion_fences = cmd->info.num_completion_fences;
	BUILD_BUG_ON(sizeof(k_transaction->info.completion_fence_fds) !=
		     sizeof(cmd->info.completion_fence_fds));
	memcpy(k_transaction->info.completion_fence_fds, cmd->info.completion_fence_fds,
	       sizeof(k_transaction->info.completion_fence_fds));
	k_transaction->info.id = cmd->info.id;
	k_transaction->info.current_trigger_event_counter = cmd->info.current_trigger_event_counter;
	k_transaction->info.submission_timestamp_ns = cmd->info.submission_timestamp_ns;

	k_transaction->info.minimum_trigger_delay_ns = cmd->info.minimum_trigger_delay_ns;

	k_transaction->info.num_device_io_entries = 0;
	k_transaction->info.device_io_entries = NULL;
	k_transaction->info.skip_device_io_entries_if_powered_off = false;
	k_transaction->device_io_fps = NULL;
}

static void populate_cmd_v7_info_from_transaction(void *_cmd,
						  struct lwis_transaction *k_transaction, int error)
{
	struct lwis_cmd_transaction_info_v7 *cmd = _cmd;

	cmd->info.trigger_event_id = k_transaction->info.trigger_event_id;
	cmd->info.trigger_event_counter = k_transaction->info.trigger_event_counter;
	BUILD_BUG_ON(sizeof(cmd->info.trigger_condition) !=
		     sizeof(k_transaction->info.trigger_condition));
	memcpy(&cmd->info.trigger_condition, &k_transaction->info.trigger_condition,
	       sizeof(struct lwis_transaction_trigger_condition));
	cmd->info.create_completion_fence_fd = k_transaction->info.create_completion_fence_fd;
	cmd->info.create_completion_fence_signal_fd =
		k_transaction->info.create_completion_fence_signal_fd;
	cmd->info.num_io_entries = k_transaction->info.num_io_entries;
	cmd->info.io_entries = (void *)k_transaction->info.io_entries;
	cmd->info.run_in_event_context = k_transaction->info.run_in_event_context;
	cmd->info.reserved = k_transaction->info.reserved;
	cmd->info.emit_success_event_id = k_transaction->info.emit_success_event_id;
	cmd->info.emit_error_event_id = k_transaction->info.emit_error_event_id;
	cmd->info.is_level_triggered = k_transaction->info.is_level_triggered;
	cmd->info.is_high_priority_transaction = k_transaction->info.is_high_priority_transaction;
	BUILD_BUG_ON(sizeof(cmd->info.transaction_name) !=
		     sizeof(k_transaction->info.transaction_name));
	memcpy(cmd->info.transaction_name, k_transaction->info.transaction_name,
	       sizeof(cmd->info.transaction_name));
	cmd->info.num_nested_transactions = k_transaction->info.num_nested_transactions;
	BUILD_BUG_ON(sizeof(cmd->info.nested_transaction_ids) !=
		     sizeof(k_transaction->info.nested_transaction_ids));
	memcpy(cmd->info.nested_transaction_ids, k_transaction->info.nested_transaction_ids,
	       sizeof(cmd->info.nested_transaction_ids));
	BUILD_BUG_ON(sizeof(cmd->info.completion_fence_fds) !=
		     sizeof(k_transaction->info.completion_fence_fds));
	memcpy(cmd->info.completion_fence_fds, k_transaction->info.completion_fence_fds,
	       sizeof(cmd->info.completion_fence_fds));
	cmd->info.id = k_transaction->info.id;
	cmd->info.current_trigger_event_counter = k_transaction->info.current_trigger_event_counter;
	cmd->info.submission_timestamp_ns = k_transaction->info.submission_timestamp_ns;

	cmd->info.minimum_trigger_delay_ns = k_transaction->info.minimum_trigger_delay_ns;

	if (error != 0)
		cmd->info.id = LWIS_ID_INVALID;
}

struct cmd_transaction_submit_ops transaction_cmd_v7_ops = {
	.cmd_size = sizeof(struct lwis_cmd_transaction_info_v7),
	.populate_transaction_info_from_cmd = populate_transaction_info_from_cmd_v7,
	.populate_cmd_info_from_transaction = populate_cmd_v7_info_from_transaction,
};

static int fetch_num_qos_settings_v4(void *k_msg)
{
	return ((struct lwis_cmd_dpm_qos_update_v4 *)k_msg)->reqs.num_settings;
}

static int populate_dpm_qos_info_from_cmd_v4(struct lwis_qos_setting *k_qos_setting,
					     void *k_msg_raw, int idx)
{
	struct lwis_cmd_dpm_qos_update_v4 *cmd = k_msg_raw;
	struct lwis_qos_setting_v4 *k_qos_setting_ptr = &cmd->reqs.qos_settings[idx];

	if (copy_from_user((void *)k_qos_setting, (void __user *)k_qos_setting_ptr,
			   sizeof(struct lwis_qos_setting_v4)))
		return -EFAULT;

	return 0;
}

const struct cmd_dpm_qos_update_ops cmd_dpm_qos_v4_ops = {
	.cmd_size = sizeof(struct lwis_cmd_dpm_qos_update_v4),
	.fetch_num_qos_settings = fetch_num_qos_settings_v4,
	.populate_dpm_qos_info_from_cmd = populate_dpm_qos_info_from_cmd_v4,
};

static int fetch_num_qos_settings_v3(void *k_msg)
{
	return ((struct lwis_cmd_dpm_qos_update_v3 *)k_msg)->reqs.num_settings;
}

static int populate_dpm_qos_info_from_cmd_v3(struct lwis_qos_setting *k_qos_setting,
					     void *k_msg_raw, int idx)
{
	struct lwis_cmd_dpm_qos_update_v3 *cmd = k_msg_raw;
	struct lwis_qos_setting_v3 *k_qos_setting_ptr = &cmd->reqs.qos_settings[idx];

	if (copy_from_user((void *)k_qos_setting, (void __user *)k_qos_setting_ptr,
			   sizeof(struct lwis_qos_setting_v3)))
		return -EFAULT;

	k_qos_setting->qos_voting_entity = 0;

	return 0;
}

const struct cmd_dpm_qos_update_ops cmd_dpm_qos_v3_ops = {
	.cmd_size = sizeof(struct lwis_cmd_dpm_qos_update_v3),
	.fetch_num_qos_settings = fetch_num_qos_settings_v3,
	.populate_dpm_qos_info_from_cmd = populate_dpm_qos_info_from_cmd_v3,
};
