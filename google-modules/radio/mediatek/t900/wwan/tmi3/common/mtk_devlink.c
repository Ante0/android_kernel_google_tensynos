// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/printk.h>
#include <linux/version.h>
#include <net/devlink.h>
#include <net/genetlink.h>

#include "mtk_debug.h"
#include "mtk_devlink.h"
#include "mtk_fsm.h"
#include "mtk_port_io.h"
#ifdef CONFIG_TX00_UT_DEVLINK
#include "ut_devlink_fake.h"
#endif

#define TAG "DEVLINK"
#define DEVLINK_WAKEUP_SOURCE_NAME		"mtk_devlink"

#define MTK_FB_REQ_FLASH			"oem download:"
#define MTK_FB_REQ_COREDUMP			"oem mrdump zip"
#define MTK_FB_REQ_REBOOT			"reboot"
#define MTK_FB_REQ_VERSION			"oem get_version:"
#define MTK_FB_REQ_MAX_LEN			64

#define MTK_FB_RSP_OKAY				"OKAY"
#define MTK_FB_RSP_FLASH_ERR			"ERROR:"
#define MTK_FB_RSP_FLASH_STATUS			"STATUS:"
#define MTK_FB_RSP_COREDUMP_FAIL		"FAIL"
#define MTK_FB_RSP_COREDUMP_DONE		"MRDUMP08_DONE"
#define MTK_FB_RSP_COREDUMP_DONE_LEN		13

#define MTK_FB_FLASH_SUCCESS			0x00
#define MTK_FB_FLASH_ERR_NO_ACTIVE_INF		0x01
#define MTK_FB_FLASH_ERR_IMG_SIZE_LARGE		0x02
#define MTK_FB_FLASH_ERR_PART_NOT_WRITABLE	0x03
#define MTK_FB_FLASH_ERR_PART_NO_EXIST		0x04
#define MTK_FB_FLASH_ERR_PART_ERASE_FAIL	0x05
#define MTK_FB_FLASH_ERR_PART_FULL		0x06
#define MTK_FB_FLASH_ERR_PART_WRITE_FAIL	0x07
#define MTK_FB_FLASH_STATUS_PART_NOT_EXIST	0x08
#define MTK_FB_FLASH_STATUS_PART_READ_FAIL	0x09
#define MTK_FB_FLASH_STATUS_PART_WRITE_FAIL	0x0A
#define MTK_FB_FLASH_MAX_CODE			0x0B
#define MTK_FB_COREDUMP_PROTOCOL_DEF		0
#define MTK_FB_COREDUMP_PROTOCOL_ONE		1
#define MTK_FB_COREDUMP_PROTOCOL_MAX		2
#define MTK_FB_COREDUMP_PROTOCOL_LEN		3
#define MTK_COREDUMP_V01_DATA_HEAD		0
#define MTK_COREDUMP_V01_DATA_LEN		4
#define MTK_DEVLINK_REGION_DEXTROPOSITION	17
#define MTK_DEVLINK_REGION_BITMASK		0x3
#define MTK_DEVLINK_DEV_STAGE_NORMAL		0x1
#define MTK_DEVLINK_REGION_ORIGINAL_BITMASK	0xF
#define MTK_DEV_STATE_UNKNOWN			0
#define MTK_DEV_STAGE_BROM1			1
#define MTK_DEV_STAGE_BROM2			2
#define MTK_VERSION_RESPONSE_LENGTH		5
#define MTK_VERSION_RSP_MAX_LENGTH		32

enum mtk_fb_ch_t {
	FB_CH_DOWNLOAD = 0,
	FB_CH_COREDUMP = 1,
	FB_CH_MAXCOUNT = 2,
};

enum mtk_fb_cmd {
	FB_CMD_FLASH    = 0,
	FB_CMD_COREDUMP = 1,
	FB_CMD_REBOOT   = 2,
	FB_CMD_VERSION	= 3,
};

enum mtk_fb_cmd_rsp {
	FB_CMD_RSP_SUCCESS         = 0,
	FB_CMD_RSP_TIMEOUT         = 1,
	FB_CMD_RSP_IMG_LARGE       = 2,
	FB_CMD_RSP_WRONG_PARTITION = 3,
	FB_CMD_RSP_PARTITION_FULL  = 4,
	FB_CMD_RSP_WRITE_FORBID    = 5,
	FB_CMD_RSP_ERASE_ERR       = 6,
	FB_CMD_RSP_WRITE_ERR       = 7,
	FB_CMD_RSP_READ_ERR        = 8,
	FB_CMD_RSP_COREDUMP_DONE   = 9,
	FB_CMD_RSP_COREDUMP_FAIL   = 10,
	FB_CMD_RSP_PROTOCOL_ERR    = 11,
	FB_CMD_RSP_COREDUMP_V1	   = 12,
};

enum mtk_fb_rsp_category {
	FB_RSP_CATEGORY_OKAY          = 0,
	FB_RSP_CATEGORY_FLASH_ERR     = 1,
	FB_RSP_CATEGORY_FLASH_STATUS  = 2,
	FB_RSP_CATEGORY_COREDUMP_FAIL = 3,
	FB_RSP_CATEGORY_COREDUMP_DONE = 4,
};

/* For enable HOST_RECVED_LOG region */
static bool enable_host_recved_log;

enum mtk_devlink_region_log_type {
	DEVLINK_REGION_BROM_SRAM_LOG          = 0,
	DEVLINK_REGION_BROM_SRAM_ORIGINAL_LOG = 1,
	DEVLINK_REGION_PL_SRAM_LOG	      = 2,
	DEVLINK_REGION_PL_DRAM_LOG	      = 3,
	DEVLINK_REGION_ATF_DRAM_LOG	      = 4,
	DEVLINK_REGION_HOST_RECVED_LOG	      = 5,
	DEVLINK_REGION_CREATE_NUM	      = 6,
};

struct channel_state {
	enum mtk_fb_ch_t ch_type;
	atomic_t opened;
};

struct mtk_fb_flash_param {
	size_t fw_size;
	const char *partition_name;
};

struct mtk_fb_rsp_category_map {
	char				*rsp_head;
	enum mtk_fb_rsp_category	category;
};

struct mtk_fb_rsp_code_map {
	int device_code;
	enum mtk_fb_cmd_rsp cmd_rsp;
};

struct mtk_devlink_region_map {
	const struct devlink_region_ops *ops;
	enum mtk_dev_log_type		log_type;
};

struct mtk_devlink_coredump_ops {
	enum mtk_fb_cmd_rsp cmd_rsp;
	int (*handler)(struct mtk_md_dev *mdev, struct devlink_fmsg *fmsg);
};

struct mtk_fb_ch {
	struct mtk_md_dev *mdev;
	struct sk_buff_head recv_list;
	struct completion recv_comp;
	void *port;
	atomic_t err;
};

struct mtk_devlink {
	struct mtk_md_dev		*mdev;
	struct devlink			*dl;
	struct devlink_health_reporter	*fw_reporter;
	struct devlink_region		*log_region[DEVLINK_REGION_CREATE_NUM];
	atomic_t			dev_mode;
	unsigned long			flags;
	struct mutex			dl_mutex; /* Protecting devlink contexts. */
	struct completion		download_comp;
	struct mutex			fb_mutex; /* Protecting FB channel contexts. */
	void				*fb_ch;
	struct work_struct		dl_worker;
	struct mutex			get_log_mutex; /* Protecting get device log process. */
	struct wakeup_source		*dl_ws; /* Wakeup source for devlink work. */
	struct channel_state		ch_state[FB_CH_MAXCOUNT];
};

static const struct mtk_fb_rsp_category_map fb_rsp_ctg_tbl[] = {
	{MTK_FB_RSP_OKAY,		FB_RSP_CATEGORY_OKAY},
	{MTK_FB_RSP_FLASH_STATUS,	FB_RSP_CATEGORY_FLASH_STATUS},
	{MTK_FB_RSP_COREDUMP_DONE,	FB_RSP_CATEGORY_COREDUMP_DONE},
	{MTK_FB_RSP_COREDUMP_FAIL,	FB_RSP_CATEGORY_COREDUMP_FAIL},
	{MTK_FB_RSP_FLASH_ERR,		FB_RSP_CATEGORY_FLASH_ERR},
};

