/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS IOCTL Handler
 *
 * Copyright (c) 2018 Google, LLC
 */

#ifndef LWIS_IOCTL_H_
#define LWIS_IOCTL_H_

#include "lwis_device.h"

/* Operations to handle different versions of the transaction commands.  */
struct cmd_transaction_submit_ops {
	/* Size of the command coming in from user space. */
	size_t cmd_size;
	/* Function to populate LWIS kernel transaction from user space command. */
	void (*populate_transaction_info_from_cmd)(void *cmd, struct lwis_transaction *transaction);
	/* Function to populte the user space command from LWIS kernel transaction
	 * before returning it up. The error has the error value returned from
	 * submitting the transaction.
	 */
	void (*populate_cmd_info_from_transaction)(void *cmd, struct lwis_transaction *transaction,
						   int error);
};

/* Operations to handle different versions of the dpm qos commands.  */
struct cmd_dpm_qos_update_ops {
	/* Size of the command coming in from user space. */
	size_t cmd_size;
	int (*fetch_num_qos_settings)(void *k_msg);
	int (*populate_dpm_qos_info_from_cmd)(struct lwis_qos_setting *k_qos_setting,
					      void *k_msg_raw, int idx);
};

/*
 *  lwis_ioctl_handler: Handle all IOCTL commands via the file descriptor.
 */
int lwis_ioctl_handler(struct lwis_client *lwis_client, unsigned int type, unsigned long param);

#endif /* LWIS_IOCTL_H_ */
