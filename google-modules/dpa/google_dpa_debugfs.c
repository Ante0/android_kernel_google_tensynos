// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "google_dpa_internal.h"
#include "google_dpa_log_proxy.h"
#include "google_dpa_rpc_internal.h"
#include "services/google_dpa_services.h"

#define CHUNK_SIZE 256
#define MAX_DATA_SIZE (CHUNK_SIZE * 10)

struct google_dma_buffer_dbg {
	struct list_head list;
	struct google_dpa_debug *dbg;
	struct dentry *base_dir;

	unsigned int size;
	char *src_buf;
	char *dst_buf;
	dma_addr_t dma_src_addr;
	dma_addr_t dma_dst_addr;
	u32 dma_channel;
	u32 dma_burst_length;
	u32 dma_burst_size;
};

struct google_dpa_mcu_dbg {
	struct google_dpa_debug *dbg;
	PwRpcClient *client;
	u32 dma_read_word;
	u32 mcu_read_word;
	int shell_cmd_res;
};

enum transfer_state {
	IPSEC_STATE_IDLE,
	IPSEC_STATE_PENDING_DATA,
	IPSEC_STATE_IN_PROGRESS,
	IPSEC_STATE_COMPLETED_OK,
	IPSEC_STATE_ERROR,
};

struct google_dpa_ipsec_pusher_dbg {
	struct device *dev; /* Device */
	struct dentry *ipsec_base_dir; /* Base IPSec directory */
	struct work_struct pusher_work; /* Work structure for the async task */

	enum transfer_state state; /* Current transfer state */
	struct mutex mutex; /* Lock to protect state and buffer ptr */

	char *data_buf; /* Buffer to hold data from userspace */
	size_t data_len; /* Length of data in data_buf */
	size_t received_count; /* Count of bytes received into data_buf so far */
};

struct google_dpa_debug {
	struct google_dpa *dpa;
	struct dentry *base_dir;

	struct list_head dma_buffers;
	struct mutex dma_buffers_mutex;

	struct google_dpa_mcu_dbg ncp_dbg;
	struct google_dpa_mcu_dbg nep_dbg;
	int buf_count;

	struct google_dpa_ipsec_pusher_dbg *ipsec_pusher_dbg;
};

static void reset_ipsec_pusher_state(struct google_dpa_ipsec_pusher_dbg *pusher)
{
	mutex_lock(&pusher->mutex);
	kfree(pusher->data_buf);
	pusher->data_buf = NULL;
	pusher->data_len = 0;
	pusher->received_count = 0;
	mutex_unlock(&pusher->mutex);
}

static void ipsec_pusher_work_func(struct work_struct *work)
{
	struct google_dpa_ipsec_pusher_dbg *pusher =
		container_of(work, struct google_dpa_ipsec_pusher_dbg, pusher_work);
	struct device *dev = pusher->dev;
	char *current_data = pusher->data_buf;
	size_t remaining_len = pusher->data_len;
	size_t offset = 0;
	int ret;

	dev_info(dev, "Workqueue started for data transfer (total len: %zu).\n", remaining_len);

	while (remaining_len > 0) {
		size_t chunk_len = min_t(size_t, remaining_len, CHUNK_SIZE);

		ret = dpa_rpc_ipsec_service_execute(dev, google_dpa_rpc_nep_client(), current_data,
						    chunk_len, offset, pusher->data_len);
		if (ret < 0) {
			dev_err(dev,
				"Failed to chunk send or wait on response. Aborting transfer.\n");
			break;
		}

		dev_info(dev, "Chunk completion received.\n");

		if (ret) {
			dev_err(dev, "Chunk transfer response error: %d. Aborting.\n", ret);
			break;
		}

		current_data += chunk_len;
		remaining_len -= chunk_len;
		offset += chunk_len;
	}

	mutex_lock(&pusher->mutex);
	if (ret == 0 && remaining_len == 0) {
		pusher->state = IPSEC_STATE_COMPLETED_OK;
		dev_info(dev, "Data transfer completed successfully.\n");
	} else {
		pusher->state = IPSEC_STATE_ERROR;
		dev_err(dev, "Data transfer failed with code %d.\n", ret);
	}

	/* Free the buffer now that the transfer is done */
	kfree(pusher->data_buf);
	pusher->data_buf = NULL;
	pusher->data_len = 0;

	mutex_unlock(&pusher->mutex);
}