static const struct mtk_fb_rsp_code_map fb_rsp_flash_map_tbl[] = {
	{MTK_FB_FLASH_SUCCESS,			FB_CMD_RSP_SUCCESS},
	{MTK_FB_FLASH_ERR_NO_ACTIVE_INF,	FB_CMD_RSP_PROTOCOL_ERR},
	{MTK_FB_FLASH_ERR_IMG_SIZE_LARGE,	FB_CMD_RSP_IMG_LARGE},
	{MTK_FB_FLASH_ERR_PART_NOT_WRITABLE,	FB_CMD_RSP_WRITE_FORBID},
	{MTK_FB_FLASH_ERR_PART_NO_EXIST,	FB_CMD_RSP_WRONG_PARTITION},
	{MTK_FB_FLASH_ERR_PART_ERASE_FAIL,	FB_CMD_RSP_ERASE_ERR},
	{MTK_FB_FLASH_ERR_PART_FULL,		FB_CMD_RSP_PARTITION_FULL},
	{MTK_FB_FLASH_ERR_PART_WRITE_FAIL,	FB_CMD_RSP_WRITE_ERR},
	{MTK_FB_FLASH_STATUS_PART_NOT_EXIST,	FB_CMD_RSP_WRONG_PARTITION},
	{MTK_FB_FLASH_STATUS_PART_READ_FAIL,	FB_CMD_RSP_READ_ERR},
	{MTK_FB_FLASH_STATUS_PART_WRITE_FAIL,	FB_CMD_RSP_WRITE_ERR},
};

static const struct mtk_fb_rsp_code_map fb_rsp_coredump_map_tbl[] = {
	{MTK_FB_COREDUMP_PROTOCOL_DEF,		FB_CMD_RSP_SUCCESS},
	{MTK_FB_COREDUMP_PROTOCOL_ONE,		FB_CMD_RSP_COREDUMP_V1},
	{MTK_FB_COREDUMP_PROTOCOL_MAX,		FB_CMD_RSP_PROTOCOL_ERR},
};

static int mtk_devlink_port_recv_handler(void *param, struct sk_buff *skb)
{
	struct mtk_fb_ch *fb_ch = param;

	if (unlikely(!param || !skb))
		return -EINVAL;

	skb_queue_tail(&fb_ch->recv_list, skb);
	complete(&fb_ch->recv_comp);
	return 0;
}

static void *mtk_fb_ch_open(struct mtk_md_dev *mdev, enum mtk_fb_ch_t ch_type, int flag)
{
	struct mtk_devlink *devlink = mdev->devlink;
	struct mtk_fb_ch *fb_ch;
	char *port_name;

	switch (ch_type) {
	case FB_CH_DOWNLOAD:
		port_name = "FBootDownload";
		break;
	case FB_CH_COREDUMP:
		port_name = "CoreDump";
		break;
	default:
		goto out;
	}

	if (atomic_cmpxchg(&devlink->ch_state[ch_type].opened, 0, 1) != 0) {
		MTK_ERR(mdev, "Channel %s is already opened\n", port_name);
		goto out;
	}

	fb_ch = kzalloc(sizeof(*fb_ch), GFP_KERNEL);
	if (unlikely(!fb_ch)) {
		MTK_ERR(mdev, "Failed to allocate channel memory\n");
		goto out;
	}
	fb_ch->mdev = mdev;
	skb_queue_head_init(&fb_ch->recv_list);
	init_completion(&fb_ch->recv_comp);

	fb_ch->port = mtk_port_internal_open(mdev, port_name, flag);
	if (unlikely(!fb_ch->port)) {
		MTK_ERR(mdev, "Failed to open the port:%s\n", port_name);
		goto err_free_fb_ch;
	}
	mtk_port_internal_recv_register(fb_ch->port, mtk_devlink_port_recv_handler, fb_ch);
	atomic_set(&fb_ch->err, 0);
	return fb_ch;
err_free_fb_ch:
	kfree(fb_ch);
out:
	return NULL;
}

static int mtk_fb_ch_send_raw(void *ch, struct sk_buff *skb)
{
	struct mtk_fb_ch *fb_ch = ch;
	unsigned int len;
	int ret;

	ret = atomic_read(&fb_ch->err);
	if (unlikely(ret)) {
		MTK_ERR(fb_ch->mdev, "Channel error: %d\n", ret);
		return ret;
	}
	len = skb->len;
	ret = mtk_port_internal_write(fb_ch->port, skb);
	if (unlikely(ret > 0 && ret != len)) {
		MTK_ERR(fb_ch->mdev, "Failed to write skb, ret=%d,len=%d\n", ret, len);
		ret = -EIO;
	}

	return (ret == len) ? 0 : ret;
}

static int mtk_fb_ch_recv_raw(void *ch, struct sk_buff **sk_buf, unsigned int timeout)
{
	struct mtk_fb_ch *fb_ch = ch;
	unsigned long timeout_jiff;
	struct sk_buff *skb = NULL;
	int ret = 0;

	timeout_jiff = msecs_to_jiffies(timeout);
	while (!(skb = skb_dequeue(&fb_ch->recv_list))) {
		if (!wait_for_completion_timeout(&fb_ch->recv_comp, timeout_jiff)) {
			MTK_ERR(fb_ch->mdev, "Receiving time out, timeout=%d\n", timeout);
			ret = -ETIMEDOUT;
			break;
		}

		ret = atomic_read(&fb_ch->err);
		if (ret) {
			MTK_ERR(fb_ch->mdev, "Failed to receive data, ret=%d\n", ret);
			break;
		}
	}
	*sk_buf = skb;
	return ret;
}

static int mtk_fb_send_cmd(void *ch, enum mtk_fb_cmd cmd, void *param)
{
	struct mtk_fb_flash_param *flash_param = param;
	struct mtk_fb_ch *fb_ch = ch;
	struct sk_buff *skb;
	char *cmd_buf;
	int len;
	int ret;

	skb = __dev_alloc_skb(MTK_FB_REQ_MAX_LEN + 1, GFP_KERNEL);
	if (!skb) {
		MTK_ERR(fb_ch->mdev, "Failed to allocate command buffer\n");
		return -ENOMEM;
	}
	skb_put(skb, MTK_FB_REQ_MAX_LEN + 1);
	cmd_buf = skb->data;
	cmd_buf[MTK_FB_REQ_MAX_LEN] = 0;

	switch (cmd) {
	case FB_CMD_FLASH:
		snprintf(cmd_buf, MTK_FB_REQ_MAX_LEN, "%s%08lx %s", MTK_FB_REQ_FLASH,
			 flash_param->fw_size, flash_param->partition_name);
		break;
	case FB_CMD_COREDUMP:
		strncpy(cmd_buf, MTK_FB_REQ_COREDUMP, MTK_FB_REQ_MAX_LEN);
		break;
	case FB_CMD_REBOOT:
		strncpy(cmd_buf, MTK_FB_REQ_REBOOT, MTK_FB_REQ_MAX_LEN);
		break;
	case FB_CMD_VERSION:
		strncpy(cmd_buf, MTK_FB_REQ_VERSION, MTK_FB_REQ_MAX_LEN);
		break;
	default:
		ret = -EINVAL;
		dev_kfree_skb(skb);
		goto out;
	}

	len = strlen(cmd_buf);
	MTK_INFO(fb_ch->mdev, "Sending command:%s\n", cmd_buf);
	skb_trim(skb, len);
	ret = mtk_fb_ch_send_raw(ch, skb);
out:
	return ret;
}

static int mtk_coredump_pro_pars(char *rsp_param, int param_len)
{
	int ret = MTK_FB_COREDUMP_PROTOCOL_MAX;
	int major_version;
	int sub_version;
	char *dot_pos;

	if (param_len != MTK_FB_COREDUMP_PROTOCOL_LEN)
		goto out;

	dot_pos = strchr(rsp_param, '.');
	if (!dot_pos)
		goto out;

	*dot_pos = '\0';
	if (unlikely(kstrtouint(rsp_param, 10, &major_version)))
		goto out;

	*dot_pos = '.';
	if (unlikely(kstrtouint(rsp_param + 2, 10, &sub_version)))
		goto out;

	if (major_version < MTK_FB_COREDUMP_PROTOCOL_MAX)
		ret = major_version;

out:
	return ret;
}

