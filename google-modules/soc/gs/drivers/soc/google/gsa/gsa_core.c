// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for the Google GSA core.
 *
 * Copyright (C) 2020 Google LLC
 */
#include <kunit/visibility.h>
#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/gsa.h>
#include <linux/gsa/gsa_aoc.h>
#include <linux/gsa/gsa_dsp.h>
#include <linux/gsa/gsa_kdn.h>
#include <linux/gsa/gsa_sjtag.h>
#include <linux/gsa/gsa_tpu.h>
#include <linux/idr.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "gsa_debugfs.h"
#include "gsa_log.h"
#include "gsa_mbox.h"
#include "gsa_priv.h"
#include "gsa_tz.h"
#include "hwmgr-ipc.h"

#define MAX_DEVICES 2

static struct class *gsa_cdev_class;
static dev_t gsa_cdev_base_num;
static DEFINE_IDR(gsa_cdev_devices);

/*
 *  Internal command interface
 */
int gsa_send_cmd(struct device *dev, u32 cmd, u32 *req, u32 req_argc,
		 u32 *rsp, u32 rsp_argc)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_send_mbox_cmd(s->mb, cmd, req, req_argc, rsp, rsp_argc);
};
EXPORT_SYMBOL_GPL(gsa_send_cmd);

int gsa_send_simple_cmd(struct device *dev, u32 cmd)
{
	return gsa_send_cmd(dev, cmd, NULL, 0, NULL, 0);
}
EXPORT_SYMBOL_GPL(gsa_send_simple_cmd);

int gsa_send_one_arg_cmd(struct device *dev, u32 cmd, u32 arg)
{
	return gsa_send_cmd(dev, cmd, &arg, 1, NULL, 0);
}
EXPORT_SYMBOL_GPL(gsa_send_one_arg_cmd);

static int gsa_send_load_img_cmd(struct device *dev, uint32_t cmd,
				 dma_addr_t hdr_da, phys_addr_t body_pa)
{
	u32 req[4];
	struct platform_device *pdev = to_platform_device(dev);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	req[IMG_LOADER_HEADER_ADDR_LO_IDX] = (u32)hdr_da;
	req[IMG_LOADER_HEADER_ADDR_HI_IDX] = (u32)(hdr_da >> 32);
	req[IMG_LOADER_BODY_ADDR_LO_IDX] = (u32)body_pa;
	req[IMG_LOADER_BODY_ADDR_HI_IDX] = (u32)(body_pa >> 32);

	return gsa_send_mbox_cmd(s->mb, cmd, req, 4, NULL, 0);
}

static int gsa_send_pq_load_img_cmd(struct device *dev,
				 dma_addr_t hdr_da, phys_addr_t body_pa, size_t body_len)
{
	u32 req[PQ_IMG_LOADER_ARGC];
	struct platform_device *pdev = to_platform_device(dev);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	req[PQ_IMG_LOADER_HEADER_ADDR_LO_IDX] = (u32)hdr_da;
	req[PQ_IMG_LOADER_HEADER_ADDR_HI_IDX] = (u32)(hdr_da >> 32);
	req[PQ_IMG_LOADER_BODY_ADDR_LO_IDX] = (u32)body_pa;
	req[PQ_IMG_LOADER_BODY_ADDR_HI_IDX] = (u32)(body_pa >> 32);
	req[PQ_IMG_LOADER_BODY_SIZE_IDX] = (u32)body_len;
	req[PQ_IMG_LOADER_HEADER_SIZE_IDX] = (u32)GSA_PQ_IMAGE_HEADER_SIZE;

	return gsa_send_mbox_cmd(s->mb, GSA_MB_CMD_PQ_AUTH_IMG, req, PQ_IMG_LOADER_ARGC, NULL, 0);
}

static int gsa_tz_send_hwmgr_state_cmd(struct gsa_dev_state *s,
				struct gsa_tz_chan_ctx *ctx, u32 cmd)
{
	int rc;
	struct {
		struct hwmgr_rsp_hdr hdr;
		struct hwmgr_state_cmd_rsp rsp;
	} rsp_msg;

	struct {
		struct hwmgr_req_hdr hdr;
		struct hwmgr_state_cmd_req req;
	} req_msg;


	req_msg.hdr.cmd = HWMGR_CMD_STATE_CMD;
	req_msg.req.cmd = cmd;

	rc = gsa_tz_chan_msg_xchg(ctx,
				  &req_msg, sizeof(req_msg),
				  &rsp_msg, sizeof(rsp_msg));

	/*
	 * TZ sends a header-only message in case of error.
	 * Therefore check we received at least a header before accessing those fields.
	 */
	if (rc < sizeof(rsp_msg.hdr)) {
		dev_err(s->dev, "short response (%d).\n", rc);
		return -EIO;
	}

	if (rsp_msg.hdr.cmd != (req_msg.hdr.cmd | HWMGR_CMD_RESP)) {
		dev_err(s->dev, "bad response hdr.cmd (%d).\n", rsp_msg.hdr.cmd);
		return -EIO;
	}