static ssize_t ipsec_push_write(struct file *file, const char __user *user_buf, size_t count,
				loff_t *ppos)
{
	struct google_dpa_ipsec_pusher_dbg *pusher =
		(struct google_dpa_ipsec_pusher_dbg *)file->private_data;
	struct device *dev = pusher->dev;
	u32 incoming_len;
	bool size_parsed = false;
	int ret = -EINVAL;

	dev_info(dev, "Received write request, count = %zu\n", count);

	/* Basic validation */
	if (count == 0 || count > (MAX_DATA_SIZE + 4)) {
		dev_err(dev, "Invalid write size %zu.\n", count);
		ret = -EINVAL;
		goto exit;
	}

	if (!google_dpa_rpc_nep_client()->rpc_channel_id) {
		ret = -EINVAL;
		goto exit;
	}

	mutex_lock(&pusher->mutex);
	if (pusher->state == IPSEC_STATE_IN_PROGRESS) {
		dev_err(dev, "Another transfer is already in progress.\n");
		ret = -EBUSY;
		goto exit_unlock;
	}

	/* Copy first 4 bytes as big endian u32, interpret as size
	 * and then allocate a reassembly buffer of that size.
	 */
	if (*ppos == 0) {
		if (count < 4) {
			dev_err(dev, "Incoming length too small %zu\n", count);
			ret = -EINVAL;
			goto exit_unlock;
		}
		if (copy_from_user(&incoming_len, user_buf, 4)) {
			dev_err(dev, "Failed to copy user bytes into driver\n");
			ret = -EFAULT;
			goto exit_unlock;
		}
		incoming_len = be32_to_cpu(incoming_len);
		if (incoming_len > MAX_DATA_SIZE) {
			dev_err(dev, "Input size too large %u\n", incoming_len);
			ret = -EINVAL;
			goto exit_unlock;
		}

		/* Always start from a new buffer */
		kfree(pusher->data_buf);

		pusher->data_buf = kmalloc(incoming_len, GFP_KERNEL);
		if (!pusher->data_buf) {
			ret = -ENOMEM;
			goto exit_unlock;
		}

		dev_info(dev, "Waiting for %u bytes\n", incoming_len);
		pusher->data_len = incoming_len;
		pusher->received_count = 0;
		pusher->state = IPSEC_STATE_PENDING_DATA;
		user_buf += 4;
		*ppos += 4;
		count -= 4;
		size_parsed = true;
	}

	if (pusher->state != IPSEC_STATE_PENDING_DATA) {
		dev_err(dev, "Unexpected state. Must be PendingData.\n");
		ret = -EINVAL;
		goto error_buffer;
	}

	if (pusher->received_count + count > pusher->data_len) {
		pusher->state = IPSEC_STATE_ERROR;
		dev_err(dev, "Received too many bytes. Only expecting %zu but got %zu.\n",
			pusher->data_len, pusher->received_count + count);
		ret = -EINVAL;
		goto error_buffer;
	}

	if (copy_from_user(pusher->data_buf + pusher->received_count, user_buf, count)) {
		dev_err(dev, "Failed to copy user data into reallocation buffer.\n");
		ret = -EFAULT;
		goto error_buffer;
	}

	pusher->received_count += count;

	/* Schedule the workqueue function to handle the transfer */
	if (pusher->received_count == pusher->data_len) {
		if (!schedule_work(&pusher->pusher_work)) {
			dev_err(dev, "Failed to queue work.\n");
			pusher->state = IPSEC_STATE_ERROR;
			ret = -EAGAIN;
			goto error_buffer;
		}
		pusher->state = IPSEC_STATE_IN_PROGRESS;
		dev_info(dev, "Data copied and work queued.\n");
	} else {
		pusher->state = IPSEC_STATE_PENDING_DATA;
		dev_info(dev, "Data copied.\n");
	}

	*ppos += count;
	ret = count;
	if (size_parsed)
		ret += 4;
	goto exit_unlock;
error_buffer:
	kfree(pusher->data_buf);
exit_unlock:
	mutex_unlock(&pusher->mutex);
exit:
	return ret;
}