static enum mtk_fb_cmd_rsp mtk_fb_get_rsp_code(enum mtk_fb_rsp_category category,
					       char *rsp_param, int param_len)
{
	enum mtk_fb_cmd_rsp rsp = FB_CMD_RSP_PROTOCOL_ERR;
	unsigned int err_code;
	unsigned int protocol;

	switch (category) {
	case FB_RSP_CATEGORY_OKAY:
		if (!param_len) {
			rsp = FB_CMD_RSP_SUCCESS;
			protocol = MTK_FB_COREDUMP_PROTOCOL_DEF;
		} else {
			protocol = mtk_coredump_pro_pars(rsp_param, param_len);
			rsp = fb_rsp_coredump_map_tbl[protocol].cmd_rsp;
		}
		break;
	case FB_RSP_CATEGORY_COREDUMP_FAIL:
		if (!param_len)
			rsp = FB_CMD_RSP_COREDUMP_FAIL;
		break;
	case FB_RSP_CATEGORY_COREDUMP_DONE:
		if (!param_len)
			rsp = FB_CMD_RSP_COREDUMP_DONE;
		break;
	case FB_RSP_CATEGORY_FLASH_ERR:
		fallthrough;
	case FB_RSP_CATEGORY_FLASH_STATUS:
		if (unlikely(!rsp_param)) {
			rsp = FB_CMD_RSP_PROTOCOL_ERR;
			break;
		}
		if (unlikely(kstrtouint(rsp_param, 16, &err_code))) {
			rsp = FB_CMD_RSP_PROTOCOL_ERR;
		} else {
			if (unlikely(err_code >= MTK_FB_FLASH_MAX_CODE)) {
				rsp = FB_CMD_RSP_PROTOCOL_ERR;
				break;
			}
			rsp = fb_rsp_flash_map_tbl[err_code].cmd_rsp;
		}
		break;
	default:
		rsp = FB_CMD_RSP_PROTOCOL_ERR;
	}
	return rsp;
}

static enum mtk_fb_cmd_rsp mtk_fb_parse_cmd_rsp(char *rsp_buf, unsigned int rsp_len)
{
	const struct mtk_fb_rsp_category_map *ctg_map;
	enum mtk_fb_cmd_rsp rsp;
	bool found = false;
	int head_len;
	int index;

	for (index = 0; index < ARRAY_SIZE(fb_rsp_ctg_tbl); index++) {
		ctg_map = fb_rsp_ctg_tbl + index;
		head_len = strlen(ctg_map->rsp_head);
		if (head_len > rsp_len)
			continue;
		if (!memcmp(rsp_buf, ctg_map->rsp_head, head_len)) {
			found = true;
			break;
		}
	}
	if (found)
		rsp = mtk_fb_get_rsp_code(ctg_map->category,
					  rsp_buf + head_len, rsp_len - head_len);
	else
		rsp = FB_CMD_RSP_PROTOCOL_ERR;
	return rsp;
}

static int mtk_fb_recv_cmd_rsp(void *ch, enum mtk_fb_cmd_rsp *rsp, unsigned int timeout)
{
	struct mtk_fb_ch *fb_ch = ch;
	struct sk_buff *recv_skb;
	char *cmd_rsp;
	int ret;

	ret = mtk_fb_ch_recv_raw(ch, &recv_skb, timeout);
	if (ret) {
		MTK_ERR(fb_ch->mdev, "Failed to receive response,ret=%d\n", ret);
		return ret;
	}

	cmd_rsp = kzalloc(recv_skb->len + 1, GFP_KERNEL);
	if (!cmd_rsp) {
		dev_kfree_skb(recv_skb);
		return -ENOMEM;
	}

	memcpy(cmd_rsp, recv_skb->data, recv_skb->len);
	MTK_INFO(fb_ch->mdev, "Command response:%s\n", cmd_rsp);
	*rsp = mtk_fb_parse_cmd_rsp(cmd_rsp, recv_skb->len);

	dev_kfree_skb(recv_skb);
	kfree(cmd_rsp);
	return ret;
}

static void mtk_fb_ch_invalid(void *ch, int err)
{
	struct mtk_fb_ch *fb_ch = ch;

	MTK_ERR(fb_ch->mdev, "Setting channel error, err=%d\n", err);
	atomic_set(&fb_ch->err, err);
	complete(&fb_ch->recv_comp);
}

static int mtk_devlink_info_update(struct devlink_info_req *req, char *par_name, char *par_ver)
{
	int ret = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0))
	ret = devlink_info_version_running_put_ext(req, par_name, par_ver,
						   DEVLINK_INFO_VERSION_TYPE_COMPONENT);
#else
	ret = devlink_info_version_running_put(req, par_name, par_ver);
#endif
	return ret;
}

static int mtk_fb_get_version(void *ch, struct devlink_info_req *req, unsigned int timeout)
{
	char par_name[MTK_VERSION_RSP_MAX_LENGTH], par_ver[MTK_VERSION_RSP_MAX_LENGTH];
	char *rsp_buf, *rsp_head, *token, *delim = ",";
	struct mtk_fb_ch *fb_ch = ch;
	int ret, par_num, data_len;
	struct sk_buff *recv_skb;
	struct mtk_md_dev *mdev;
	bool is_par_name = true;

	mdev = fb_ch->mdev;
	ret = mtk_fb_ch_recv_raw(ch, &recv_skb, timeout);
	if (ret) {
		MTK_ERR(mdev, "Failed to receive response,ret=%d\n", ret);
		return ret;
	}

	rsp_buf = kzalloc(recv_skb->len + 1, GFP_KERNEL);
	if (!rsp_buf) {
		dev_kfree_skb(recv_skb);
		return -ENOMEM;
	}

	memcpy(rsp_buf, recv_skb->data, recv_skb->len);
	rsp_head = strsep(&rsp_buf, delim);
	MTK_INFO(mdev, "Devlink version response is %s\n", rsp_head);
	if (strlen(rsp_head) >= MTK_VERSION_RESPONSE_LENGTH) {
		if (unlikely(kstrtoint(rsp_head + MTK_VERSION_RESPONSE_LENGTH, 10, &par_num))) {
			MTK_ERR(mdev, "The response is invalid\n");
			ret = -EINVAL;
			goto out;
		}
	} else {
		MTK_ERR(mdev, "The response is too short\n");
		ret = -EINVAL;
		goto out;
	}

	while ((token = strsep(&rsp_buf, delim)) != NULL) {
		data_len = strlen(token) > MTK_VERSION_RSP_MAX_LENGTH ?
			   MTK_VERSION_RSP_MAX_LENGTH : strlen(token);
		if (is_par_name) {
			memset(par_name, 0, MTK_VERSION_RSP_MAX_LENGTH);
			strncpy(par_name, token, data_len);
			is_par_name = false;
		} else {
			memset(par_ver, 0, MTK_VERSION_RSP_MAX_LENGTH);
			strncpy(par_ver, token, data_len);
			is_par_name = true;
			ret = mtk_devlink_info_update(req, par_name, par_ver);
			if (ret)
				goto out;
			}
	}

out:
	dev_kfree_skb(recv_skb);
	kfree(rsp_buf);
	return ret;
}

static void mtk_fb_ch_close(void *ch, enum mtk_fb_ch_t ch_type)
{
	struct mtk_fb_ch *fb_ch = ch;
	struct mtk_devlink *devlink;

	devlink = fb_ch->mdev->devlink;
	mtk_port_internal_close(fb_ch->port);
	skb_queue_purge(&fb_ch->recv_list);
	atomic_set(&devlink->ch_state[ch_type].opened, 0);
	kfree(fb_ch);
}

#define MTK_DEVLINK_FMSG_MAX_SIZE		(GENLMSG_DEFAULT_SIZE - GENL_HDRLEN - NLA_HDRLEN)
#define MTK_DEVLINK_COREDUMP_RECV_TIMEOUT_MS	10000
#define MTK_DEVLINK_COREDUMP_MAX_PKT_SIZE	(7 * 1024)
#define MTK_DEVLINK_FLASH_MAX_PKT_SIZE		(7 * 1024)
#define MTK_DEVLINK_WAIT_DL_MODE_TIMEOUT_MS	45000
#define MTK_DEVLINK_REBOOT_TIMEOUT_MS		8000
#define MTK_DEVLINK_FLASH_CMD_TIMEOUT_MS	10000
#define MTK_DEVLINK_FINAL_UPDATE_TIMEOUT_MS	30000
#define MTK_DEVLINK_PROGRESS_IND_MAX_COUNT	3

#define MTK_DEVLINK_FLAG_ENTER_DOWNLOAD		0
#define MTK_DEVLINK_FLAG_RELOAD			1

enum mtk_devlink_dev_mode {
	DEVLINK_DEV_MODE_OFF		= 0,
	DEVLINK_DEV_MODE_NORMAL		= 1,
	DEVLINK_DEV_MODE_COREDUMP	= 2,
	DEVLINK_DEV_MODE_DOWNLOAD	= 3,
	DEVLINK_DEV_MODE_LINKDOWN	= 4,
	DEVLINK_DEV_MODE_MISC		= 5,
};

struct mtk_fb_rsp_to_err_map {
	enum mtk_fb_cmd_rsp rsp;
	int err;
};