	if (rsp_msg.hdr.err) {
		dev_err(s->dev, "bad response hdr.err (%d).\n", rsp_msg.hdr.err);
		return -EIO;
	}

	if (rc != sizeof(rsp_msg)) {
		dev_err(s->dev, "bad response len (%d).\n", rc);
		return -EIO;
	}

	return rsp_msg.rsp.state;
}

static int gsa_tz_send_hwmgr_unload_fw_image_cmd(struct gsa_tz_chan_ctx *ctx)
{
	int rc;
	struct {
		struct hwmgr_rsp_hdr hdr;
	} rsp_msg;

	struct {
		struct hwmgr_req_hdr hdr;
	} req_msg;


	req_msg.hdr.cmd = HWMGR_CMD_UNLOAD_IMG;

	rc = gsa_tz_chan_msg_xchg(ctx,
				  &req_msg, sizeof(req_msg),
				  &rsp_msg, sizeof(rsp_msg));

	if (rc != sizeof(rsp_msg)) {
		return -EIO;
	}

	if (rsp_msg.hdr.cmd != (req_msg.hdr.cmd | HWMGR_CMD_RESP)) {
		return -EIO;
	}

	if (rsp_msg.hdr.err) {
		return -EIO;
	}

	return 0;
}

/*
 *  External AOC interface
 */
int gsa_load_aoc_fw_image(struct device *gsa,
			  dma_addr_t img_meta,
			  phys_addr_t img_body)
{
	return gsa_send_load_img_cmd(gsa, GSA_MB_CMD_LOAD_AOC_FW_IMG,
				     img_meta, img_body);
}
EXPORT_SYMBOL_GPL(gsa_load_aoc_fw_image);

int gsa_load_aoc_fw_image_pq(struct device *gsa,
			  dma_addr_t img_meta,
			  phys_addr_t img_body,
			  size_t body_len)
{
	return gsa_send_pq_load_img_cmd(gsa, img_meta, img_body, body_len);
}
EXPORT_SYMBOL_GPL(gsa_load_aoc_fw_image_pq);

int gsa_unload_aoc_fw_image(struct device *gsa)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_tz_send_hwmgr_unload_fw_image_cmd(&s->aoc_srv);
}
EXPORT_SYMBOL_GPL(gsa_unload_aoc_fw_image);

int gsa_send_aoc_cmd(struct device *gsa, enum gsa_aoc_cmd arg)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_tz_send_hwmgr_state_cmd(s, &s->aoc_srv, arg);
}
EXPORT_SYMBOL_GPL(gsa_send_aoc_cmd);

/*
 *  External TPU interface
 */
int gsa_load_tpu_fw_image(struct device *gsa,
			  dma_addr_t img_meta,
			  phys_addr_t img_body)
{
	return gsa_send_load_img_cmd(gsa, GSA_MB_CMD_LOAD_TPU_FW_IMG,
				     img_meta, img_body);
}
EXPORT_SYMBOL_GPL(gsa_load_tpu_fw_image);

int gsa_load_tpu_fw_image_pq(struct device *gsa,
			  dma_addr_t img_meta,
			  phys_addr_t img_body,
			  size_t body_len)
{
	return gsa_send_pq_load_img_cmd(gsa, img_meta, img_body, body_len);
}
EXPORT_SYMBOL_GPL(gsa_load_tpu_fw_image_pq);

int gsa_unload_tpu_fw_image(struct device *gsa)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_tz_send_hwmgr_unload_fw_image_cmd(&s->tpu_srv);
}
EXPORT_SYMBOL_GPL(gsa_unload_tpu_fw_image);

int gsa_send_tpu_cmd(struct device *gsa, enum gsa_tpu_cmd arg)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_tz_send_hwmgr_state_cmd(s, &s->tpu_srv, arg);
}
EXPORT_SYMBOL_GPL(gsa_send_tpu_cmd);

/*
 *  External DSP interface
 */
int gsa_load_dsp_fw_image(struct device *gsa,
			  dma_addr_t img_meta,
			  phys_addr_t img_body)
{
	return gsa_send_load_img_cmd(gsa, GSA_MB_CMD_LOAD_DSP_FW_IMG,
				     img_meta, img_body);
}
EXPORT_SYMBOL_GPL(gsa_load_dsp_fw_image);

int gsa_load_dsp_fw_image_pq(struct device *gsa,
			  dma_addr_t img_meta,
			  phys_addr_t img_body,
			  size_t body_len)
{
	return gsa_send_pq_load_img_cmd(gsa, img_meta, img_body, body_len);
}
EXPORT_SYMBOL_GPL(gsa_load_dsp_fw_image_pq);

int gsa_unload_dsp_fw_image(struct device *gsa)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_tz_send_hwmgr_unload_fw_image_cmd(&s->dsp_srv);
}
EXPORT_SYMBOL_GPL(gsa_unload_dsp_fw_image);

