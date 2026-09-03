// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spmi.h>

#include "google_queue_mailbox_client_utils.h"

/* TODO(b/308790173): replace with spmi_service.h when available */
#define SPMI_REQ_ADDR_MASK GENMASK(15, 0)
#define SPMI_REQ_BC_MASK GENMASK(19, 16)
#define SPMI_REQ_SID_MASK GENMASK(23, 20)
#define SPMI_REQ_CMD_MASK GENMASK(28, 24)
#define SPMI_REQ_SEQ_MASK BIT(29)
#define SPMI_REQ_RSV_MASK GENMASK(31, 30)

#define SPMI_REQ_SEQ_FIRST    0
#define SPMI_REQ_SEQ_SECOND   1

#define SPMI_RSP_RSV_MASK GENMASK(23, 0)
#define SPMI_RSP_STATUS_MASK GENMASK(31, 24)

#define SPMI_MAX_BYTES 16ul
#define SPMI_MAX_BYTES_PER_MSG 8ul

#define SPMI_ADDR_LIMIT 0x1f
#define SPMI_ADDR_EXT_LIMIT 0xff
#define SPMI_ADDR_EXT_LONG_LIMIT 0xffff

#define MBA_CLIENT_TX_TOUT 3000 /* in ms */

#define SPMI_REQ_CMD_REG_WRITE       0x00
#define SPMI_REQ_CMD_REG_READ        0x01
#define SPMI_REQ_CMD_EXT_REG_WRITE   0x02
#define SPMI_REQ_CMD_EXT_REG_READ    0x03
#define SPMI_REQ_CMD_EXT_REG_WRITEL  0x04
#define SPMI_REQ_CMD_EXT_REG_READL   0x05
#define SPMI_REQ_CMD_EXT_REG_WRITE0  0x06
#define SPMI_REQ_CMD_RESET           0x0a
#define SPMI_REQ_CMD_SLEEP           0x0b
#define SPMI_REQ_CMD_SHUTDOWN        0x0c
#define SPMI_REQ_CMD_WAKEUP          0x0d
#define SPMI_REQ_CMD_AUTHENTICATE    0x0e
#define SPMI_REQ_CMD_DDB_MASTER_READ 0x0f
#define SPMI_REQ_CMD_DDB_SLAVE_READ  0x10
#define SPMI_REQ_CMD_TRANSFER_BUS    0x11
#define SPMI_REQ_CMD_BUS_CONNECT     0x12

struct spmi_mba_info {
	struct device		*dev;
	struct spmi_controller	*ctrl;

	struct mbox_client	client;
	struct mbox_chan	*channel;
	u32			remote_id;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry		*debugfs_root;
	u8			dbg_sid;
	u8			dbg_len;
	u16			dbg_addr;
	u32			dbg_data[4];
#endif
	struct class		*cdev_class;
	dev_t			cdev_num;
	struct cdev		cdev;
};

enum mb_spmi_rsp_status {
	MB_SPMI_RSP_STS_OK,
	MB_SPMI_RSP_STS_NEXT,
	MB_SPMI_RSP_STS_INVALID_TARGET,
	MB_SPMI_RSP_STS_INVALID_CMD,
	MB_SPMI_RSP_STS_INVALID_ID,
	MB_SPMI_RSP_STS_INVALID_VALUE,
	MB_SPMI_RSP_STS_NOT_PERMITTED,
	MB_SPMI_RSP_STS_ERROR,
};