static struct mtk_fb_rsp_to_err_map fb_rsp_to_err_map_tbl[] = {
	{FB_CMD_RSP_SUCCESS,		0},
	{FB_CMD_RSP_TIMEOUT,		-ETIMEDOUT},
	{FB_CMD_RSP_IMG_LARGE,		-EFBIG},
	{FB_CMD_RSP_WRONG_PARTITION,	-EINVAL},
	{FB_CMD_RSP_PARTITION_FULL,	-ENOSPC},
	{FB_CMD_RSP_WRITE_FORBID,	-EIO},
	{FB_CMD_RSP_ERASE_ERR,		-EIO},
	{FB_CMD_RSP_WRITE_ERR,		-EIO},
	{FB_CMD_RSP_READ_ERR,		-EIO},
	{FB_CMD_RSP_COREDUMP_DONE,	0},
	{FB_CMD_RSP_COREDUMP_FAIL,	-ENODATA},
	{FB_CMD_RSP_PROTOCOL_ERR,	-EPROTO},
	{FB_CMD_RSP_COREDUMP_V1,	0},
};

static ssize_t fb_dl_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count);
static ssize_t reload_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count);

static DEVICE_ATTR_WO(fb_dl);
static DEVICE_ATTR_WO(reload);

static __maybe_unused int mtk_devlink_info_get(struct devlink *devlink,
					       struct devlink_info_req *req,
					       struct netlink_ext_ack *extack)
{
	struct mtk_devlink *mtk_dl = devlink_priv(devlink);
	struct mtk_md_dev *mdev = mtk_dl->mdev;
	enum mtk_fb_cmd_rsp cmd_rsp;
	int dev_mode;
	void *fb_ch;
	int ret = 0;

	dev_mode = atomic_read(&mtk_dl->dev_mode);
	if (dev_mode != DEVLINK_DEV_MODE_DOWNLOAD) {
		MTK_ERR(mdev, "Device mode=%d, devlink version is unsupported.\n", dev_mode);
		ret = -EIO;
		goto out;
	}

	fb_ch = mtk_fb_ch_open(mdev, FB_CH_DOWNLOAD, O_NONBLOCK);
	if (unlikely(!fb_ch)) {
		MTK_ERR(mdev, "Failed to open download channel\n");
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&mtk_dl->fb_mutex);
	mtk_dl->fb_ch = fb_ch;
	mutex_unlock(&mtk_dl->fb_mutex);

	ret = mtk_fb_send_cmd(mtk_dl->fb_ch, FB_CMD_VERSION, NULL);
	if (unlikely(ret)) {
		MTK_ERR(mdev, "Failed to send version command, ret=%d\n", ret);
		goto out;
	}

	ret = mtk_fb_get_version(mtk_dl->fb_ch, req, MTK_DEVLINK_FLASH_CMD_TIMEOUT_MS);
	if (ret)
		goto out;

	ret = mtk_fb_recv_cmd_rsp(mtk_dl->fb_ch, &cmd_rsp, MTK_DEVLINK_FLASH_CMD_TIMEOUT_MS);
	if (ret)
		goto out;
	if (cmd_rsp != FB_CMD_RSP_SUCCESS) {
		MTK_ERR(mdev, "Get version error:%d\n", cmd_rsp);
		ret = fb_rsp_to_err_map_tbl[cmd_rsp].err;
		goto out;
	}

out:
	mutex_lock(&mtk_dl->fb_mutex);
	if (mtk_dl->fb_ch) {
		mtk_fb_ch_close(mtk_dl->fb_ch, FB_CH_DOWNLOAD);
		mtk_dl->fb_ch = NULL;
	}
	mutex_unlock(&mtk_dl->fb_mutex);
	return ret;
}

static int mtk_devlink_flash_update(struct devlink *dl,
				    struct devlink_flash_update_params *params,
				    struct netlink_ext_ack *extack)
{
	struct mtk_fb_flash_param flash_param;
	enum mtk_fb_cmd_rsp cmd_rsp;
	struct mtk_devlink *mtk_dl;
	struct sk_buff *send_skb;
	struct mtk_md_dev *mdev;
	size_t sent_len;
	size_t pkt_len;
	size_t ind_pos;
	int dev_mode;
	size_t left;
	void *fb_ch;
	int ret;

	mtk_dl = devlink_priv(dl);
	mdev = mtk_dl->mdev;

	if (!params->component) {
		MTK_ERR(mdev, "Invalid component parameter\n");
		return -EINVAL;
	}
	MTK_INFO(mdev, "Trying to update %s\n", params->component);

	ret = mutex_trylock(&mtk_dl->dl_mutex);
	if (!ret) {
		MTK_ERR(mdev, "Flash operation is not allowed\n");
		return -EBUSY;
	}
	dev_mode = atomic_read(&mtk_dl->dev_mode);
	if (dev_mode != DEVLINK_DEV_MODE_DOWNLOAD) {
		MTK_ERR(mdev, "Invalid device mode, dev_mode=%d\n", dev_mode);
		goto out;
	}

	fb_ch = mtk_fb_ch_open(mdev, FB_CH_DOWNLOAD, O_NONBLOCK);
	if (unlikely(!fb_ch)) {
		MTK_ERR(mdev, "Failed to open download channel\n");
		ret = -ENODEV;
		goto err_flash_fail;
	}
	mutex_lock(&mtk_dl->fb_mutex);
	mtk_dl->fb_ch = fb_ch;
	mutex_unlock(&mtk_dl->fb_mutex);

	flash_param.fw_size = params->fw->size;
	flash_param.partition_name = params->component;
	ret = mtk_fb_send_cmd(mtk_dl->fb_ch, FB_CMD_FLASH, &flash_param);
	if (unlikely(ret)) {
		MTK_ERR(mdev, "Failed to send flash command, ret=%d\n", ret);
		goto err_flash_fail;
	}

	ret = mtk_fb_recv_cmd_rsp(mtk_dl->fb_ch, &cmd_rsp, MTK_DEVLINK_FLASH_CMD_TIMEOUT_MS);
	if (ret)
		goto err_flash_fail;
	if (cmd_rsp != FB_CMD_RSP_SUCCESS) {
		MTK_ERR(mdev, "Preparing update error:%d\n", cmd_rsp);
		ret = fb_rsp_to_err_map_tbl[cmd_rsp].err;
		goto err_flash_fail;
	}

	devlink_flash_update_status_notify(dl, "Begin to transmit image",
					   params->component, 0, params->fw->size);
	sent_len = 0;
	left = params->fw->size;
	ind_pos = params->fw->size / MTK_DEVLINK_PROGRESS_IND_MAX_COUNT;
	do {
		if (left > MTK_DEVLINK_FLASH_MAX_PKT_SIZE)
			pkt_len = MTK_DEVLINK_FLASH_MAX_PKT_SIZE;
		else
			pkt_len = left;
		send_skb = __dev_alloc_skb(pkt_len, GFP_KERNEL);
		if (unlikely(!send_skb)) {
			MTK_ERR(mdev, "Failed to allocate TX skb buffer\n");
			ret = -ENOMEM;
			goto err_flash_fail;
		}
		skb_put_data(send_skb, params->fw->data + sent_len, pkt_len);
		ret = mtk_fb_ch_send_raw(mtk_dl->fb_ch, send_skb);
		if (ret == -EAGAIN) {
			MTK_WARN(mdev, "Buffer full and retry\n");
			msleep(20);/* Waiting for 20 ms to retry it. */
			continue;
		}
		if (ret) {
			MTK_ERR(mdev, "Failed to send skb, ret=%d\n", ret);
			goto err_flash_fail;
		}
		sent_len += pkt_len;
		left = params->fw->size - sent_len;
		if (sent_len >= ind_pos) {
			devlink_flash_update_status_notify(dl, "Transmitting image",
							   params->component, sent_len,
							   params->fw->size);
			ind_pos += (params->fw->size / MTK_DEVLINK_PROGRESS_IND_MAX_COUNT);
		}
	} while (left);

	devlink_flash_update_status_notify(dl, "Begin to flash the partition",
					   params->component, 0, params->fw->size);

	/* Waiting for the firmware final upgrade result. */
	ret = mtk_fb_recv_cmd_rsp(mtk_dl->fb_ch, &cmd_rsp, MTK_DEVLINK_FINAL_UPDATE_TIMEOUT_MS);

	if (ret)
		goto err_flash_fail;

	if (cmd_rsp != FB_CMD_RSP_SUCCESS) {
		MTK_ERR(mdev, "Flash update err: %d\n", cmd_rsp);
		ret = fb_rsp_to_err_map_tbl[cmd_rsp].err;
		goto err_flash_fail;
	}

	devlink_flash_update_status_notify(dl, "Flash done", params->component,
					   params->fw->size, params->fw->size);

err_flash_fail:
	if (ret)
		devlink_flash_update_status_notify(dl, "Flash failed", params->component, 0, 0);
	mutex_lock(&mtk_dl->fb_mutex);
	if (mtk_dl->fb_ch) {
		mtk_fb_ch_close(mtk_dl->fb_ch, FB_CH_DOWNLOAD);
		mtk_dl->fb_ch = NULL;
	}
	mutex_unlock(&mtk_dl->fb_mutex);
out:
	MTK_INFO(mdev, "Devlink flash result, ret=%d\n", ret);
	mutex_unlock(&mtk_dl->dl_mutex);
	return ret;
}

