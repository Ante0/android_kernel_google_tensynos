// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019-2025 Google LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/circ_buf.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/sched/clock.h>
#include <linux/seq_file.h>
#include <linux/suspend.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <misc/logbuffer.h>

#include <uapi/linux/time.h>

#define LOGBUFFER_SIZE_DEFAULT		65536
#define LOGBUFFER_ENTRY_SIZE		256
#define LOGBUFFER_ID_LENGTH		50

struct logbuffer {
	struct circ_buf log_circ_buf;
	size_t buf_size;
	char log[LOGBUFFER_ENTRY_SIZE];
	u64 ts_nsec;
	unsigned long rem_nsec;

	spinlock_t logbuffer_lock;	/* protect from multiple _log() */
	char id[LOGBUFFER_ID_LENGTH];
	struct miscdevice misc;
	char name[50];
	uint suspend_count;
};

/* Driver suspended count. */
static uint driver_suspended_count;
/* Log index for logbuffer_logk */
static atomic_t log_index = ATOMIC_INIT(0);

static void __circ_buf_write(struct circ_buf *cb, size_t buf_size, const void *src, size_t count)
{
	int space_to_end = CIRC_SPACE_TO_END(cb->head, cb->tail, buf_size);
	int part1_len = min_t(int, count, space_to_end);

	memcpy(cb->buf + cb->head, src, part1_len);
	if (count > part1_len)
		memcpy(cb->buf, src + part1_len, count - part1_len);

	cb->head = (cb->head + count) & (buf_size - 1);
}

/* The log record is formatted as (length prefix)(log data) */
static void __logbuffer_log(struct logbuffer *instance, u8 log_length)
{
	struct circ_buf *buf = &instance->log_circ_buf;
	size_t total;

	/* Add the size of the length prefix */
	total = sizeof(log_length) + log_length;

	/* Discard oldest entries if there is not enough space */
	while (CIRC_SPACE(buf->head, buf->tail, instance->buf_size) < total) {
		u8 old_log_len;

		/* If buffer is empty, break to avoid infinite loop */
		if (CIRC_CNT(buf->head, buf->tail, instance->buf_size) == 0)
			break;

		old_log_len = buf->buf[buf->tail];
		buf->tail = (buf->tail + old_log_len + sizeof(old_log_len)) &
			    (instance->buf_size - 1);
	}

	/* Write the length prefix and log content to the buffer */
	__circ_buf_write(&instance->log_circ_buf, instance->buf_size, &log_length,
			 sizeof(log_length));
	__circ_buf_write(&instance->log_circ_buf, instance->buf_size, instance->log, log_length);
}