static int lnx_to_mba(uint8_t lnx)
{
	switch (lnx) {
	case SPMI_CMD_WRITE:
		return SPMI_REQ_CMD_REG_WRITE;
	case SPMI_CMD_READ:
		return SPMI_REQ_CMD_REG_READ;
	case SPMI_CMD_EXT_WRITE:
		return SPMI_REQ_CMD_EXT_REG_WRITE;
	case SPMI_CMD_EXT_READ:
		return SPMI_REQ_CMD_EXT_REG_READ;
	case SPMI_CMD_EXT_WRITEL:
		return SPMI_REQ_CMD_EXT_REG_WRITEL;
	case SPMI_CMD_EXT_READL:
		return SPMI_REQ_CMD_EXT_REG_READL;
	case SPMI_CMD_ZERO_WRITE:
		return SPMI_REQ_CMD_EXT_REG_WRITE0;
	case SPMI_CMD_MSTR_READ:
		return SPMI_REQ_CMD_REG_READ;
	case SPMI_CMD_MSTR_WRITE:
		return SPMI_REQ_CMD_REG_WRITE;
	case SPMI_CMD_RESET:
		return SPMI_REQ_CMD_RESET;
	case SPMI_CMD_SLEEP:
		return SPMI_REQ_CMD_SLEEP;
	case SPMI_CMD_SHUTDOWN:
		return SPMI_REQ_CMD_SHUTDOWN;
	case SPMI_CMD_WAKEUP:
		return SPMI_REQ_CMD_WAKEUP;
	case SPMI_CMD_AUTHENTICATE:
		return SPMI_REQ_CMD_AUTHENTICATE;
	case SPMI_CMD_DDB_MASTER_READ:
		return SPMI_REQ_CMD_DDB_MASTER_READ;
	case SPMI_CMD_DDB_SLAVE_READ:
		return SPMI_REQ_CMD_DDB_SLAVE_READ;
	case SPMI_CMD_TRANSFER_BUS_OWNERSHIP:
		return SPMI_REQ_CMD_TRANSFER_BUS;
	default:
		return -EINVAL;
	}
}

/*
 * Copy the valid subset of the mailbox payload dwords to the kernel
 * interface's u8 array. The number of bytes (num_bytes) is not required
 * to be multiple of 4.
 */
static void spmi_unpack(const u32 dwords[], u8 bytes[], size_t num_bytes)
{
	const size_t num_dws = DIV_ROUND_UP(num_bytes, sizeof(*dwords)); /* number of dwords */
	size_t dw_idx;  /* index into input dwords */
	size_t dw_byte_idx; /* byte index relative to current dword */
	size_t dw_byte_lim; /* number of bytes in current dword */

	for (dw_idx = 0; dw_idx < num_dws; dw_idx++) {
		u32 data = dwords[dw_idx];

		dw_byte_lim = min(sizeof(*dwords), (num_bytes - dw_idx * sizeof(*dwords)));
		for (dw_byte_idx = 0; dw_byte_idx < dw_byte_lim; dw_byte_idx++) {
			*bytes++ = data & 0xFF;
			data >>= BITS_PER_BYTE;
		}
	}
}

/*
 * Copy the kernel interface's u8 array values to the mailbox payload dwords.
 * The number of bytes (num_bytes) is not required to be multiple of 4.
 */
static void spmi_pack(const u8 bytes[], u32 dwords[], size_t num_bytes)
{
	const size_t num_dws = DIV_ROUND_UP(num_bytes, sizeof(*dwords)); /* number of dwords */
	size_t dw_idx;  /* index into input dwords */
	size_t dw_byte_idx; /* byte index relative to current dword */
	size_t dw_byte_lim; /* number of bytes in current dword */
	size_t byte_idx = 0;  /* overall index into output bytes array */

	for (dw_idx = 0; dw_idx < num_dws; dw_idx++) {
		size_t shift = 0;

		dw_byte_lim = min(sizeof(*dwords), (num_bytes - dw_idx * sizeof(*dwords)));
		for (dw_byte_idx = 0; dw_byte_idx < dw_byte_lim; dw_byte_idx++) {
			dwords[dw_idx] |= bytes[byte_idx++] << shift;
			shift += BITS_PER_BYTE;
		}
	}
}

