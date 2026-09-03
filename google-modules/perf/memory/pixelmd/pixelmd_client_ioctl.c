// SPDX-License-Identifier: GPL-2.0

#include "pixelmd_api.h"
#include "pixelmd_client.h"
#include "pixelmd_events.h"
#include "pixelmd_cmd_lmkd_kill.h"
#include "pixelmd_cmd_kswapd.h"

#include <linux/errno.h>
#include <linux/uaccess.h>

static long cmd_get_module_info(struct pixelmd_client *client, void __user *param)
{
	struct pixelmd_module_info info = {
		.protocol_version = PIXELMD_PROTOCOL_VERSION,
	};

	if (copy_to_user(param, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static long cmd_enable_disable_source(struct pixelmd_client *client, void __user *param,
				      bool enable)
{
	__u32 source;

	if (copy_from_user(&source, param, sizeof(source)))
		return -EFAULT;

	if (source >= PIXELMD_NUM_SOURCES)
		return -EINVAL;

	pixelmd_client_enable_source(client, (enum pixelmd_source)source, enable);

	return 0;
}

static long cmd_generate_test_event(struct pixelmd_client *client, void __user *param)
{
	__u32 cookie;

	if (copy_from_user(&cookie, param, sizeof(cookie)))
		return -EFAULT;

	if (cookie)
		pixelmd_client_write_event(client, PIXELMD_SOURCE_TEST_GENERATOR,
					   PIXELMD_EVENT_TEST_COOKIE, &cookie, sizeof(cookie));
	else
		pixelmd_client_write_event(client, PIXELMD_SOURCE_TEST_GENERATOR,
					   PIXELMD_EVENT_TEST, NULL, 0);

	return 0;
}

long pixelmd_client_ioctl(struct pixelmd_client *client, unsigned int cmd, void __user *param)
{
	switch (cmd) {
	case PIXELMD_CMD_GET_MODULE_INFO:
		return cmd_get_module_info(client, param);
	case PIXELMD_CMD_ENABLE_SOURCE:
		return cmd_enable_disable_source(client, param, true);
	case PIXELMD_CMD_DISABLE_SOURCE:
		return cmd_enable_disable_source(client, param, false);
	case PIXELMD_CMD_GENERATE_TEST_EVENT:
		return cmd_generate_test_event(client, param);
	case PIXELMD_CMD_LMKD_KILL:
		return pixelmd_cmd_lmkd_kill(param);
	case PIXELMD_CMD_NUDGE_KSWAPD:
		return pixelmd_cmd_nudge_kswapd();
	}

	return -EINVAL;
}