/* Format the kernel time and rtc time to instance->log. Return the length of the copied buffer. */
static u8 create_utc_log(struct logbuffer *instance)
{
	struct timespec64 ts;
	struct rtc_time tm;
	u8 log_length;

	ktime_get_real_ts64(&ts);
	rtc_time64_to_tm(ts.tv_sec, &tm);
	log_length = scnprintf(instance->log, LOGBUFFER_ENTRY_SIZE,
			       "[%5lu.%06lu] %4d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
			       (unsigned long)instance->ts_nsec, instance->rem_nsec / 1000,
			       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			       tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
	return log_length;
}

void logbuffer_vlog(struct logbuffer *instance, const char *fmt, va_list args)
{
	char tmpbuffer[LOGBUFFER_ENTRY_SIZE];
	unsigned long flags;
	u8 log_length;

	if (!instance)
		return;

	spin_lock_irqsave(&instance->logbuffer_lock, flags);

	instance->ts_nsec = local_clock();
	instance->rem_nsec = do_div(instance->ts_nsec, 1000000000);

	/* Print UTC at the start of the buffer */
	if (instance->log_circ_buf.head == instance->log_circ_buf.tail) {
		log_length = create_utc_log(instance);
		__logbuffer_log(instance, log_length);
	/* Print UTC when logging after suspend */
	} else if (driver_suspended_count != instance->suspend_count) {
		log_length = create_utc_log(instance);
		__logbuffer_log(instance, log_length);
		instance->suspend_count = driver_suspended_count;
	}

	/*
	 * Empty log msgs are passed from TCPM to log RTC. The RTC is printed
	 * if thats the first message printed after resume.
	 */
	if (!fmt)
		goto abort;

	scnprintf(tmpbuffer, LOGBUFFER_ENTRY_SIZE, "[%5lu.%06lu] %s\n",
		  (unsigned long)instance->ts_nsec, instance->rem_nsec / 1000, fmt);
	log_length = vscnprintf(instance->log, sizeof(instance->log), tmpbuffer, args);
	/*
	 * If the source string exceeds LOGBUFFER_ENTRY_SIZE and the trailing '\n' is discarded,
	 * overwrite the end of the destination string with '\n'.
	 */
	instance->log[log_length - 1] = '\n';

	__logbuffer_log(instance, log_length);
abort:
	spin_unlock_irqrestore(&instance->logbuffer_lock, flags);
}
EXPORT_SYMBOL_GPL(logbuffer_vlog);

void logbuffer_log(struct logbuffer *instance, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	logbuffer_vlog(instance, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(logbuffer_log);

static unsigned int logbuffer_indexed_vlog(struct logbuffer *instance, int loglevel,
					   const char *fmt, va_list args)
{
	char log[LOGBUFFER_ENTRY_SIZE];
	unsigned int index;

	index = atomic_inc_return(&log_index);

	scnprintf(log, LOGBUFFER_ENTRY_SIZE, "[%5u] %s", index, fmt);
	logbuffer_vlog(instance, log, args);

	return index;
}

void logbuffer_logk(struct logbuffer *instance, int loglevel, const char *fmt, ...)
{
	char log[LOGBUFFER_ENTRY_SIZE];
	unsigned int index;
	va_list args;

	if (!fmt || !instance)
		return;

	va_start(args, fmt);
	index = logbuffer_indexed_vlog(instance, loglevel, fmt, args);
	if (IS_ENABLED(CONFIG_PRINTK)) {
		scnprintf(log, LOGBUFFER_ENTRY_SIZE, "%s: [%5u] %s\n", instance->name, index, fmt);
		vprintk_emit(0, loglevel, NULL, log, args);
	}
	va_end(args);
}
EXPORT_SYMBOL_GPL(logbuffer_logk);

int dev_logbuffer_logk(struct device *dev, struct logbuffer *instance, int loglevel,
		       const char *fmt, ...)
{
	char log[LOGBUFFER_ENTRY_SIZE];
	unsigned int index;
	va_list args;
	int ret = 0;

	if (!dev || !instance)
		return -ENODEV;

	if (!fmt)
		return 0;

	va_start(args, fmt);
	index = logbuffer_indexed_vlog(instance, loglevel, fmt, args);
	if (IS_ENABLED(CONFIG_PRINTK)) {
		scnprintf(log, LOGBUFFER_ENTRY_SIZE, "%s %s: [%5u] %s\n", dev_driver_string(dev),
			  dev_name(dev), index, fmt);
		ret = dev_vprintk_emit(loglevel, dev, log, args);
	}
	va_end(args);

	return ret;
}
EXPORT_SYMBOL_GPL(dev_logbuffer_logk);

static int logbuffer_seq_show(struct seq_file *s, void *v)
{
	struct logbuffer *instance = (struct logbuffer *)s->private;
	int data_start_idx;
	int data_part1_len;
	u8 log_len;
	int tail;

	spin_lock_irq(&instance->logbuffer_lock);

	tail = instance->log_circ_buf.tail;

	while (tail != instance->log_circ_buf.head) {
		/* Get the length from the prefix at the current tail position */
		log_len = instance->log_circ_buf.buf[tail];

		/* Determine the starting position of the actual log data */
		data_start_idx = (tail + sizeof(log_len)) & (instance->buf_size - 1);

		/* Write the log data (which might wrap around the buffer) */
		data_part1_len = min_t(int, (int)log_len, instance->buf_size - data_start_idx);
		seq_write(s, instance->log_circ_buf.buf + data_start_idx, data_part1_len);
		if (log_len > data_part1_len)
			seq_write(s, instance->log_circ_buf.buf, log_len - data_part1_len);

		/* Advance tail to the beginning of the next log */
		tail = (tail + log_len + sizeof(log_len)) & (instance->buf_size - 1);
	}

	spin_unlock_irq(&instance->logbuffer_lock);
	return 0;
}

static int logbuffer_dev_open(struct inode *inode, struct file *file)
{
	struct logbuffer *instance =
		container_of(file->private_data, struct logbuffer, misc);

	inode->i_private = instance;
	file->private_data = NULL;
	return single_open(file, logbuffer_seq_show, inode->i_private);
}

static const struct file_operations logbuffer_dev_operations = {
	.owner = THIS_MODULE,
	.open = logbuffer_dev_open,
	.read = seq_read,
	.release = single_release,
};

/**
 * logbuffer_register_size() - Register a logbuffer instance with a specific buffer size.
 * @name: The name of the logbuffer instance.
 * @buf_size: The size of the internal buffer. Must be a power of 2.
 *
 * This function allocates and initializes a new logbuffer instance with the
 * given name and buffer size. The buffer size must be a power of 2 to
 * ensure correct operation of the circular buffer logic.
 *
 * Return: A pointer to the allocated logbuffer instance on success,
 *         or an ERR_PTR() encoded error number on failure (e.g., -ENOMEM).
 */
struct logbuffer *logbuffer_register_size(const char *name, size_t buf_size)
{
	struct logbuffer *instance;
	int ret;

	if (!is_power_of_2(buf_size))
		return ERR_PTR(-EINVAL);

	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return ERR_PTR(-ENOMEM);

	instance->buf_size = buf_size;

	instance->log_circ_buf.buf = vzalloc(instance->buf_size);
	if (!instance->log_circ_buf.buf)
		goto free_instance;

	strscpy(instance->name, "logbuffer_", sizeof(instance->name));
	strlcat(instance->name, name, sizeof(instance->name));
	instance->misc.minor = MISC_DYNAMIC_MINOR;
	instance->misc.name = instance->name;
	instance->misc.fops = &logbuffer_dev_operations;

	ret = misc_register(&instance->misc);
	if (ret) {
		pr_err("Logbuffer error while doing misc_register ret=%d\n", ret);
		goto free_buffer;
	}

	strscpy(instance->id, name, sizeof(instance->id));

	spin_lock_init(&instance->logbuffer_lock);

	pr_info("id:%s registered, buffer size: %zu\n", name, instance->buf_size);
	return instance;

free_buffer:
	vfree(instance->log_circ_buf.buf);
free_instance:
	kfree(instance);

	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(logbuffer_register_size);

/**
 * logbuffer_register() - Register a logbuffer instance with default size.
 * @name: The name of the logbuffer instance.
 *
 * This function allocates and initializes a new logbuffer instance with the
 * given name, using the default buffer size (LOGBUFFER_SIZE_DEFAULT).
 * It calls logbuffer_register_size() internally.
 *
 * Return: A pointer to the allocated logbuffer instance on success,
 *         or an ERR_PTR() encoded error number on failure.
 */
struct logbuffer *logbuffer_register(const char *name)
{
	return logbuffer_register_size(name, (size_t)LOGBUFFER_SIZE_DEFAULT);
}
EXPORT_SYMBOL_GPL(logbuffer_register);

void logbuffer_unregister(struct logbuffer *instance)
{
	if (!instance)
		return;

	misc_deregister(&instance->misc);

	vfree(instance->log_circ_buf.buf);
	pr_info("id:%s unregistered\n", instance->id);
	kfree(instance);
}
EXPORT_SYMBOL_GPL(logbuffer_unregister);

static int logbuffer_suspend(void)
{
	driver_suspended_count += 1;
	return 0;
}

static struct syscore_ops logbuffer_ops = {
	.suspend        = logbuffer_suspend,
};

static int __init logbuffer_dev_init(void)
{
	register_syscore_ops(&logbuffer_ops);
	driver_suspended_count = 0;
	return 0;
}

static void logbuffer_dev_exit(void)
{
	unregister_syscore_ops(&logbuffer_ops);
}
early_initcall(logbuffer_dev_init);
module_exit(logbuffer_dev_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google BMS logbuffer");