static int spmi_mba_command_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	struct spmi_mba_info *info = spmi_controller_get_drvdata(ctrl);
	struct mba_data_package package = {0};
	struct mba_transport_payload *req_payload;
	struct mba_transport_payload rsp_payload;
	struct mba_queue_msg_hdr *msg_hdr;
	int cpm_opc;
	int ret;

	req_payload = &package.payload;
	msg_hdr = &req_payload->hdr;
	msg_hdr->type = MBA_TRANSPORT_TYPE_REQ;
	msg_hdr->dst = info->remote_id;

	cpm_opc = lnx_to_mba(opc);
	if (cpm_opc < 0)
		return -EINVAL;
	req_payload->data[0] = FIELD_PREP(SPMI_REQ_SEQ_MASK, SPMI_REQ_SEQ_FIRST) |
			       FIELD_PREP(SPMI_REQ_CMD_MASK, cpm_opc) |
			       FIELD_PREP(SPMI_REQ_SID_MASK, sid);

	ret = google_mba_send_msg_and_block(info->dev, info->channel,
					    &package, &rsp_payload,
					    msecs_to_jiffies(MBA_CLIENT_TX_TOUT));
	if (ret < 0)
		dev_err(info->dev, "Send request failed ret (%d)\n", ret);

	return ret;
}

static int spmi_mba_write_cmd(struct spmi_controller *ctrl,
			      u8 opc, u8 sid, u16 addr,
			      const u8 *buf, size_t len)
{
	struct spmi_mba_info *info = spmi_controller_get_drvdata(ctrl);
	struct mba_data_package package = {0};
	struct mba_transport_payload *req_payload;
	struct mba_transport_payload rsp_payload;
	struct mba_queue_msg_hdr *msg_hdr;
	enum mb_spmi_rsp_status rsp_status;
	u8 cpm_opc;
	int ret;

	req_payload = &package.payload;
	msg_hdr = &req_payload->hdr;
	msg_hdr->type = MBA_TRANSPORT_TYPE_REQ;
	msg_hdr->dst = info->remote_id;

	cpm_opc = lnx_to_mba(opc);
	if (cpm_opc < 0 || len > SPMI_MAX_BYTES)
		return -EINVAL;
	req_payload->data[0] = FIELD_PREP(SPMI_REQ_SEQ_MASK, SPMI_REQ_SEQ_FIRST) |
			       FIELD_PREP(SPMI_REQ_CMD_MASK, cpm_opc) |
			       FIELD_PREP(SPMI_REQ_SID_MASK, sid) |
			       FIELD_PREP(SPMI_REQ_ADDR_MASK, addr) |
			       FIELD_PREP(SPMI_REQ_BC_MASK, len - 1);
	spmi_pack(&buf[0], &req_payload->data[1],
		  min(SPMI_MAX_BYTES_PER_MSG, len));

	ret = google_mba_send_msg_and_block(info->dev, info->channel,
					    &package, &rsp_payload,
					    msecs_to_jiffies(MBA_CLIENT_TX_TOUT));
	if (ret < 0) {
		dev_err(info->dev, "Send request failed ret (%d)\n", ret);
		return ret;
	}

	rsp_status = FIELD_GET(SPMI_RSP_STATUS_MASK, rsp_payload.data[0]);
	if (rsp_status != MB_SPMI_RSP_STS_OK &&
	    rsp_status != MB_SPMI_RSP_STS_NEXT) {
		dev_err(info->dev, "SPMI Error failed ret (%u)\n", rsp_status);
		return -EIO;
	}

	if (len <= SPMI_MAX_BYTES_PER_MSG)
		return 0;

	/* data is too big to fit into single message send the rest now */
	/* this will actually initiate the write                        */
	spmi_pack(&buf[SPMI_MAX_BYTES_PER_MSG], &req_payload->data[1],
		  len - SPMI_MAX_BYTES_PER_MSG);
	req_payload->data[0] &= ~SPMI_REQ_SEQ_MASK;
	req_payload->data[0] |= FIELD_PREP(SPMI_REQ_SEQ_MASK, SPMI_REQ_SEQ_SECOND);

	ret = google_mba_send_msg_and_block(info->dev, info->channel,
					    &package, &rsp_payload,
					    msecs_to_jiffies(MBA_CLIENT_TX_TOUT));
	if (ret < 0) {
		dev_err(info->dev, "Send request failed ret (%d)\n", ret);
		return ret;
	}

	rsp_status = FIELD_GET(SPMI_RSP_STATUS_MASK, rsp_payload.data[0]);
	if (rsp_status != MB_SPMI_RSP_STS_OK) {
		dev_err(info->dev, "SPMI Error failed ret (%u)\n", rsp_status);
		ret = -EIO;
	}

	return ret;
}