int gsa_send_dsp_cmd(struct device *gsa, enum gsa_dsp_cmd arg)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_tz_send_hwmgr_state_cmd(s, &s->dsp_srv, arg);
}
EXPORT_SYMBOL_GPL(gsa_send_dsp_cmd);


/*
 *  External KDN interface
 */
static int send_kdn_cmd(struct gsa_dev_state *s, u32 cmd,
			void *dst_buf, size_t dst_buf_sz, u32 opts,
			const void *src_data, size_t src_data_len)
{
	int ret;
	size_t cb;
	u32 req[KDN_REQ_ARGC];
	u32 rsp[KDN_RSP_ARGC];

	if (dst_buf_sz) {
		if (!dst_buf) {
			/* invalid args */
			return -EINVAL;
		}
		if (dst_buf_sz > s->bb_sz) {
			/* too much data */
			return -EINVAL;
		}
	}

	/* copy in data */
	if (src_data_len) {
		if (!src_data) {
			/* invalid args */
			return -EINVAL;
		}

		if (src_data_len > s->bb_sz) {
			/* too much data */
			return -EINVAL;
		}

		memcpy(s->bb_va, src_data, src_data_len);
	}

	/* Invoke KDN command */
	req[KDN_DATA_BUF_ADDR_LO_IDX] = (u32)s->bb_da;
	req[KDN_DATA_BUF_ADDR_HI_IDX] = (u32)(s->bb_da >> 32);
	req[KDN_DATA_BUF_SIZE_IDX] = max_t(u32, dst_buf_sz, src_data_len);
	req[KDN_DATA_LEN_IDX] = (u32)src_data_len;
	req[KDN_OPTION_IDX] = opts;

	ret = gsa_send_mbox_cmd(s->mb, cmd, req, ARRAY_SIZE(req),
				rsp, ARRAY_SIZE(rsp));
	if (ret < 0) {
		/* mailbox command failed */
		return ret;
	}

	if (ret != KDN_RSP_ARGC) {
		/* unexpected reply */
		return -EINVAL;
	}

	/* copy data out */
	cb = rsp[KDN_RSP_DATA_LEN_IDX];

	if (cb > dst_buf_sz) {
		/* buffer too short */
		return -EINVAL;
	}

	if (cb) {
		/* copy data to destination buffer */
		memcpy(dst_buf, s->bb_va, cb);
	}

	return cb;
}

int gsa_kdn_generate_key(struct device *gsa, void *buf, size_t buf_sz)
{
	int ret;
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_kdn_cmd(s, GSA_MB_CMD_KDN_GENERATE_KEY, buf, buf_sz, 0, NULL, 0);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_kdn_generate_key);

int gsa_kdn_prepare_key(struct device *gsa, void *buf, size_t buf_sz,
		const void *key_blob, size_t key_blob_len)
{
	int ret;
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_kdn_cmd(s, GSA_MB_CMD_KDN_EPHEMERAL_WRAP_KEY, buf, buf_sz,
				0, key_blob, key_blob_len);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_kdn_prepare_key);

int gsa_kdn_derive_raw_secret(struct device *gsa, void *buf, size_t buf_sz,
			      const void *key_blob, size_t key_blob_len)
{
	int ret;
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_kdn_cmd(s, GSA_MB_CMD_KDN_DERIVE_RAW_SECRET,
			   buf, buf_sz, 0, key_blob, key_blob_len);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_kdn_derive_raw_secret);

int gsa_kdn_program_key(struct device *gsa, u32 slot, const void *key_blob,
			size_t key_blob_len)
{
	int ret;
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_kdn_cmd(s, GSA_MB_CMD_KDN_PROGRAM_KEY,
			   NULL, 0, slot, key_blob, key_blob_len);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_kdn_program_key);