static const struct devlink_ops mtk_devlink_ops = {
	.flash_update = mtk_devlink_flash_update,
#ifdef CONFIG_MTK_DEVLINK_FLASH_SUPPORT
	.info_get = mtk_devlink_info_get,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	.supported_flash_update_params = DEVLINK_SUPPORT_FLASH_UPDATE_OVERWRITE_MASK
#else
	.supported_flash_update_params = DEVLINK_SUPPORT_FLASH_UPDATE_COMPONENT
#endif
};

static int mtk_dl_fmsg_binary_pair_put(struct devlink_fmsg *fmsg, const void *value, u32 value_len)
{
	int ret = 0;
	u32 offset;
	u32 len;

	for (offset = 0; offset < value_len; offset += len) {
		len = value_len - offset;
		if (len > MTK_DEVLINK_FMSG_MAX_SIZE)
			len = MTK_DEVLINK_FMSG_MAX_SIZE;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0))
		devlink_fmsg_binary_put(fmsg, value + offset, len);
#else
		ret = devlink_fmsg_binary_put(fmsg, value + offset, len);
		if (ret)
			break;
#endif
	}
	return ret;
}

static int mtk_devlink_coredump_handle(struct mtk_md_dev *mdev, struct devlink_fmsg *fmsg)
{
	char cd_buf[MTK_FB_RSP_COREDUMP_DONE_LEN + 1] = {0};
	struct mtk_devlink *devlink = mdev->devlink;
	enum mtk_fb_cmd_rsp cmd_rsp;
	struct sk_buff *recv_skb;
	bool complete = false;
	size_t cd_len = 0;
	size_t pkt_len;
	int ret;

	do {
		ret = mtk_fb_ch_recv_raw(devlink->fb_ch, &recv_skb,
					 MTK_DEVLINK_FLASH_MAX_PKT_SIZE);
		if (unlikely(ret)) {
			MTK_ERR(mdev, "Core dump receive error, ret=%d\n", ret);
			goto out;
		}

		pkt_len = recv_skb->len;
		if (pkt_len < MTK_DEVLINK_COREDUMP_MAX_PKT_SIZE) {
			MTK_INFO(mdev, "Core dump packet length: %d\n", pkt_len);
			if (pkt_len >= MTK_FB_RSP_COREDUMP_DONE_LEN) {
				memcpy(cd_buf,
				       recv_skb->data + pkt_len - MTK_FB_RSP_COREDUMP_DONE_LEN,
				       MTK_FB_RSP_COREDUMP_DONE_LEN);
				MTK_INFO(mdev, "Core dump data: %s\n", cd_buf);
				cmd_rsp = mtk_fb_parse_cmd_rsp(cd_buf,
							       MTK_FB_RSP_COREDUMP_DONE_LEN);
				if (cmd_rsp == FB_CMD_RSP_COREDUMP_DONE) {
					MTK_INFO(mdev, "Core dump finishes\n");
					complete = true;
					pkt_len -= MTK_FB_RSP_COREDUMP_DONE_LEN;
				}
			}
		}
		cd_len += pkt_len;
		if (pkt_len)
			mtk_dl_fmsg_binary_pair_put(fmsg, recv_skb->data, pkt_len);
		dev_kfree_skb(recv_skb);
	} while (!ret && !complete);

	MTK_INFO(mdev, "coredump result: %d,%d,%d\n", ret, cmd_rsp, complete);
	if (complete)
		MTK_INFO(mdev, "Core dump len:%lld\n", cd_len);
out:
	return ret;
}

static int mtk_devlink_coredump_handle_v01(struct mtk_md_dev *mdev, struct devlink_fmsg *fmsg)
{
	struct mtk_devlink *devlink;
	struct sk_buff *recv_skb;
	unsigned int len;
	size_t pkt_len;
	size_t cd_len;
	bool complete;
	int ret;

	devlink = mdev->devlink;
	cd_len = 0;
	complete = false;

	do {
		ret = mtk_fb_ch_recv_raw(devlink->fb_ch, &recv_skb,
					 MTK_DEVLINK_FLASH_MAX_PKT_SIZE);
		if (unlikely(ret)) {
			MTK_ERR(mdev, "Core dump receive error, ret=%d\n", ret);
			goto out;
		}

		pkt_len = recv_skb->len;
		ret = skb_copy_bits(recv_skb, MTK_COREDUMP_V01_DATA_HEAD, &len,
				    MTK_COREDUMP_V01_DATA_LEN);
		if (unlikely(ret)) {
			MTK_ERR(mdev, "Invalid core dump data, ret=%d\n", ret);
			goto out;
		}
		MTK_INFO(mdev, "Rcv %d len data, raw data len is: %d\n", pkt_len, len);
		if (len) {
			cd_len += len;
			mtk_dl_fmsg_binary_pair_put(fmsg,
						    recv_skb->data + MTK_COREDUMP_V01_DATA_LEN,
						    len);
		} else {
			MTK_INFO(mdev, "Core dump finishes\n");
			complete = true;
		}
		dev_kfree_skb(recv_skb);
	} while (!ret && !complete);

	MTK_INFO(mdev, "coredump result: %d,%d\n", ret, complete);
	if (complete)
		MTK_INFO(mdev, "Core dump len:%lld\n", cd_len);
out:
	return ret;
}

static const struct mtk_devlink_coredump_ops dl_coredump_tbl[] = {
	{FB_CMD_RSP_SUCCESS,		mtk_devlink_coredump_handle},
	{FB_CMD_RSP_COREDUMP_V1,	mtk_devlink_coredump_handle_v01},
};

static int mtk_devlink_health_dump(struct devlink_health_reporter *reporter,
				   struct devlink_fmsg *fmsg, void *priv_ctx,
				   struct netlink_ext_ack *extack)
{
	enum mtk_fb_cmd_rsp cmd_rsp;
	struct mtk_devlink *devlink;
	struct mtk_md_dev *mdev;
	int dev_mode;
	void *fb_ch;
	int index;
	int ret;

	mdev = devlink_health_reporter_priv(reporter);
	devlink = mdev->devlink;
	MTK_ERR(mdev, "Devlink core dump begins\n");

	ret = mutex_trylock(&devlink->dl_mutex);
	if (!ret) {
		MTK_ERR(mdev, "Core dump operation is not allowed\n");
		return -EPERM;
	}

	dev_mode = atomic_read(&devlink->dev_mode);
	if (unlikely(dev_mode != DEVLINK_DEV_MODE_COREDUMP)) {
		MTK_ERR(mdev, "Device is not in postdump mode, dev_mode=%d\n", dev_mode);
		ret = -EPERM;
		goto out;
	}

	fb_ch = mtk_fb_ch_open(mdev, FB_CH_COREDUMP, 0);
	if (!fb_ch) {
		MTK_ERR(mdev, "Failed to open core dump channel\n");
		ret = -ENODEV;
		goto out;
	}
	mutex_lock(&devlink->fb_mutex);
	devlink->fb_ch = fb_ch;
	mutex_unlock(&devlink->fb_mutex);

	ret = mtk_fb_send_cmd(devlink->fb_ch, FB_CMD_COREDUMP, NULL);
	if (ret) {
		MTK_ERR(mdev, "Failed to request core dump data, ret=%d\n", ret);
		goto error_close_ch;
	}

	ret = mtk_fb_recv_cmd_rsp(devlink->fb_ch, &cmd_rsp, MTK_DEVLINK_FLASH_MAX_PKT_SIZE);
	if (ret)
		goto error_close_ch;

	if (fb_rsp_to_err_map_tbl[cmd_rsp].err) {
		MTK_ERR(mdev, "Failed to request core dump data, cmd_rsp=%d\n", cmd_rsp);
		ret = fb_rsp_to_err_map_tbl[cmd_rsp].err;
		goto error_close_ch;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0))
	devlink_fmsg_binary_pair_nest_start(fmsg, "coredump");
#else
	ret = devlink_fmsg_binary_pair_nest_start(fmsg, "coredump");
	if (ret)
		goto error_close_ch;
#endif

	for (index = 0; index < ARRAY_SIZE(dl_coredump_tbl); index++) {
		if (cmd_rsp == dl_coredump_tbl[index].cmd_rsp) {
			__pm_stay_awake(devlink->dl_ws);
			ret = dl_coredump_tbl[index].handler(mdev, fmsg);
			__pm_relax(devlink->dl_ws);
			break;
		}
	}
	if (unlikely(ret)) {
		MTK_ERR(mdev, "Core dump error, ret=%d\n", ret);
		goto error_close_ch;
	}
	devlink_fmsg_binary_pair_nest_end(fmsg);