static int spmi_mba_read_cmd(struct spmi_controller *ctrl,
			     u8 opc, u8 sid, u16 addr, u8 *buf, size_t len)
{
	struct spmi_mba_info *info = spmi_controller_get_drvdata(ctrl);
	struct mba_data_package package = {0};
	struct mba_transport_payload *req_payload;
	struct mba_transport_payload rsp_payload;
	struct mba_queue_msg_hdr *msg_hdr;
	enum mb_spmi_rsp_status rsp_status;
	int cpm_opc;
	int ret;
	u32 dwords[4];

	req_payload = &package.payload;
	msg_hdr = &req_payload->hdr;
	msg_hdr->type = MBA_TRANSPORT_TYPE_REQ;
	msg_hdr->dst = info->remote_id;

	cpm_opc = lnx_to_mba(opc);
	if (cpm_opc < 0 || len > SPMI_MAX_BYTES)
		return -EINVAL;
	req_payload->data[0] = FIELD_PREP(SPMI_REQ_SEQ_MASK, SPMI_REQ_SEQ_FIRST) |
			       FIELD_PREP(SPMI_REQ_CMD_MASK, cpm_opc) |
			       FIELD_PREP(SPMI_REQ_SID_MASK, sid) |
			       FIELD_PREP(SPMI_REQ_ADDR_MASK, addr) |
			       FIELD_PREP(SPMI_REQ_BC_MASK, len - 1);

	ret = google_mba_send_msg_and_block(info->dev, info->channel,
					    &package, &rsp_payload,
					    msecs_to_jiffies(MBA_CLIENT_TX_TOUT));
	if (ret < 0) {
		dev_err(info->dev, "Send request failed ret (%d)\n", ret);
		return ret;
	}

	rsp_status = FIELD_GET(SPMI_RSP_STATUS_MASK, rsp_payload.data[0]);
	if (rsp_status != MB_SPMI_RSP_STS_OK &&
	    rsp_status != MB_SPMI_RSP_STS_NEXT) {
		dev_err(info->dev, "SPMI Error failed ret (%u)\n", rsp_status);
		return -EIO;
	}

	if (likely(len <= SPMI_MAX_BYTES_PER_MSG)) {
		spmi_unpack(&rsp_payload.data[1], &buf[0],
			    min(SPMI_MAX_BYTES_PER_MSG, len));
		return 0;
	}
	dwords[0] = rsp_payload.data[1];
	dwords[1] = rsp_payload.data[2];

	/* data is too big to fit into single message send the rest now */
	/* this will actually initiate the write                        */
	req_payload->data[0] &= ~SPMI_REQ_SEQ_MASK;
	req_payload->data[0] |= FIELD_PREP(SPMI_REQ_SEQ_MASK, SPMI_REQ_SEQ_SECOND);
	ret = google_mba_send_msg_and_block(info->dev, info->channel,
					    &package, &rsp_payload,
					    msecs_to_jiffies(MBA_CLIENT_TX_TOUT));
	if (ret < 0) {
		dev_err(info->dev, "Send request failed ret (%d)\n", ret);
		return ret;
	}
	dwords[2] = rsp_payload.data[1];
	dwords[3] = rsp_payload.data[2];

	spmi_unpack(dwords, &buf[0], len);

	return ret;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
#define SPMI_DEBUG_ATTRIBUTE(name, fn_read, fn_write) \
static const struct file_operations name = {    \
	.open   = simple_open,                  \
	.read   = fn_read,                      \
	.write  = fn_write,                     \
}

static int debugfs_read_cmd(struct spmi_mba_info *info)
{
	u16 addr = info->dbg_addr;
	u8 len = info->dbg_len;
	u8 sid = info->dbg_sid;
	u8 opc;
	u8 buf[SPMI_MAX_BYTES] = {0};
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(info->dbg_data); i++)
		info->dbg_data[i] = 0;

	if (addr <= SPMI_ADDR_LIMIT && len == 1) {
		opc = SPMI_CMD_READ;
	} else if (addr <= SPMI_ADDR_EXT_LIMIT && len <= 16) {
		opc = SPMI_CMD_EXT_READ;
	} else if (addr <= SPMI_ADDR_EXT_LONG_LIMIT && len <= 8) {
		opc = SPMI_CMD_EXT_READL;
	} else {
		dev_err(info->dev, "Invalid SPMI command generated\n");
		return -EINVAL;
	}
	err = spmi_mba_read_cmd(info->ctrl, opc, sid, addr, buf, len);
	if (err) {
		dev_err(info->dev, "Unable to read (%d)\n", err);
		return err;
	}
	spmi_pack(buf, info->dbg_data, len);

	return 0;
}

