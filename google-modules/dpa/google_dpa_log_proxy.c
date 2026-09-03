// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 * This file init the log proxy to forward NOA logs to the kernel.
 */
#include "services/google_dpa_services.h"
#include "pw_rpc_service_client/pw_log_service_client.nanopb.h"

#include "google_dpa_log_proxy.h"
#include "google_dpa_rpc_internal.h"

static bool log_to_console = true;
module_param(log_to_console, bool, 0444);
MODULE_PARM_DESC(log_to_console, "Forward DPA log to console");

/*
 * Log to console if @log_to_console is specified
 * Always log to textlog cdev
 * Log to tracepoint cdev if "[TP]" is found
 */
static void handle_log_string(void *context, const char *log)
{
	struct google_dpa_log_info *cur_log_info = (struct google_dpa_log_info *)context;
	struct device *dev = cur_log_info->dev;

	// Forward DPA logs to kernel logs.
	if (log_to_console)
		dev_info(dev, "%s > %s\n", cur_log_info->name, log);

	google_dpa_log_cdev_write_suffixed(&cur_log_info->textlog_cdev, log, "\n");

	if (strstr(log, "[TP]") != 0)
		google_dpa_log_cdev_write_suffixed(&cur_log_info->tracepoint_cdev, log, NULL);
}

static void pwlog_listen_on_next(PwRpcCall *call, const uint8_t *payload, size_t payload_size)
{
	PwLogServiceListenOnNext(payload, payload_size, handle_log_string, call->context);
}

static struct google_dpa_log_info log_info[] = {
	{
		.name = "ncp",
		.pwlog_listen_on_next = &pwlog_listen_on_next,
	},
	{
		.name = "nep",
		.pwlog_listen_on_next = &pwlog_listen_on_next,
	},
};

static ssize_t google_dpa_read_one_tracepoint_entry(struct file *file, char __user *buf,
						    size_t count_user, loff_t *f_pos)
{
	struct google_dpa_log_cdev *priv_data = (struct google_dpa_log_cdev *)file->private_data;
	unsigned int bytes_to_read;
	ssize_t bytes_read = 0;
	char peek_buffer[256] = { 0 };
	int num_peeked;
	int idx_peeked;

	if (mutex_lock_interruptible(&priv_data->mutex))
		return -ERESTARTSYS; /* Interrupted while trying to acquire lock */

	while (count_user > 0 && !kfifo_is_empty(&priv_data->fifo)) {
		num_peeked = kfifo_out_peek(&priv_data->fifo, peek_buffer,
					    min(sizeof(peek_buffer), count_user));
		idx_peeked = 0;
		while (idx_peeked < num_peeked && peek_buffer[idx_peeked] != '\0')
			idx_peeked++;
		// we want to consume the '\0'
		if (idx_peeked < num_peeked)
			idx_peeked++;
		if (kfifo_to_user(&priv_data->fifo, buf, idx_peeked, &bytes_to_read)) {
			dev_err(priv_data->dev, "Failed to copy data to userspace.\n");
			// 0 bytes read, an error. Otherwise treat as partial read
			if (bytes_read == 0)
				bytes_read = -EFAULT;
			break;
		} else {
			dev_dbg(priv_data->dev, "copied %d to user space.\n", idx_peeked);
			buf += bytes_to_read;
			count_user -= bytes_to_read;
		}
	}

	mutex_unlock(&priv_data->mutex);
	return bytes_read;
}

static const struct file_operations dpa_tracepoint_cdev_fops = {
	.owner = THIS_MODULE, /* Owner of the module (for module refcounting) */
	.open = google_dpa_log_cdev_open, /* Function to call on device open */
	.release = google_dpa_log_cdev_release, /* Function to call on device close */
	.read = google_dpa_read_one_tracepoint_entry, /* Function to call on device read */
	.poll = google_dpa_log_cdev_poll, /* Function to call on device poll/select */
};
static const struct file_operations dpa_textlog_cdev_fops = {
	.owner = THIS_MODULE,
	.open = google_dpa_log_cdev_open,
	.release = google_dpa_log_cdev_release,
	.read = google_dpa_log_cdev_read_to_user,
	.poll = google_dpa_log_cdev_poll, /* Function to call on device poll/select */
};

static DEFINE_MUTEX(google_dpa_log_proxy_mutex);

/*
 * This function initializes the pw_log system including
 * cdev registration is complete
 * Listening is not started yet.
 * Destroy everything on any failure.
 */