int gsa_kdn_restore_keys(struct device *gsa)
{
	int ret;

	/* Restore keys is a special no argument command */
	ret = gsa_send_cmd(gsa, GSA_MB_CMD_KDN_RESTORE_KEYS, NULL, 0, NULL, 0);
	if (ret < 0)
		return ret;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(gsa_kdn_restore_keys);


int gsa_kdn_set_operating_mode(struct device *gsa,
			       enum kdn_op_mode mode,
			       enum kdn_ufs_descr_type descr)
{
	int ret;
	u32 req[KDN_SET_OP_MODE_ARGC];

	req[KDN_SET_OP_MODE_MODE_IDX] = mode;
	req[KDN_SET_OP_MODE_UFS_DESCR_IDX] = descr;

	ret = gsa_send_cmd(gsa, GSA_MB_CMD_KDN_SET_OP_MODE,
			   req, ARRAY_SIZE(req), NULL, 0);
	if (ret < 0)
		return ret;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(gsa_kdn_set_operating_mode);

/*
 *   External SJTAG management interface
 */
static int send_sjtag_data_cmd(struct gsa_dev_state *s, u32 cmd,
			       void *dst_buf, size_t dst_buf_sz,
			       const void *src_data, size_t src_data_len,
			       u32 *status)
{
	int ret;
	size_t cb;
	u32 req[SJTAG_DATA_REQ_ARGC];
	u32 rsp[SJTAG_DATA_RSP_ARGC];

	if (dst_buf_sz) {
		if (!dst_buf) {
			/* invalid args */
			return -EINVAL;
		}
		if (dst_buf_sz > s->bb_sz) {
			/* too much data */
			return -EINVAL;
		}
	}

	/* copy in data */
	if (src_data_len) {
		if (!src_data) {
			/* invalid args */
			return -EINVAL;
		}

		if (src_data_len > s->bb_sz) {
			/* too much data */
			return -EINVAL;
		}

		memcpy(s->bb_va, src_data, src_data_len);
	}

	/* Invoke SJTAG command */
	req[SJTAG_DATA_BUF_ADDR_LO_IDX] = (u32)s->bb_da;
	req[SJTAG_DATA_BUF_ADDR_HI_IDX] = (u32)(s->bb_da >> 32);
	req[SJTAG_DATA_BUF_SIZE_IDX] = max_t(u32, dst_buf_sz, src_data_len);
	req[SJTAG_DATA_LEN_IDX] = (u32)src_data_len;

	ret = gsa_send_mbox_cmd(s->mb, cmd, req, ARRAY_SIZE(req),
				rsp, ARRAY_SIZE(rsp));
	if (ret < 0) {
		/* mailbox command failed */
		return ret;
	}

	if (ret != SJTAG_DATA_RSP_ARGC) {
		/* unexpected reply */
		return -EINVAL;
	}

	/* return command status */
	if (status)
		*status = rsp[SJTAG_DATA_RSP_STATUS_IDX];

	/* copy data out */
	cb = rsp[SJTAG_DATA_RSP_DATA_LEN_IDX];

	if (cb > dst_buf_sz) {
		/* buffer too short */
		return -EINVAL;
	}

	if (cb) {
		/* copy data to destination buffer */
		memcpy(dst_buf, s->bb_va, cb);
	}

	return cb;
}

int gsa_sjtag_get_status(struct device *gsa, u32 *debug_allowed, u32 *hw_state,
			 u32 *debug_time)
{
	u32 rsp[SJTAG_STATUS_RSP_ARGC];
	int ret = gsa_send_cmd(gsa, GSA_MB_CMD_SJTAG_GET_STATUS, NULL, 0,
			       rsp, ARRAY_SIZE(rsp));
	if (ret < 0)
		return ret;

	/* we expect exactly 2 parameters */
	if (ret != SJTAG_STATUS_RSP_ARGC)
		return -EIO;

	if (debug_allowed)
		*debug_allowed= rsp[SJTAG_STATUS_RSP_DEBUG_ALLOWED_IDX];

	if (hw_state)
		*hw_state = rsp[SJTAG_STATUS_RSP_HW_STATUS_IDX];

	if (debug_time)
		*debug_time = rsp[SJTAG_STATUS_RSP_DEBUG_TIME_IDX];

	return 0;
}
EXPORT_SYMBOL_GPL(gsa_sjtag_get_status);

int gsa_sjtag_get_chip_id(struct device *gsa, u32 id[2])
{
	int ret = gsa_send_cmd(gsa, GSA_MB_CMD_SJTAG_GET_CHIP_ID, NULL, 0, id, 2);
	if (ret < 0)
		return ret;

	/* we expect exactly 2 parameters */
	if (ret != 2)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_GPL(gsa_sjtag_get_chip_id);

int gsa_sjtag_get_pub_key_hash(struct device *gsa, void *hash, size_t size,
			       u32 *status)
{
	int ret;
	struct gsa_dev_state *s;
	struct platform_device *pdev;

	pdev = to_platform_device(gsa);
	s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_sjtag_data_cmd(s, GSA_MB_CMD_SJTAG_GET_PUB_KEY_HASH,
				  hash, size, NULL, 0, status);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_sjtag_get_pub_key_hash);

int gsa_sjtag_set_pub_key(struct device *gsa, const void *key, size_t size,
			  u32 *status)
{
	int ret;
	struct gsa_dev_state *s;
	struct platform_device *pdev;

	pdev = to_platform_device(gsa);
	s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_sjtag_data_cmd(s, GSA_MB_CMD_SJTAG_SET_PUB_KEY,
				  NULL, 0, key, size, status);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_sjtag_set_pub_key);

int gsa_sjtag_get_challenge(struct device *gsa, void *challenge, size_t size,
			    u32 *status)
{
	int ret;
	struct gsa_dev_state *s;
	struct platform_device *pdev;

	pdev = to_platform_device(gsa);
	s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_sjtag_data_cmd(s, GSA_MB_CMD_SJTAG_GET_CHALLENGE,
				  challenge, size, NULL, 0, status);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_sjtag_get_challenge);

int gsa_sjtag_send_srv_response(struct device *gsa,
				const void *rsp, size_t size,
				u32 *status)
{
	int ret;
	struct gsa_dev_state *s;
	struct platform_device *pdev;

	pdev = to_platform_device(gsa);
	s = platform_get_drvdata(pdev);

	mutex_lock(&s->bb_lock);
	ret = send_sjtag_data_cmd(s, GSA_MB_CMD_SJTAG_ENABLE,
				  NULL, 0, rsp, size, status);
	mutex_unlock(&s->bb_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(gsa_sjtag_send_srv_response);

int gsa_sjtag_end_session(struct device *gsa, u32 *status)
{
	int rc = gsa_send_cmd(gsa, GSA_MB_CMD_SJTAG_FINISH, NULL, 0, status, 1);
	if (rc < 0)
		return rc;

	/* exactly 1 argument is expected  */
	if (rc != 1)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_GPL(gsa_sjtag_end_session);

/*
 *   GSA Character Device
 */

const struct file_operations gsa_cdev_fops = {};

/*
 *   GSA pm_stats Character Device
 */

struct gsa_pm_stat_file {
	struct device *gsa;
	int text_len;
	char text[SZ_1K];
};

static uint64_t to_usec(uint64_t ticks, uint32_t freq)
{
	return (ticks * 1000000) / freq;
}

static int format_gsa_pm_stats(char *buf, size_t size, const struct gsa_pm_stats *st)
{
	int i;
	int at = 0;

	if (!(st->ver == GSA_PM_STATS_V1 || st->ver == GSA_PM_STATS_V2) ||
		!st->tick_freq || !st->uptime_ticks) {
		/* Invalid data */
		return -EINVAL;
	}

	at += scnprintf(buf + at, size - at, "Uptime (usec): %llu\n",
			to_usec(st->uptime_ticks, st->tick_freq));

	at += scnprintf(buf + at, size - at, "Idle: %3llu%%\n",
			(st->idle_ticks * 100) / st->uptime_ticks);
	at += scnprintf(buf + at, size - at, "    total residency (usec):   %12llu\n",
			to_usec(st->idle_ticks, st->tick_freq));
	at += scnprintf(buf + at, size - at, "    event count:  %10llu\n", st->idle_cnt);

	at += scnprintf(buf + at, size - at, "PG:   %3llu%%\n",
			(st->pg_ticks * 100) / st->uptime_ticks);
	at += scnprintf(buf + at, size - at, "    total residency (usec):   %12llu\n",
			to_usec(st->pg_ticks, st->tick_freq));
	at += scnprintf(buf + at, size - at, "    average residency (usec): %12llu\n",
			st->pg_cnt ? to_usec(st->pg_ticks, st->tick_freq) / st->pg_cnt : 0);

	at += scnprintf(buf + at, size - at, "    event count:  %10llu\n", st->pg_cnt);
	at += scnprintf(buf + at, size - at, "    wakeup count: %10llu\n", st->pg_wakeup_cnt);
	at += scnprintf(buf + at, size - at, "    abort count:  %10llu\n", st->pg_abort_cnt);

	if (st->ver == GSA_PM_STATS_V2) {
		at += scnprintf(buf + at, size - at, "RG:   %3llu%%\n",
			(st->rg_ticks * 100) / st->uptime_ticks);
		at += scnprintf(buf + at, size - at, "    total residency (usec):   %12llu\n",
			to_usec(st->rg_ticks, st->tick_freq));
		at += scnprintf(buf + at, size - at, "    average residency (usec): %12llu\n",
			st->rg_cnt ? to_usec(st->rg_ticks, st->tick_freq) / st->rg_cnt : 0);

		at += scnprintf(buf + at, size - at, "    event count:  %10llu\n",
			st->rg_cnt);
		at += scnprintf(buf + at, size - at, "    wakeup count: %10llu\n",
			st->rg_wakeup_cnt);
		at += scnprintf(buf + at, size - at, "    abort count:  %10llu\n",
			st->rg_abort_cnt);
	}

	at += scnprintf(buf + at, size - at, "Longest sleep (usec): %12llu\n",
			to_usec(st->longest_sleep, st->tick_freq));
	at += scnprintf(buf + at, size - at, "Longest awake (usec): %12llu\n",
			to_usec(st->longest_awake, st->tick_freq));

	at += scnprintf(buf + at, size - at, "Suspend hint count:   %12u\n", st->ap_suspend_cnt);
	at += scnprintf(buf + at, size - at, "Resume hint count:    %12u\n", st->ap_resume_cnt);

	at += scnprintf(buf + at, size - at, "Wakelock state: %#x\n", st->wakelock_state);
	at += scnprintf(buf + at, size - at, "Wakelock acquire/release count:\n");

	for (i = 0; i < MAX_WAKELOCK_NUM; i++) {
		at += scnprintf(buf + at, size - at, "    mb[%u]: %8u %8u\n", i,
				st->wakelock_acquire_cnt[i], st->wakelock_release_cnt[i]);
	}

	return at;
}

int gsa_pm_stat_open(struct device *dev, struct gsa_dev_state *s, struct file *filp)
{
	int ret;
	u32 req[GET_PM_STATS_REQ_ARGC];
	u32 rsp[GET_PM_STATS_RSP_ARGC];
	struct gsa_pm_stat_file *stat_file = kzalloc(sizeof(struct gsa_pm_stat_file), GFP_KERNEL);

	if (!stat_file)
		return -ENOMEM;

	stat_file->gsa = dev;
	filp->private_data = stat_file;

	req[GET_PM_STATS_REQ_DATA_BUF_ADDR_LO_IDX] = (u32)s->bb_da;
	req[GET_PM_STATS_REQ_DATA_BUF_ADDR_HI_IDX] = (u32)(s->bb_da >> 32);
	req[GET_PM_STATS_REQ_DATA_BUF_SIZE] = s->bb_sz;

	mutex_lock(&s->bb_lock);
	ret = gsa_send_cmd(dev, GSA_MB_CMD_GET_PM_STATS, req, ARRAY_SIZE(req), rsp,
			   ARRAY_SIZE(rsp));
	if (ret < 0)
		goto err;

	/* Check for unexpected reply */
	if (rsp[GET_PM_STATS_RSP_DATA_LEN_IDX] != sizeof(struct gsa_pm_stats) ||
	    ret != GET_PM_STATS_RSP_ARGC) {
		ret = -EINVAL;
		goto err;
	}

	ret = format_gsa_pm_stats(stat_file->text, sizeof(stat_file->text), s->bb_va);
	if (ret < 0)
		goto err;
	stat_file->text_len = ret;

	mutex_unlock(&s->bb_lock);

	return 0;

err:
	mutex_unlock(&s->bb_lock);
	kfree(stat_file);
	return ret;
}

static int gsa_cdev_pm_stat_open(struct inode *inode, struct file *filp)
{
	struct gsa_cdev *cdev = container_of(inode->i_cdev, struct gsa_cdev, cdev);
	struct gsa_dev_state *s = container_of(cdev, struct gsa_dev_state, cdev_pm_stats_node);
	struct device *dev = s->dev;

	return gsa_pm_stat_open(dev, s, filp);
}

ssize_t gsa_pm_stat_read(struct file *filp, char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct gsa_pm_stat_file *stat_file = filp->private_data;

	return simple_read_from_buffer(buf, count, ppos, stat_file->text, stat_file->text_len);
}

int gsa_pm_state_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);
	return 0;
}

const struct file_operations gsa_cdev_pm_stats_fops = {
	.open = gsa_cdev_pm_stat_open,
	.read = gsa_pm_stat_read,
	.release = gsa_pm_state_release,
};

int gsa_cdev_init(void)
{
	int ret = alloc_chrdev_region(&gsa_cdev_base_num, 0, MAX_DEVICES, KBUILD_MODNAME);

	if (ret) {
		pr_err("%s: failed (%d) to alloc chdev region\n", __func__, ret);
		return ret;
	}

	gsa_cdev_class = class_create(KBUILD_MODNAME);
	if (IS_ERR(gsa_cdev_class)) {
		ret = PTR_ERR(gsa_cdev_class);
		unregister_chrdev_region(gsa_cdev_base_num, MAX_DEVICES);
		return ret;
	}

	return 0;
}

int gsa_cdev_create(struct device *parent, struct gsa_cdev *cdev_node,
		    const char *name, const struct file_operations *fops)
{
	int ret;
	int minor;

	/* allocate minor */
	minor = idr_alloc(&gsa_cdev_devices, cdev_node, 0, MAX_DEVICES, GFP_KERNEL);
	if (minor < 0) {
		dev_err(parent, "%s: failed (%d) to get id\n", __func__, minor);
		return minor;
	}
	cdev_node->device_num = MKDEV(MAJOR(gsa_cdev_base_num), minor);

	/* Create device node */
	cdev_node->device = device_create(gsa_cdev_class, parent,
					  cdev_node->device_num,
					  NULL, "%s%d", name, 0);
	if (IS_ERR(cdev_node->device)) {
		ret = PTR_ERR(cdev_node->device);
		dev_err(parent, "%s: device_create failed: %d\n", __func__, ret);
		goto err_device_create;
	}

	/* Add character device */
	cdev_node->cdev.owner = THIS_MODULE;
	cdev_init(&cdev_node->cdev, fops);
	ret = cdev_add(&cdev_node->cdev, cdev_node->device_num, 1);
	if (ret) {
		dev_err(parent, "%s: cdev_add failed (%d)\n", __func__, ret);
		goto err_add_cdev;
	}

	pr_debug("GSA cdev created.\n");
	return 0;

err_add_cdev:
	device_destroy(gsa_cdev_class, cdev_node->device_num);
err_device_create:
	idr_remove(&gsa_cdev_devices, MINOR(cdev_node->device_num));
	return ret;
}

void gsa_cdev_remove(struct gsa_cdev *cdev_node)
{
	cdev_del(&cdev_node->cdev);
	device_destroy(gsa_cdev_class, cdev_node->device_num);
}

void gsa_cdev_exit(void)
{
	class_destroy(gsa_cdev_class);
	unregister_chrdev_region(gsa_cdev_base_num, MAX_DEVICES);
}

/*
 *  External image authentication interface
 */
int gsa_authenticate_image(struct device *gsa, dma_addr_t img_meta, phys_addr_t img_body)
{
	return gsa_send_load_img_cmd(gsa, GSA_MB_CMD_AUTH_IMG, img_meta, img_body);
}
EXPORT_SYMBOL_GPL(gsa_authenticate_image);

int gsa_authenticate_image_pq(struct device *gsa, dma_addr_t img_meta, phys_addr_t img_body,
				size_t body_len)
{
	return gsa_send_pq_load_img_cmd(gsa, img_meta, img_body, body_len);
}
EXPORT_SYMBOL_GPL(gsa_authenticate_image_pq);

/*
 *   GSA Device Attributes
 */
static ssize_t version_show(struct device *gsa, struct device_attribute *attr, char *buf)
{
	u32 req[GSA_VERSION_ARG_COUNT];
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	req[GSA_VERSION_ARG_ADDR_LO_IDX] = (u32)s->bb_da;
	req[GSA_VERSION_ARG_ADDR_HI_IDX] = (u32)(s->bb_da >> 32);
	req[GSA_VERSION_ARG_BUFF_SIZE] = 256;

	mutex_lock(&s->bb_lock);
	int ret = gsa_send_cmd(gsa, GSA_MB_CMD_GET_GSA_VERSION, req,
			       ARRAY_SIZE(req), NULL, 0);
	if (ret < 0) {
		mutex_unlock(&s->bb_lock);
		return ret;
	}

	strscpy(buf, s->bb_va, PAGE_SIZE);
	mutex_unlock(&s->bb_lock);

	return strlcat(buf, "\n", PAGE_SIZE);
}

ssize_t gsa_get_gsa_version(struct device *gsa, char *buf)
{
	return version_show(gsa, NULL, buf);
}
EXPORT_SYMBOL_GPL(gsa_get_gsa_version);

#if IS_ENABLED(CONFIG_GSA_IPC)
int gsa_set_ipc_irq_handler(struct device *gsa,
			    void (*handler)(void *),
			    void *handler_args)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	mbox_set_ipc_irq_handler(s->mb, handler, handler_args);

	return 0;
}
#else
int gsa_set_ipc_irq_handler(struct device *gsa,
			    void (*handler)(void *),
			    void *handler_args)
{
	/* Not supported */
	return -EINVAL;
}
#endif
EXPORT_SYMBOL_GPL(gsa_set_ipc_irq_handler);

static DEVICE_ATTR_RO(version);

static ssize_t rom_patch_show(struct device *gsa, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	if (s->rom_patch < 0)
		return s->rom_patch;
	return sysfs_emit(buf, "%u\n", s->rom_patch);
}
static DEVICE_ATTR_RO(rom_patch);

VISIBLE_IF_KUNIT ssize_t log_main_show(struct device *gsa,
				       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_log_read(s->log, false, buf, PAGE_SIZE);
}
EXPORT_SYMBOL_IF_KUNIT(log_main_show);
static DEVICE_ATTR_RO(log_main);

VISIBLE_IF_KUNIT ssize_t log_intermediate_show(struct device *gsa,
					       struct device_attribute *attr,
					       char *buf)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_log_read(s->log, true, buf, PAGE_SIZE);
}
EXPORT_SYMBOL_IF_KUNIT(log_intermediate_show);
static DEVICE_ATTR_RO(log_intermediate);

static struct attribute *gsa_attrs[] = {
	&dev_attr_log_main.attr,
	&dev_attr_log_intermediate.attr,
	&dev_attr_version.attr,
	&dev_attr_rom_patch.attr,
	NULL,
};
ATTRIBUTE_GROUPS(gsa);

static int32_t get_rom_version(struct device *gsa, struct gsa_dev_state *s)
{
	if (IS_ENABLED(CONFIG_GSA_PATCH_VERSION)) {
		uint32_t rsp[READ_GSA_ROM_PATCH_VERSION_ARGC] = { -1 };
		int ret = gsa_send_cmd(gsa, GSA_MB_CMD_GET_GSA_ROM_PATCH_VERSION, NULL, 0, rsp,
				       ARRAY_SIZE(rsp));
		if (ret < 0)
			return ret;
		return rsp[READ_GSA_ROM_PATCH_VERSION_IDX];
	}
	return -ENODATA;
}

static int gsa_probe(struct platform_device *pdev)
{
	int err = 0;
	struct gsa_dev_state *state = NULL;
	struct device *dev = &pdev->dev;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->dev = dev;
	mutex_init(&state->bb_lock);
	platform_set_drvdata(pdev, state);

	/*
	 * Set DMA mask and coherent to 36-bit as it is what GSA supports.
	 */
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(36));
	if (err) {
		dev_err(dev, "failed (%d) to setup dma mask\n", err);
		return err;
	}

	/* initialize mailbox */
	state->mb = gsa_mbox_init(pdev);
	if (IS_ERR(state->mb))
		return (int)PTR_ERR(state->mb);

	/* add children */
	err = devm_of_platform_populate(dev);
	if (err < 0) {
		dev_err(dev, "populate children failed (%d)\n", err);
		goto err_destroy_mbox;
	}

	/* alloc bounce buffer */
	state->bb_va = dmam_alloc_coherent(dev, GSA_PAGE_SIZE, &state->bb_da, GFP_KERNEL);
	if (!state->bb_va) {
		err = -ENOMEM;
		goto err_destroy_mbox;
	}
	state->bb_sz = GSA_PAGE_SIZE;

	/* Initialize TZ serice link to HWMGR */
	gsa_tz_chan_ctx_init(&state->aoc_srv, HWMGR_AOC_PORT, dev);
	gsa_tz_chan_ctx_init(&state->tpu_srv, HWMGR_TPU_PORT, dev);
	gsa_tz_chan_ctx_init(&state->dsp_srv, HWMGR_DSP_PORT, dev);

	state->rom_patch = get_rom_version(dev, state);
	if (state->rom_patch < 0)
		dev_err(dev, "Failed to read rom patch: %d\n", state->rom_patch);

	/* Unknown until first attempt to use mbox command */
	state->suspend_hint = GSA_SUSPEND_HINT_UNKNOWN;

	/* Initialize log if configured */
	state->log = gsa_log_init(pdev);
	if (IS_ERR(state->log)) {
		dev_err(dev, "Log init failed (%ld).  Continuing anyways\n",
			PTR_ERR(state->log));
	}

	/* Initialize character devices */
	err = gsa_cdev_create(dev, &state->cdev_node, "gsa", &gsa_cdev_fops);
	if (err != 0)
		dev_err(dev, "Failed to initialize gsa device (%d)\n", err);
	err = gsa_cdev_create(dev, &state->cdev_pm_stats_node, "gsa_pm_stat",
			       &gsa_cdev_pm_stats_fops);
	if (err != 0)
		dev_err(dev, "Failed to initialize pm_stats device (%d)\n", err);

	gsa_debugfs_init(dev);

#if !IS_ENABLED(CONFIG_GSA_PKVM)
	devm_pm_runtime_enable(&pdev->dev);
#endif
	return 0;

err_destroy_mbox:
	gsa_mbox_destroy(state->mb);
	return err;
}

static void gsa_remove(struct platform_device *pdev)
{
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	gsa_cdev_remove(&s->cdev_pm_stats_node);
	gsa_debugfs_exit(&pdev->dev);

	/* close connection to tz services */
	gsa_tz_chan_close(&s->aoc_srv);
	gsa_tz_chan_close(&s->tpu_srv);
	gsa_tz_chan_close(&s->dsp_srv);

	/* Destroy mailbox allocations */
	gsa_mbox_destroy(s->mb);
}

static int __maybe_unused gsa_pm_suspend(struct device *dev)
{
	int ret;
	struct platform_device *pdev = to_platform_device(dev);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	if (s->suspend_hint == GSA_SUSPEND_HINT_NOT_SUPPORTED)
		return 0;

	ret = gsa_send_simple_cmd(dev, GSA_MB_CMD_AP_SUSPEND_HINT);
	if (s->suspend_hint == GSA_SUSPEND_HINT_UNKNOWN) {
		if (ret < 0) {
			dev_err(dev, "Suspend hint is not supported (%d). Disabling\n", ret);
			s->suspend_hint = GSA_SUSPEND_HINT_NOT_SUPPORTED;
		} else
			s->suspend_hint = GSA_SUSPEND_HINT_SUPPORTED;
	}

	return 0;
}

static int __maybe_unused gsa_pm_resume(struct device *dev)
{
	/* Do not send a resume hint as it is only marginally useful */
	return 0;
}

const struct dev_pm_ops gsa_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(gsa_pm_suspend, gsa_pm_resume)
};

static const struct of_device_id gsa_of_match[] = {
	{ .compatible = "google,gs101-gsa-v1", },
	{},
};
MODULE_DEVICE_TABLE(of, gsa_of_match);

static struct platform_driver gsa_driver = {
	.probe = gsa_probe,
	.remove = gsa_remove,
	.driver	= {
		.name = "gsa",
		.of_match_table = gsa_of_match,
		.dev_groups = gsa_groups,
		.pm = &gsa_pm_ops,
	},
};

static int __init gsa_driver_init(void)
{
	int ret = gsa_cdev_init();

	if (ret)
		return ret;

	return platform_driver_register(&gsa_driver);
}

static void __exit gsa_driver_exit(void)
{
	pr_debug("gsa driver exit\n");

	platform_driver_unregister(&gsa_driver);
	gsa_cdev_exit();
}

/* XXX - EPROBE_DEFER would be better. */
#if IS_ENABLED(CONFIG_GSA_PKVM)
MODULE_SOFTDEP("pre: pkvm-s2mpu");
#endif

MODULE_DESCRIPTION("Google GSA core platform driver");
MODULE_LICENSE("GPL v2");
module_init(gsa_driver_init);
module_exit(gsa_driver_exit);