static int debugfs_write_cmd(struct spmi_mba_info *info)
{
	u16 addr = info->dbg_addr;
	u8 len = info->dbg_len;
	u8 sid = info->dbg_sid;
	u8 opc;
	u8 buf[SPMI_MAX_BYTES];

	if (addr == 0 && len == 1) {
		opc = SPMI_CMD_ZERO_WRITE;
	} else if (addr <= SPMI_ADDR_LIMIT && len == 1) {
		opc = SPMI_CMD_WRITE;
	} else if (addr <= SPMI_ADDR_EXT_LIMIT && len <= 16) {
		opc = SPMI_CMD_EXT_WRITE;
	} else if (addr <= SPMI_ADDR_EXT_LONG_LIMIT && len <= 8) {
		opc = SPMI_CMD_EXT_WRITEL;
	} else {
		dev_err(info->dev, "Invalid SPMI command generated\n");
		return -EINVAL;
	}
	spmi_unpack(info->dbg_data, buf, len);

	return spmi_mba_write_cmd(info->ctrl, opc, sid, addr, buf, len);
}

static ssize_t debugfs_cmd_write(struct file *filp, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct spmi_controller *ctrl = (struct spmi_controller *)filp->private_data;
	struct spmi_mba_info *info = spmi_controller_get_drvdata(ctrl);
	char cmd;
	int err;

	err = copy_from_user(&cmd, user_buf, 1);
	if (err)
		goto exit_write;

	switch (toupper(cmd)) {
	case 'W':
		err = debugfs_write_cmd(info);
		break;
	case 'R':
		err = debugfs_read_cmd(info);
		break;
	default:
		dev_err(info->dev, "Invalid command %c\n", cmd);
		break;
	}

exit_write:
	return count;
}
SPMI_DEBUG_ATTRIBUTE(cmd_fops, NULL, debugfs_cmd_write);