error_close_ch:
	mutex_lock(&devlink->fb_mutex);
	if (devlink->fb_ch) {
		mtk_fb_ch_close(devlink->fb_ch, FB_CH_COREDUMP);
		devlink->fb_ch = NULL;
	}
	mutex_unlock(&devlink->fb_mutex);
out:
	mutex_unlock(&devlink->dl_mutex);
	return ret;
}

static const struct devlink_health_reporter_ops mtk_devlink_fw_fatal_reporter_ops = {
	.name = "fw_fatal",
	.dump = mtk_devlink_health_dump,
};

static int mtk_devlink_region_get_log(struct mtk_md_dev *mdev, u8 **buffer,
				      enum mtk_devlink_region_log_type region_type)
{
	struct mtk_devlink *devlink = mdev->devlink;
	int device_log_type;
	char *log_buffer;
	int buffer_size;
	int ret = -EIO;
	int dev_state;
	int dev_mode;
	u32 dev_reg;

	mutex_lock(&devlink->get_log_mutex);
	dev_reg = mtk_dev_get_dev_state(mdev);
	dev_state = MTK_DEV_STATE_UNKNOWN;
	switch (region_type) {
	case DEVLINK_REGION_BROM_SRAM_LOG:
		device_log_type = MTK_DEV_LOG_BROM_SRAM;
		break;
	case DEVLINK_REGION_BROM_SRAM_ORIGINAL_LOG:
		dev_state = dev_reg & MTK_DEVLINK_REGION_ORIGINAL_BITMASK;
		if (dev_state == MTK_DEV_STAGE_BROM1 || dev_state == MTK_DEV_STAGE_BROM2) {
			device_log_type = MTK_DEV_LOG_BROM_SRAM_ORIGIN;
		} else {
			MTK_ERR(mdev, "Error! device is in invalid state\n");
			goto out;
		}
		break;
	case DEVLINK_REGION_PL_SRAM_LOG:
		device_log_type = MTK_DEV_LOG_PL_SRAM;
		break;
	case DEVLINK_REGION_PL_DRAM_LOG:
		dev_reg >>= MTK_DEVLINK_REGION_DEXTROPOSITION;
		dev_state = dev_reg & MTK_DEVLINK_REGION_BITMASK;
		if (dev_state == MTK_DEVLINK_DEV_STAGE_NORMAL) {
			device_log_type = MTK_DEV_LOG_PL_DRAM;
		} else {
			MTK_ERR(mdev, "Error! device is in invalid state\n");
			goto out;
		}
		break;
	case DEVLINK_REGION_ATF_DRAM_LOG:
		dev_reg >>= MTK_DEVLINK_REGION_DEXTROPOSITION;
		dev_state = dev_reg & MTK_DEVLINK_REGION_BITMASK;
		if (dev_state == MTK_DEVLINK_DEV_STAGE_NORMAL) {
			device_log_type = MTK_DEV_LOG_ATF_DRAM;
		} else {
			MTK_ERR(mdev, "Error! device is in invalid state\n");
			goto out;
		}
		break;
	case DEVLINK_REGION_HOST_RECVED_LOG:
		device_log_type = MTK_DEV_LOG_HOST_RECVED;
		break;
	default:
		MTK_ERR(mdev, "Invalid region type\n");
		goto out;
	}
	MTK_INFO(mdev, "device_log_type = %d, dev_state = %d\n", device_log_type, dev_state);

	buffer_size = mtk_dev_get_log_region_size(mdev, device_log_type);
	if (buffer_size > 0) {
		log_buffer = vzalloc(buffer_size);
		if (log_buffer) {
			pm_runtime_get(mdev->dev);
			__pm_stay_awake(devlink->dl_ws);
			dev_mode = atomic_read(&devlink->dev_mode);
			if (dev_mode == DEVLINK_DEV_MODE_LINKDOWN ||
			    dev_mode == DEVLINK_DEV_MODE_OFF)
				MTK_ERR(mdev, "Invalid state, get log is forbidden\n");
			else
				ret = mtk_dev_get_dev_log(mdev, log_buffer, buffer_size,
							  device_log_type);
			__pm_relax(devlink->dl_ws);
			pm_runtime_put(mdev->dev);
			if (ret == buffer_size) {
				MTK_INFO(mdev, "read %d length data\n", ret);
				*buffer = log_buffer;
				ret = 0;
			} else {
				MTK_ERR(mdev, "Error! read %d length data\n", ret);
				vfree(log_buffer);
				ret = -EIO;
			}
		} else {
			MTK_ERR(mdev, "buffer allocate failed\n");
			ret = -ENOMEM;
		}
	}

out:
	mutex_unlock(&devlink->get_log_mutex);
	return ret;
}

static int mtk_devlink_brom_sram_snapshot(struct devlink *devlink,
					  const struct devlink_region_ops *ops,
					  struct netlink_ext_ack *extack, u8 **data)
{
	struct mtk_devlink *mdevlink = devlink_priv(devlink);
	struct mtk_md_dev *mdev = mdevlink->mdev;

	return mtk_devlink_region_get_log(mdev, data, DEVLINK_REGION_BROM_SRAM_LOG);
}

static const struct devlink_region_ops mtk_brom_sram_region_ops = {
	.name = "BROM_SRAM_LOG",
	.destructor = vfree,
	.snapshot = mtk_devlink_brom_sram_snapshot,
};

static int mtk_devlink_brom_sram_original_snapshot(struct devlink *devlink,
						   const struct devlink_region_ops *ops,
						   struct netlink_ext_ack *extack, u8 **data)
{
	struct mtk_devlink *mdevlink = devlink_priv(devlink);
	struct mtk_md_dev *mdev = mdevlink->mdev;

	return mtk_devlink_region_get_log(mdev, data, DEVLINK_REGION_BROM_SRAM_ORIGINAL_LOG);
}

static const struct devlink_region_ops mtk_brom_sram_original_region_ops = {
	.name = "BROM_SRAM_ORIGINAL_LOG",
	.destructor = vfree,
	.snapshot = mtk_devlink_brom_sram_original_snapshot,
};

static int mtk_devlink_pl_sram_snapshot(struct devlink *devlink,
					const struct devlink_region_ops *ops,
					struct netlink_ext_ack *extack, u8 **data)
{
	struct mtk_devlink *mdevlink = devlink_priv(devlink);
	struct mtk_md_dev *mdev = mdevlink->mdev;

	return mtk_devlink_region_get_log(mdev, data, DEVLINK_REGION_PL_SRAM_LOG);
}

static const struct devlink_region_ops mtk_pl_sram_region_ops = {
	.name = "PL_SRAM_LOG",
	.destructor = vfree,
	.snapshot = mtk_devlink_pl_sram_snapshot,
};

static int mtk_devlink_pl_dram_snapshot(struct devlink *devlink,
					const struct devlink_region_ops *ops,
					struct netlink_ext_ack *extack, u8 **data)
{
	struct mtk_devlink *mdevlink = devlink_priv(devlink);
	struct mtk_md_dev *mdev = mdevlink->mdev;

	return mtk_devlink_region_get_log(mdev, data, DEVLINK_REGION_PL_DRAM_LOG);
}

static const struct devlink_region_ops mtk_pl_dram_region_ops = {
	.name = "PLLK_DRAM_LOG",
	.destructor = vfree,
	.snapshot = mtk_devlink_pl_dram_snapshot,
};

static int mtk_devlink_host_recved_snapshot(struct devlink *devlink,
					    const struct devlink_region_ops *ops,
					    struct netlink_ext_ack *extack, u8 **data)
{
	struct mtk_devlink *mdevlink = devlink_priv(devlink);
	struct mtk_md_dev *mdev = mdevlink->mdev;

	return mtk_devlink_region_get_log(mdev, data, DEVLINK_REGION_HOST_RECVED_LOG);
}

static const struct devlink_region_ops mtk_host_recved_region_ops = {
	.name = "HOST_RECVED_LOG",
	.destructor = vfree,
	.snapshot = mtk_devlink_host_recved_snapshot,
};

static int mtk_devlink_atf_dram_snapshot(struct devlink *devlink,
					 const struct devlink_region_ops *ops,
					 struct netlink_ext_ack *extack, u8 **data)
{
	struct mtk_devlink *mdevlink = devlink_priv(devlink);
	struct mtk_md_dev *mdev = mdevlink->mdev;

	return mtk_devlink_region_get_log(mdev, data, DEVLINK_REGION_ATF_DRAM_LOG);
}

static const struct devlink_region_ops mtk_atf_dram_region_ops = {
	.name = "TFA_DRAM_LOG",
	.destructor = vfree,
	.snapshot = mtk_devlink_atf_dram_snapshot,
};