/* Primitives to write for IPSec pusher data */
static const struct file_operations ipsec_pusher_data_fops = {
	.open = simple_open,
	.write = ipsec_push_write,
};

static ssize_t ipsec_pusher_read(struct file *file, char __user *user_buf, size_t count,
				 loff_t *ppos)
{
	struct google_dpa_ipsec_pusher_dbg *pusher =
		(struct google_dpa_ipsec_pusher_dbg *)file->private_data;
	enum transfer_state current_state;
	ssize_t len;
	char buf[20];

	mutex_lock(&pusher->mutex);
	current_state = pusher->state;
	mutex_unlock(&pusher->mutex);

	switch (current_state) {
	case IPSEC_STATE_IDLE:
		len = snprintf(buf, sizeof(buf), "Idle\n");
		break;
	case IPSEC_STATE_IN_PROGRESS:
		len = snprintf(buf, sizeof(buf), "InProgress\n");
		break;
	case IPSEC_STATE_COMPLETED_OK:
		len = snprintf(buf, sizeof(buf), "CompletedOK\n");
		break;
	case IPSEC_STATE_ERROR:
		len = snprintf(buf, sizeof(buf), "Error\n");
		break;
	case IPSEC_STATE_PENDING_DATA:
		len = snprintf(buf, sizeof(buf), "PendingData\n");
		break;
	default:
		len = snprintf(buf, sizeof(buf), "Unknown State (%d)\n", current_state);
		break;
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

/* Primitives to read for IPSec pusher status */
static const struct file_operations ipsec_pusher_status_fops = {
	.open = simple_open,
	.read = ipsec_pusher_read,
};

static ssize_t dpa_shell_cmd_write(struct file *file, const char __user *buffer, size_t count,
				   loff_t *pos)
{
	struct google_dpa_mcu_dbg *mcu_dbg = file->private_data;
	struct device *dev = mcu_dbg->dbg->dpa->dev;
	char *cmd_buf;
	size_t cmd_end;
	int ret;

	if (*pos)
		return -EINVAL;

	cmd_buf = memdup_user_nul(buffer, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	cmd_end = strcspn(cmd_buf, "\r\n\0");
	cmd_buf[cmd_end] = '\0';

	ret = dpa_rpc_shell_cmd_sync(dev, mcu_dbg->client, cmd_buf);
	kfree(cmd_buf);

	if (!ret)
		return count;

	return ret;
}

static const struct file_operations dpa_shell_cmd_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dpa_shell_cmd_write,
};

/* Ask the remote core to read a word of data from the given address */
static int dma_read_word_request(void *data, u64 val)
{
	struct google_dpa_mcu_dbg *mcu_dbg = (struct google_dpa_mcu_dbg *)data;

	return dpa_rpc_dma_service_read_word_sync(mcu_dbg->dbg->dpa->dev, mcu_dbg->client, val,
						  &mcu_dbg->dma_read_word);
}

/* Return the last value that we asked the remote core to read */
static int dma_read_word_show(void *data, u64 *val)
{
	struct google_dpa_mcu_dbg *mcu_dbg = (struct google_dpa_mcu_dbg *)data;

	*val = mcu_dbg->dma_read_word;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(dma_read_fops, dma_read_word_show, dma_read_word_request, "%llu\n");

/* Ask the remote core to read a word of data from the given address */
static int mcu_read_word_request(void *data, u64 val)
{
	struct google_dpa_mcu_dbg *mcu_dbg = (struct google_dpa_mcu_dbg *)data;

	return dpa_rpc_dma_service_read_word_sync(mcu_dbg->dbg->dpa->dev, mcu_dbg->client, val,
						  &mcu_dbg->mcu_read_word);
}

/* Return the last value that we asked the remote core to read */
static int mcu_read_word_show(void *data, u64 *val)
{
	struct google_dpa_mcu_dbg *mcu_dbg = (struct google_dpa_mcu_dbg *)data;

	*val = mcu_dbg->mcu_read_word;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(mcu_read_fops, mcu_read_word_show, mcu_read_word_request, "%llu\n");

static ssize_t src_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct google_dma_buffer_dbg *buf_dbg = (struct google_dma_buffer_dbg *)file->private_data;

	if (!buf_dbg->size)
		return -EIO;

	if (*ppos >= buf_dbg->size)
		return -EINVAL;

	return simple_read_from_buffer(user_buf, count, ppos, buf_dbg->src_buf, buf_dbg->size);
}

static ssize_t src_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct google_dma_buffer_dbg *buf_dbg = (struct google_dma_buffer_dbg *)file->private_data;

	if (!buf_dbg->size)
		return -EIO;

	if (*ppos >= buf_dbg->size)
		return -EINVAL;

	return simple_write_to_buffer(buf_dbg->src_buf, buf_dbg->size, ppos, user_buf, count);
}

static loff_t buf_llseek(struct file *file, loff_t offset, int whence)
{
	struct google_dma_buffer_dbg *buf_dbg = (struct google_dma_buffer_dbg *)file->private_data;

	return fixed_size_llseek(file, offset, whence, buf_dbg->size);
}

/* Primitives to read/write the src DRAM buffer */
static const struct file_operations src_fops = {
	.open = simple_open,
	.write = src_write,
	.read = src_read,
	.llseek = buf_llseek,
};

static ssize_t dst_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct google_dma_buffer_dbg *buf_dbg = (struct google_dma_buffer_dbg *)file->private_data;

	if (!buf_dbg->size)
		return -EIO;

	if (*ppos >= buf_dbg->size)
		return -EINVAL;

	return simple_read_from_buffer(user_buf, count, ppos, buf_dbg->dst_buf, buf_dbg->size);
}

static ssize_t dst_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct google_dma_buffer_dbg *buf_dbg = (struct google_dma_buffer_dbg *)file->private_data;

	if (!buf_dbg->size)
		return -EIO;

	if (*ppos >= buf_dbg->size)
		return -EINVAL;

	return simple_write_to_buffer(buf_dbg->dst_buf, buf_dbg->size, ppos, user_buf, count);
}