static int spmi_mba_debugfs_init(struct platform_device *pdev)
{
	struct spmi_mba_info *info = platform_get_drvdata(pdev);

	if (!debugfs_initialized())
		return -ENODEV;

	info->debugfs_root = debugfs_create_dir("spmi_mba", NULL);
	debugfs_create_x8("sid", 0660, info->debugfs_root, &info->dbg_sid);
	debugfs_create_u8("len", 0660, info->debugfs_root, &info->dbg_len);
	debugfs_create_x16("addr", 0660, info->debugfs_root, &info->dbg_addr);
	debugfs_create_x32("dat0", 0660, info->debugfs_root, &info->dbg_data[0]);
	debugfs_create_x32("dat1", 0660, info->debugfs_root, &info->dbg_data[1]);
	debugfs_create_x32("dat2", 0660, info->debugfs_root, &info->dbg_data[2]);
	debugfs_create_x32("dat3", 0660, info->debugfs_root, &info->dbg_data[3]);
	debugfs_create_file("cmd", 0660, info->debugfs_root, info->ctrl, &cmd_fops);

	return 0;
}
#else
static int spmi_mba_debugfs_init(struct platform_device *pdev)
{
	return 0;
}
#endif

static int spmi_mba_probe(struct platform_device *pdev)
{
	struct spmi_mba_info *spmi_mba_info;
	struct spmi_controller *ctrl;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	u32 dest_channel;
	int err = 0;

	if (of_property_read_u32(np, "mba-dest-channel", &dest_channel)) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		return -EINVAL;
	}

	ctrl = spmi_controller_alloc(&pdev->dev, sizeof(*spmi_mba_info));
	if (!ctrl) {
		dev_err(dev, "cannot allocate spmi_mba_info\n");
		return -ENOMEM;
	}

	spmi_mba_info = spmi_controller_get_drvdata(ctrl);
	spmi_mba_info->ctrl = ctrl;
	dev_set_drvdata(&ctrl->dev, spmi_mba_info);
	platform_set_drvdata(pdev, spmi_mba_info);

	ctrl->cmd = spmi_mba_command_cmd;
	ctrl->read_cmd = spmi_mba_read_cmd;
	ctrl->write_cmd = spmi_mba_write_cmd;

	spmi_mba_info->remote_id = dest_channel;
	spmi_mba_info->client.dev = dev;
	spmi_mba_info->client.tx_block = true;
	spmi_mba_info->client.tx_tout = MBA_CLIENT_TX_TOUT;

	spmi_mba_info->channel = mbox_request_channel(&spmi_mba_info->client, 0);
	if (IS_ERR(spmi_mba_info->channel)) {
		err = PTR_ERR(spmi_mba_info->channel);
		dev_err(dev, "Failed to request mailbox channel err %d.\n", err);
		goto err_put_controller;
	}

	err = spmi_controller_add(ctrl);
	if (err) {
		dev_err(&pdev->dev, "Unable to add controller (%d)!\n", err);
		goto err_free_mbox;
	}

	spmi_mba_debugfs_init(pdev);

	return 0;

err_free_mbox:
	mbox_free_channel(spmi_mba_info->channel);
err_put_controller:
	spmi_controller_put(ctrl);
	return err;
}

static void spmi_mba_remove(struct platform_device *pdev)
{
	struct spmi_mba_info *info = platform_get_drvdata(pdev);
	struct spmi_controller *ctrl = info->ctrl;

	spmi_controller_remove(ctrl);
	spmi_controller_put(ctrl);
}

static const struct of_device_id spmi_mba_match_table[] = {
	{
		.compatible = "google,spmi-mba-ctrl",
	},
	{}
};
MODULE_DEVICE_TABLE(of, spmi_mba_match_table);

static struct platform_driver spmi_mba_driver = {
	.probe		= spmi_mba_probe,
	.remove		= spmi_mba_remove,
	.driver		= {
		.name	= "spmi_mba_controller",
		.of_match_table = spmi_mba_match_table,
	},
};

module_platform_driver(spmi_mba_driver);
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
MODULE_DESCRIPTION("Google SPMI -> MBA -> CPM driver");
MODULE_AUTHOR("Jim Wylder<jwylder@google.com>");