static const struct mtk_devlink_region_map dl_region_tbl[] = {
	{&mtk_brom_sram_region_ops,		MTK_DEV_LOG_BROM_SRAM},
	{&mtk_brom_sram_original_region_ops,	MTK_DEV_LOG_BROM_SRAM_ORIGIN},
	{&mtk_pl_sram_region_ops,		MTK_DEV_LOG_PL_SRAM},
	{&mtk_pl_dram_region_ops,		MTK_DEV_LOG_PL_DRAM},
	{&mtk_host_recved_region_ops,		MTK_DEV_LOG_HOST_RECVED},
	{&mtk_atf_dram_region_ops,		MTK_DEV_LOG_ATF_DRAM},
};

static ssize_t fb_dl_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct mtk_devlink *devlink;
	unsigned long timeout_jiff;
	struct pci_dev *pdev;
	int dev_mode;
	int input;
	int ret;

	if (unlikely(!buf || !count))
		return -EINVAL;

	pdev = container_of(dev, struct pci_dev, dev);
	devlink = ((struct mtk_md_dev *)pci_get_drvdata(pdev))->devlink;

	if (kstrtoint(buf, 0, &input))
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	dev_mode = atomic_read(&devlink->dev_mode);
	if (dev_mode == DEVLINK_DEV_MODE_DOWNLOAD) {
		ret = count;
		goto out;
	} else if (dev_mode != DEVLINK_DEV_MODE_NORMAL) {
		ret = -EPERM;
		goto out;
	}

	set_bit(MTK_DEVLINK_FLAG_ENTER_DOWNLOAD, &devlink->flags);
	ret = mutex_trylock(&devlink->dl_mutex);
	if (!ret) {
		MTK_WARN(devlink->mdev, "Entering download mode is not allowed\n");
		return -EPERM;
	}

	reinit_completion(&devlink->download_comp);
	pm_runtime_get(dev);
	ret = mtk_fsm_evt_submit(devlink->mdev, FSM_EVT_FB_RESET, FSM_F_DL_FB,
				 NULL, 0, EVT_MODE_BLOCKING);
	pm_runtime_put(dev);
	if (ret == FSM_EVT_RET_FAIL) {
		ret = -EPERM;
		goto out;
	}
	timeout_jiff = msecs_to_jiffies(MTK_DEVLINK_WAIT_DL_MODE_TIMEOUT_MS);
	ret = wait_for_completion_timeout(&devlink->download_comp, timeout_jiff);
	if (ret == 0) {
		ret = -ETIMEDOUT;
	} else {
		dev_mode = atomic_read(&devlink->dev_mode);
		if (dev_mode != DEVLINK_DEV_MODE_DOWNLOAD)
			ret = -ENODEV;
		else
			ret = count;
	}
	if (ret != count)
		device_remove_file_self(dev, &dev_attr_fb_dl);
out:
	mutex_unlock(&devlink->dl_mutex);
	clear_bit(MTK_DEVLINK_FLAG_ENTER_DOWNLOAD, &devlink->flags);
	return ret;
}

static ssize_t reload_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct mtk_devlink *devlink;
	enum mtk_fb_cmd_rsp cmd_rsp;
	enum mtk_fb_ch_t ch_type;
	struct pci_dev *pdev;
	int dev_mode;
	void *fb_ch;
	int input;
	int ret;

	if (unlikely(!buf || !count))
		return -EINVAL;

	pdev = container_of(dev, struct pci_dev, dev);
	devlink = ((struct mtk_md_dev *)pci_get_drvdata(pdev))->devlink;

	if (kstrtoint(buf, 0, &input))
		return -EINVAL;

	if (input != 1)
		return -EINVAL;

	if (test_bit(MTK_DEVLINK_FLAG_RELOAD, &devlink->flags))
		return -EPERM;

	dev_mode = atomic_read(&devlink->dev_mode);
	if (dev_mode == DEVLINK_DEV_MODE_COREDUMP) {
		ch_type = FB_CH_COREDUMP;
	} else if (dev_mode == DEVLINK_DEV_MODE_DOWNLOAD) {
		ch_type = FB_CH_DOWNLOAD;
	} else {
		ret = -EPERM;
		goto out_unlock_dl_mutex;
	}

	ret = mutex_trylock(&devlink->dl_mutex);
	if (!ret) {
		MTK_WARN(devlink->mdev, "Device reload is not allowed\n");
		return -EPERM;
	}

	fb_ch = mtk_fb_ch_open(devlink->mdev, ch_type, 0);
	if (unlikely(!fb_ch)) {
		MTK_WARN(devlink->mdev, "Failed to open channel, ch_type=%d\n", ch_type);
		ret = -ENODEV;
		goto out_unlock_dl_mutex;
	}

	mutex_lock(&devlink->fb_mutex);
	devlink->fb_ch = fb_ch;
	mutex_unlock(&devlink->fb_mutex);

	ret = mtk_fb_send_cmd(devlink->fb_ch, FB_CMD_REBOOT, NULL);
	if (!ret)
		set_bit(MTK_DEVLINK_FLAG_RELOAD, &devlink->flags);

	ret = mtk_fb_recv_cmd_rsp(devlink->fb_ch, &cmd_rsp, MTK_DEVLINK_REBOOT_TIMEOUT_MS);
	if (fb_rsp_to_err_map_tbl[cmd_rsp].err) {
		MTK_ERR(devlink->mdev, "Failed to request reboot, cmd_rsp=%d\n", cmd_rsp);
		ret = fb_rsp_to_err_map_tbl[cmd_rsp].err;
	} else {
		MTK_INFO(devlink->mdev, "Reload success\n");
		ret = count;
	}

	mutex_lock(&devlink->fb_mutex);
	mtk_fb_ch_close(devlink->fb_ch, ch_type);
	devlink->fb_ch = NULL;
	mutex_unlock(&devlink->fb_mutex);
out_unlock_dl_mutex:
	mutex_unlock(&devlink->dl_mutex);
	return ret;
}

static void mtk_devlink_fsm_callback(struct mtk_fsm_param *fsm_param, void *data)
{
	struct mtk_md_dev *mdev = data;
	struct mtk_devlink *devlink;
	int old_mode;
	int ret;

	devlink = mdev->devlink;
	switch (fsm_param->to) {
	case FSM_STATE_OFF:
		old_mode = atomic_read(&devlink->dev_mode);
		if (devlink->fw_reporter && (old_mode == DEVLINK_DEV_MODE_COREDUMP ||
					     old_mode == DEVLINK_DEV_MODE_DOWNLOAD)) {
			device_remove_file(mdev->dev, &dev_attr_reload);
			clear_bit(MTK_DEVLINK_FLAG_RELOAD, &devlink->flags);
		}
		mutex_lock(&devlink->get_log_mutex);
		atomic_set(&devlink->dev_mode, DEVLINK_DEV_MODE_OFF);
		mutex_unlock(&devlink->get_log_mutex);
		if (!test_bit(MTK_DEVLINK_FLAG_ENTER_DOWNLOAD, &devlink->flags) &&
		    devlink->fw_reporter) {
			MTK_INFO(devlink->mdev, "FSM switch to off, device remove fb_dl file\n");
			device_remove_file(mdev->dev, &dev_attr_fb_dl);
		}
		if (fsm_param->evt_id == FSM_EVT_DEV_RM)
			complete(&devlink->download_comp);

		mutex_lock(&devlink->fb_mutex);
		if (devlink->fb_ch) {
			ret = fsm_param->evt_id == FSM_EVT_DEV_RM ? -ENODEV : -EIO;
			mtk_fb_ch_invalid(devlink->fb_ch, ret);
		}
		mutex_unlock(&devlink->fb_mutex);
		if (devlink->fw_reporter)
			flush_work(&devlink->dl_worker);
		break;
	case FSM_STATE_DOWNLOAD:
		if (fsm_param->fsm_flag & FSM_F_DL_FB) {
			MTK_INFO(mdev, "Enter Fb_download mode.\n");
			if (!test_bit(MTK_DEVLINK_FLAG_ENTER_DOWNLOAD, &devlink->flags)) {
				ret = device_create_file(mdev->dev, &dev_attr_fb_dl);
				if (ret)
					MTK_ERR(mdev, "Failed to create fb_dl sysfs entry\n");
			}
			ret = device_create_file(mdev->dev, &dev_attr_reload);
			if (ret)
				MTK_ERR(mdev, "Failed to create reload sysfs entry\n");
			atomic_set(&devlink->dev_mode, DEVLINK_DEV_MODE_DOWNLOAD);
			complete(&devlink->download_comp);
		}
		break;
	case FSM_STATE_POSTDUMP:
		if (devlink->fw_reporter) {
			atomic_set(&devlink->dev_mode, DEVLINK_DEV_MODE_COREDUMP);
			queue_work(system_long_wq, &devlink->dl_worker);
			ret = device_create_file(mdev->dev, &dev_attr_reload);
			if (ret)
				MTK_ERR(mdev, "Failed to create reload sysfs entry\n");
		}
		break;
	case FSM_STATE_READY:
		atomic_set(&devlink->dev_mode, DEVLINK_DEV_MODE_NORMAL);
		if (devlink->fw_reporter) {
			devlink_health_reporter_state_update(devlink->fw_reporter,
							     DEVLINK_HEALTH_REPORTER_STATE_HEALTHY);
			ret = device_create_file(mdev->dev, &dev_attr_fb_dl);
			if (ret)
				MTK_ERR(mdev, "Failed to create fb_dl sysfs entry\n");
		}
		break;
	case FSM_STATE_EXCEPTION:
		if (fsm_param->fsm_flag & FSM_F_LINK_EXCEPTION) {
			MTK_INFO(devlink->mdev, "Link down, get log is forbidden\n");
			atomic_set(&devlink->dev_mode, DEVLINK_DEV_MODE_LINKDOWN);
		}
		break;
	case FSM_STATE_ON:
		fallthrough;
	case FSM_STATE_BOOTUP:
		fallthrough;
	case FSM_STATE_INVALID:
		fallthrough;
	default:
		atomic_set(&devlink->dev_mode, DEVLINK_DEV_MODE_MISC);
		break;
	}
}