int google_dpa_log_proxy_init(struct google_dpa *dpa)
{
	int i;
	int ret = 0;
	char device_node_name[MAX_DEVICE_NAME_LEN];

	mutex_lock(&google_dpa_log_proxy_mutex);

	for (i = 0; i < ARRAY_SIZE(log_info); i++) {
		log_info[i].dev = dpa->dev;

		ret = snprintf(device_node_name, sizeof(device_node_name), "%s-%s",
			       dev_name(dpa->dev), log_info[i].name);
		if (ret >= sizeof(device_node_name) || ret < 0) {
			goto completed_parts_deinit;
		}

		ret = google_dpa_log_cdev_init(dpa, &log_info[i].tracepoint_cdev,
					       &dpa_tracepoint_cdev_fops, device_node_name);
		if (ret) {
			dev_warn(dpa->dev,
				 "Cannot init tracepoint for index %d\n"
				 "tracepoint features would not work\n",
				 i);
			goto completed_parts_deinit;
		}

		ret = snprintf(device_node_name, sizeof(device_node_name), "%s-%s-log",
			       dev_name(dpa->dev), log_info[i].name);
		if (ret >= sizeof(device_node_name) || ret < 0) {
			goto tracepoint_cdev_deinit;
		}

		ret = google_dpa_log_cdev_init(dpa, &log_info[i].textlog_cdev,
					       &dpa_textlog_cdev_fops, device_node_name);
		if (ret) {
			dev_warn(dpa->dev,
				 "Cannot init textlog for index %d\n"
				 "textlog features would not work\n",
				 i);
			goto tracepoint_cdev_deinit;
		}
	}

	mutex_unlock(&google_dpa_log_proxy_mutex);

	return 0;

tracepoint_cdev_deinit:
	google_dpa_log_cdev_deinit(dpa, &log_info[i].tracepoint_cdev);
completed_parts_deinit:
	for (i--; i >= 0; i--) {
		google_dpa_log_cdev_deinit(dpa, &log_info[i].textlog_cdev);
		google_dpa_log_cdev_deinit(dpa, &log_info[i].tracepoint_cdev);
	}

	mutex_unlock(&google_dpa_log_proxy_mutex);

	return ret;
}

/*
 * Start listening to its pw_log sources if log_proxy has been initialized
 * Initiate listening if it is not listening yet
 */
int google_dpa_log_proxy_listen(struct google_dpa *dpa)
{
	int ret = 0;

	PwRpcClient *const rpc_clients[] = { google_dpa_rpc_ncp_client(),
					     google_dpa_rpc_nep_client() };

	static_assert(ARRAY_SIZE(rpc_clients) == ARRAY_SIZE(log_info));

	mutex_lock(&google_dpa_log_proxy_mutex);

	for (int i = 0; i < ARRAY_SIZE(log_info); i++) {
		if (log_info[i].rpc_client)
			continue;

		log_info[i].rpc_client = rpc_clients[i];
		if (dpa_rpc_pwlog_service_listen(log_info[i].rpc_client, &log_info[i])) {
			dev_err(dpa->dev, "Cannot listen to %s log event!", log_info[i].name);
			log_info[i].rpc_client = NULL;
			ret = -EIO;
		}
	}

	mutex_unlock(&google_dpa_log_proxy_mutex);

	return ret;
}

/*
 * Cancel listening to its pw_log sources if log_proxy has been initialized
 */
int google_dpa_log_proxy_cancel(struct google_dpa *dpa)
{
	mutex_lock(&google_dpa_log_proxy_mutex);

	for (int i = 0; i < ARRAY_SIZE(log_info); i++) {
		if (log_info[i].rpc_client &&
		    dpa_rpc_pwlog_service_listen_cancel(log_info[i].rpc_client, &log_info[i])) {
			dev_warn(dpa->dev, "Cannot cancel %s log event listening!",
				 log_info[i].name);
		}
		log_info[i].rpc_client = NULL;
	}

	mutex_unlock(&google_dpa_log_proxy_mutex);

	return 0;
}

/*
 * De-init the pw_log system if initialized
 */
int google_dpa_log_proxy_deinit(struct google_dpa *dpa)
{
	mutex_lock(&google_dpa_log_proxy_mutex);

	for (int i = 0; i < ARRAY_SIZE(log_info); ++i) {
		google_dpa_log_cdev_deinit(dpa, &log_info[i].textlog_cdev);
		google_dpa_log_cdev_deinit(dpa, &log_info[i].tracepoint_cdev);
	}

	mutex_unlock(&google_dpa_log_proxy_mutex);

	return 0;
}