/* Primitives to read/write the dst DRAM buffer */
static const struct file_operations dst_fops = {
	.open = simple_open,
	.write = dst_write,
	.read = dst_read,
	.llseek = buf_llseek,
};

/* Request the NCP to perform a memcpy from the src buffer to the dst buffer */
static int ncp_memcpy_write(void *data, u64 unused)
{
	struct google_dma_buffer_dbg *buf_dbg = data;

	return dpa_rpc_dma_service_copy_data_sync(buf_dbg->dbg->dpa->dev,
						  google_dpa_rpc_ncp_client(),
						  buf_dbg->dma_dst_addr, buf_dbg->dma_src_addr,
						  buf_dbg->size);
}
DEFINE_DEBUGFS_ATTRIBUTE(ncp_memcpy_fops, NULL, ncp_memcpy_write, "%llu\n");

/* Request the NCP to perform a DMA copy from the src buffer to the dst buffer */
static int ncp_dma_copy_write(void *data, u64 unused)
{
	struct google_dma_buffer_dbg *buf_dbg = data;

	return dpa_rpc_dma_service_dma_copy_data_sync(buf_dbg->dbg->dpa->dev,
						      google_dpa_rpc_ncp_client(),
						      buf_dbg->dma_dst_addr, buf_dbg->dma_src_addr,
						      buf_dbg->size, buf_dbg->dma_channel,
						      buf_dbg->dma_burst_size,
						      buf_dbg->dma_burst_length);
}
DEFINE_DEBUGFS_ATTRIBUTE(ncp_dma_copy_fops, NULL, ncp_dma_copy_write, "%llu\n");