#ifdef CONFIG_MTK_DEVLINK_COREDUMP_SUPPORT
static void mtk_devlink_work(struct work_struct *work)
{
	struct mtk_devlink *devlink;

	devlink = (struct mtk_devlink *)container_of(work, struct mtk_devlink, dl_worker);
	if (unlikely(!devlink))
		return;

	MTK_INFO(devlink->mdev, "Devlink start health dump work\n");
	devlink_health_report(devlink->fw_reporter, "coredump", NULL);
}

static int mtk_devlink_health_reporter_init(struct mtk_md_dev *mdev, struct mtk_devlink *mtk_dl,
					    struct devlink *dl)
{
	int ret = 0;

	mtk_dl->fw_reporter = devlink_health_reporter_create(dl, &mtk_devlink_fw_fatal_reporter_ops,
							     0, mdev);
	if (!mtk_dl->fw_reporter) {
		MTK_ERR(mdev, "Failed to create firmware reporter\n");
		ret = -ENOMEM;
		return ret;
	}
	INIT_WORK(&mtk_dl->dl_worker, mtk_devlink_work);
	return ret;
}

static void mtk_devlink_health_exit(struct mtk_devlink *mtk_dl)
{
	devlink_health_reporter_destroy(mtk_dl->fw_reporter);
}
#else /* !CONFIG_MTK_DEVLINK_COREDUMP_SUPPORT */

static int mtk_devlink_health_reporter_init(struct mtk_md_dev *mdev, struct mtk_devlink *mtk_dl,
					    struct devlink *dl)
{
	return 0;
}

static void mtk_devlink_health_exit(struct mtk_devlink *mtk_dl)
{
}
#endif /* CONFIG_MTK_DEVLINK_COREDUMP_SUPPORT */

#ifdef CONFIG_MTK_DEVLINK_LOGDUMP_SUPPORT
static int mtk_devlink_region_init(struct mtk_md_dev *mdev, struct mtk_devlink *mtk_dl,
				   struct devlink *dl)
{
	int region_size;
	int region_type;
	int ret = 0;
	int index;

	for (index = 0; index < DEVLINK_REGION_CREATE_NUM; index++) {
		region_type = dl_region_tbl[index].log_type;
		if (!enable_host_recved_log && region_type == DEVLINK_REGION_HOST_RECVED_LOG) {
			mtk_dl->log_region[index] = NULL;
			continue;
		}
		region_size = mtk_dev_get_log_region_size(mdev, region_type);
		if (region_size < 0) {
			MTK_ERR(mdev, "Region %s not supported\n", dl_region_tbl[index].ops->name);
			mtk_dl->log_region[index] = NULL;
			continue;
		}
		mtk_dl->log_region[index] = devlink_region_create(dl,
								  dl_region_tbl[index].ops, 1,
								  region_size);
		if (!mtk_dl->log_region[index]) {
			MTK_ERR(mdev, "Failed to create %s\n",
				dl_region_tbl[index].ops->name);
			ret = -ENOMEM;
			goto free_and_exit;
		}
	}
	return ret;

free_and_exit:
	for (index = 0; index < DEVLINK_REGION_CREATE_NUM; index++)
		if (mtk_dl->log_region[index])
			devlink_region_destroy(mtk_dl->log_region[index]);

	MTK_ERR(mdev, "Failed to creat devlink_region\n");
	return ret;
}

static void mtk_devlink_region_exit(struct mtk_devlink *mtk_dl)
{
	int index;

	for (index = 0; index < DEVLINK_REGION_CREATE_NUM; index++)
		if (mtk_dl->log_region[index])
			devlink_region_destroy(mtk_dl->log_region[index]);
}
#else /* !CONFIG_MTK_DEVLINK_LOGDUMP_SUPPORT */

static int mtk_devlink_region_init(struct mtk_md_dev *mdev, struct mtk_devlink *mtk_dl,
				   struct devlink *dl)
{
	return 0;
}

static void mtk_devlink_region_exit(struct mtk_devlink *mtk_dl)
{
}
#endif /* CONFIG_MTK_DEVLINK_LOGDUMP_SUPPORT */

int mtk_devlink_init(struct mtk_md_dev *mdev)
{
	struct mtk_devlink *mtk_dl;
	struct devlink *dl;
	int ret;

	dl = devlink_alloc(&mtk_devlink_ops, sizeof(struct mtk_devlink), mdev->dev);
	if (!dl) {
		MTK_ERR(mdev, "Failed to allocate devlink buffer\n");
		return -ENOMEM;
	}
	mtk_dl = devlink_priv(dl);
	mdev->devlink = mtk_dl;
	mtk_dl->dl = dl;
	mtk_dl->mdev = mdev;

	devlink_register(dl);
	ret = mtk_devlink_health_reporter_init(mdev, mtk_dl, dl);
	if (ret)
		goto exit;
	ret = mtk_devlink_region_init(mdev, mtk_dl, dl);
	if (ret)
		goto exit;
	init_completion(&mtk_dl->download_comp);
	mutex_init(&mtk_dl->dl_mutex);
	mutex_init(&mtk_dl->fb_mutex);
	mutex_init(&mtk_dl->get_log_mutex);
	mtk_dl->dl_ws = wakeup_source_register(mdev->dev, DEVLINK_WAKEUP_SOURCE_NAME);
	if (!mtk_dl->dl_ws) {
		MTK_ERR(mdev, "Failed to register wakeup source for devlink\n");
		goto free_and_exit;
	}
	ret = mtk_fsm_notifier_register(mdev, MTK_USER_DEVLINK, mtk_devlink_fsm_callback,
					mdev, FSM_PRIO_1, false);
	if (ret) {
		MTK_ERR(mdev, "Failed to register FSM\n");
		goto free_and_exit;
	}
	atomic_set(&mtk_dl->dev_mode, DEVLINK_DEV_MODE_OFF);
	atomic_set(&mtk_dl->ch_state[FB_CH_DOWNLOAD].opened, 0);
	atomic_set(&mtk_dl->ch_state[FB_CH_COREDUMP].opened, 0);
	return ret;

free_and_exit:
	mtk_devlink_region_exit(mtk_dl);
	mtk_devlink_health_exit(mtk_dl);
exit:
	devlink_free(mtk_dl->dl);
	return ret;
}
EXPORT_SYMBOL(mtk_devlink_init);

void mtk_devlink_exit(struct mtk_md_dev *mdev)
{
	struct mtk_devlink *mtk_dl = mdev->devlink;

	MTK_INFO(mdev, "Devlink exits\n");
	mtk_fsm_notifier_unregister(mdev, MTK_USER_DEVLINK);
	if (test_bit(MTK_DEVLINK_FLAG_ENTER_DOWNLOAD, &mtk_dl->flags))
		complete(&mtk_dl->download_comp);
	wakeup_source_unregister(mtk_dl->dl_ws);
	devlink_unregister(mtk_dl->dl);
	mutex_lock(&mtk_dl->dl_mutex);
	mtk_devlink_region_exit(mtk_dl);
	mtk_devlink_health_exit(mtk_dl);
	mutex_unlock(&mtk_dl->dl_mutex);
	devlink_free(mtk_dl->dl);
}
EXPORT_SYMBOL(mtk_devlink_exit);

module_param(enable_host_recved_log, bool, 0644);
MODULE_PARM_DESC(enable_host_recved_log, "This value is used to enable HOST_RECVED_LOG region\n");
