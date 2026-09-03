// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles tethering offload related information.
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Mark Chien <markchien@google.com>
 */
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>                 //kmalloc()
#include <linux/uaccess.h>              //copy_to/from_user()
#include <linux/ioctl.h>
#include <linux/err.h>
#include <nep/nep.h>
#include <net/nep_cmd_rpc_service/noa_nep_cmd_dispatch.h>
#include <net/nep_cmd_rpc_service/noa_nep_cmd_rpc.h>

#include "noa_def.h"
#include <common/map_def.h>

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
#include "noa_vpn_manager.h"
#endif

#define TEST_SEND _IOW('a','a',int32_t*)

/*
** Function Prototypes
*/
int init_noa_tethering_manager(void);
void deinit_noa_tethering_manager(void);
void send_callback(uint32_t type, const void *data);

struct noa_callback_node {
	struct list_head cb_list;
	union {
		char cb_buffer[sizeof(NoaCallbackCommand)];
		NoaCallbackCommand cb;
	};
};

struct file_prvdata {
	wait_queue_head_t read_queue;
	struct list_head pending_neteng_callbacks;
	struct mutex pending_cb_lock;
	atomic_t pending_cb_count;
};