/*
 * alloc_write - Allocate a pair of test buffers
 * @val: Size (in bytes) of the buffers to allocate
 *
 * Allocate a pair of test buffers and create a debugfs sub-directory tied to this buffer pair.
 * The buffer contents can be manipulated and inspected through debugfs files created under this
 * new subdirectory.
 *
 * Test buffers and their debugfs directories can be freed by writing to the dma_free_all file.
 */
static int alloc_write(void *data, u64 val)
{
	struct google_dma_buffer_dbg *buf_dbg;
	struct google_dpa_debug *dbg = data;
	struct device *dev = dbg->dpa->dev;
	struct dentry *buf_dir;
	char dirname[32];
	int ret = 0;

	if (!val)
		return -EINVAL;

	buf_dbg = kzalloc(sizeof(*buf_dbg), GFP_KERNEL);
	if (!buf_dbg)
		return -ENOMEM;

	buf_dbg->dbg = dbg;
	buf_dbg->src_buf = dma_alloc_coherent(dev, val, &buf_dbg->dma_src_addr, GFP_KERNEL);
	if (!buf_dbg->src_buf) {
		ret = -ENOMEM;
		goto err;
	}
	buf_dbg->dst_buf = dma_alloc_coherent(dev, val, &buf_dbg->dma_dst_addr, GFP_KERNEL);
	if (!buf_dbg->dst_buf) {
		ret = -ENOMEM;
		goto err;
	}
	buf_dbg->size = val;
	buf_dbg->dma_channel = 0;
	buf_dbg->dma_burst_size = 1;
	buf_dbg->dma_burst_length = 1;

	mutex_lock(&dbg->dma_buffers_mutex);
	snprintf(dirname, sizeof(dirname), "dma_buffer_%d", dbg->buf_count);
	buf_dir = debugfs_create_dir(dirname, dbg->base_dir);
	if (!buf_dir) {
		dev_err(dev, "Failed to create directory\n");
		ret = -ENOMEM;
		mutex_unlock(&dbg->dma_buffers_mutex);
		goto err;
	}

	buf_dbg->base_dir = buf_dir;
	/* Primary interface */
	debugfs_create_file_size("src_buf", 0600, buf_dir, buf_dbg, &src_fops, val);
	debugfs_create_file_size("dst_buf", 0600, buf_dir, buf_dbg, &dst_fops, val);
	debugfs_create_file("ncp_memcpy", 0200, buf_dir, buf_dbg, &ncp_memcpy_fops);
	debugfs_create_file("ncp_dmacpy", 0200, buf_dir, buf_dbg, &ncp_dma_copy_fops);

	/* config parameters */
	debugfs_create_u32("dma_channel", 0600, buf_dir, &buf_dbg->dma_channel);
	debugfs_create_u32("dma_burst_size", 0600, buf_dir, &buf_dbg->dma_burst_size);
	debugfs_create_u32("dma_burst_length", 0600, buf_dir, &buf_dbg->dma_burst_length);

	/* For debug */
	debugfs_create_u32("size", 0400, buf_dir, &buf_dbg->size);
	debugfs_create_u64("src_addr", 0600, buf_dir, (u64 *)&buf_dbg->src_buf);
	debugfs_create_u64("dst_addr", 0600, buf_dir, (u64 *)&buf_dbg->dst_buf);
	debugfs_create_u64("dma_src_addr", 0600, buf_dir, &buf_dbg->dma_src_addr);
	debugfs_create_u64("dma_dst_addr", 0600, buf_dir, &buf_dbg->dma_dst_addr);

	list_add(&buf_dbg->list, &dbg->dma_buffers);
	dbg->buf_count += 1;
	mutex_unlock(&dbg->dma_buffers_mutex);

	return 0;
err:
	if (buf_dbg->src_buf)
		dma_free_coherent(buf_dbg->dbg->dpa->dev, buf_dbg->size, buf_dbg->src_buf,
				  buf_dbg->dma_src_addr);
	if (buf_dbg->dst_buf)
		dma_free_coherent(buf_dbg->dbg->dpa->dev, buf_dbg->size, buf_dbg->dst_buf,
				  buf_dbg->dma_dst_addr);
	kfree(buf_dbg);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(alloc_fops, NULL, alloc_write, "%llu\n");

static void free_buffers(struct google_dpa_debug *dbg)
{
	struct google_dma_buffer_dbg *buf_dbg;
	struct google_dma_buffer_dbg *tmp;
	struct device *dev = dbg->dpa->dev;

	mutex_lock(&dbg->dma_buffers_mutex);
	list_for_each_entry_safe(buf_dbg, tmp, &dbg->dma_buffers, list) {
		/*
		 * debugfs_remove will lock while any files are open. Calling it first ensures
		 * that the resources we are freeing are no longer used.
		 */
		debugfs_remove_recursive(buf_dbg->base_dir);
		dma_free_coherent(dev, buf_dbg->size, buf_dbg->src_buf, buf_dbg->dma_src_addr);
		dma_free_coherent(dev, buf_dbg->size, buf_dbg->dst_buf, buf_dbg->dma_dst_addr);
		list_del(&buf_dbg->list);
		kfree(buf_dbg);
	}
	mutex_unlock(&dbg->dma_buffers_mutex);
	dbg->buf_count = 0;
}

/* Free all previously allocated debug buffers */
static int free_write(void *data, u64 unused)
{
	struct google_dpa_debug *dbg = data;

	free_buffers(dbg);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(free_fops, NULL, free_write, "%llu\n");

#define LOG_FORWARD_ENABLE "enable"
#define LOG_FORWARD_DISABLE "disable"
static ssize_t dpa_log_forward_write(struct file *file, const char __user *buffer, size_t count,
				   loff_t *pos)
{
	struct google_dpa_debug *dbg = file->private_data;
	struct google_dpa *dpa = dbg->dpa;
	struct device *dev = dpa->dev;
	bool enable = false;
	char *cmd_buf;
	size_t cmd_end;
	int ret = 0;

	if (*pos)
		return -EINVAL;

	cmd_buf = memdup_user_nul(buffer, count);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);

	cmd_end = strcspn(cmd_buf, "\r\n\0");
	cmd_buf[cmd_end] = '\0';

	if (0 == strncmp(cmd_buf, LOG_FORWARD_ENABLE, strlen(LOG_FORWARD_ENABLE))) {
		enable = true;
	} else if (0 == strncmp(cmd_buf, LOG_FORWARD_DISABLE, strlen(LOG_FORWARD_DISABLE))) {
		enable = false;
	} else {
		dev_err(dev, "Valid input: %s or %s.", LOG_FORWARD_ENABLE, LOG_FORWARD_DISABLE);
		ret = -EINVAL;
		goto out;
	}

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret)
		goto out;

	dpa->use_log_proxy = enable;

	if (dpa->fw_state == GOOGLE_DPA_FW_RUNNING) {
		if (enable)
			ret = google_dpa_log_proxy_listen(dpa);
		else
			ret = google_dpa_log_proxy_cancel(dpa);
	}

	mutex_unlock(&dpa->mutex);

out:
	kfree(cmd_buf);

	if (!ret)
		return count;

	return ret;
}

static const struct file_operations dpa_log_forward_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dpa_log_forward_write,
};


static int google_dpa_init_mcu_debugfs(struct google_dpa *google_dpa, struct google_dpa_debug *dbg,
				       struct google_dpa_mcu_dbg *mcu_dbg, char *name,
				       PwRpcClient *client)
{
	struct dentry *dir;

	dir = debugfs_create_dir(name, dbg->base_dir);
	if (!dir) {
		dev_err(google_dpa->dev, "Failed to create directory\n");
		return -ENOMEM;
	}

	mcu_dbg->client = client;
	mcu_dbg->dbg = dbg;
	debugfs_create_file("dma_read_word", 0600, dir, mcu_dbg, &dma_read_fops);
	debugfs_create_file("mcu_read_word", 0600, dir, mcu_dbg, &mcu_read_fops);
	debugfs_create_file("shell_cmd", 0400, dir, mcu_dbg, &dpa_shell_cmd_fops);

	return 0;
}

static int google_dpa_ipsec_init(struct google_dpa_debug *dbg, struct dentry *ipsec_base_dir)
{
	struct google_dpa_ipsec_pusher_dbg *pusher;
	struct dentry *pusher_base_dir;

	/* Initialize data */
	pusher = devm_kzalloc(dbg->dpa->dev, sizeof(*pusher), GFP_KERNEL);
	if (!pusher)
		return -ENOMEM;

	mutex_init(&pusher->mutex);
	pusher->state = IPSEC_STATE_IDLE;
	pusher->dev = dbg->dpa->dev;
	pusher->ipsec_base_dir = ipsec_base_dir;

	/* Initialize the work structure */
	INIT_WORK(&pusher->pusher_work, ipsec_pusher_work_func);

	/* Create pusher folder */
	pusher_base_dir = debugfs_create_dir("pusher", ipsec_base_dir);
	if (!pusher_base_dir) {
		dev_err(dbg->dpa->dev, "Failed to create IPSec pusher directory\n");
		return -ENOMEM;
	}
	/* Data file */
	debugfs_create_file("data", 0220, pusher_base_dir, pusher, &ipsec_pusher_data_fops);

	/* Status file */
	debugfs_create_file("status", 0444, pusher_base_dir, pusher, &ipsec_pusher_status_fops);

	dbg->ipsec_pusher_dbg = pusher;

	return 0;
}

int google_dpa_init_debugfs(struct google_dpa *google_dpa)
{
	struct device *dev = google_dpa->dev;
	struct google_dpa_debug *dbg;
	struct dentry *ipsec_base_dir;
	int err;

	dbg = devm_kzalloc(dev, sizeof(*dbg), GFP_KERNEL);
	if (!dbg)
		return -ENOMEM;

	dbg->dpa = google_dpa;
	dbg->base_dir = debugfs_create_dir(dev_name(dev), NULL);
	INIT_LIST_HEAD(&dbg->dma_buffers);
	mutex_init(&dbg->dma_buffers_mutex);

	debugfs_create_file("log_forward", 0200, dbg->base_dir, dbg, &dpa_log_forward_fops);

	/* dma buffer interface */
	debugfs_create_file("dma_alloc", 0200, dbg->base_dir, dbg, &alloc_fops);
	debugfs_create_file("dma_free_all", 0200, dbg->base_dir, dbg, &free_fops);

	/* simple dma_read interface */
	err = google_dpa_init_mcu_debugfs(google_dpa, dbg, &dbg->ncp_dbg, "ncp",
					  google_dpa_rpc_ncp_client());
	if (err)
		return err;

	err = google_dpa_init_mcu_debugfs(google_dpa, dbg, &dbg->nep_dbg, "nep",
					  google_dpa_rpc_nep_client());
	if (err)
		return err;

	google_dpa->debugfs = dbg;

	/* IPSec testing interface */
	ipsec_base_dir = debugfs_create_dir("ipsec", dbg->base_dir);
	if (!ipsec_base_dir) {
		dev_err(google_dpa->dev, "Failed to create IPSec directory\n");
		return -ENOMEM;
	}

	err = google_dpa_ipsec_init(dbg, ipsec_base_dir);
	if (err)
		return err;

	return 0;
}

void google_dpa_exit_debugfs(struct google_dpa *dpa)
{
	struct google_dpa_debug *dbg = (struct google_dpa_debug *)dpa->debugfs;

	free_buffers(dbg);
	reset_ipsec_pusher_state(dbg->ipsec_pusher_dbg);
	debugfs_remove_recursive(dbg->base_dir);
}
