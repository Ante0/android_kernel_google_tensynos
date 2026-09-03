// SPDX-License-Identifier: GPL-2.0
/*
 * GTI Simulation for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/poll.h>

#include "uapi/input/touch_offload.h"
#include "gti_sim.h"

#undef pr_fmt
#define pr_fmt(fmt) "touch_sim: " fmt

#define emulator_has_function(emulator, function) \
	((emulator) && ((emulator)->ops) && ((emulator)->ops->function))

static bool touch_sim_has_error(struct touch_sim *sim);

void touch_sim_clean_fifo(struct touch_sim *sim)
{
	kfifo_reset(&sim->fifo);
	wake_up_interruptible(&sim->event_wait_queue);
}

int touch_sim_on_frame_processed(struct touch_sim *sim, ktime_t timestamp)
{
	u32 ret = kfifo_out(&sim->fifo, sim->temp_frame, sim->frame_size);

	if (ret != sim->frame_size) {
		pr_err("fifo out failed %u, %u", ret, sim->frame_size);
		return -EINVAL;
	}

	if (sim->pop_data_cb == NULL) {
		pr_err("Invalid pop data callback function");
		return -EINVAL;
	}

	s64 diff_ns = ktime_to_ns(ktime_sub(ktime_get(), timestamp));

	atomic_inc(&sim->reported_frame_count);
	sim->total_diff_ns = sim->total_diff_ns + diff_ns;
	sim->max_diff_ns = max(sim->max_diff_ns, diff_ns);
	sim->min_diff_ns = min(sim->min_diff_ns, diff_ns);

	wake_up_interruptible(&sim->event_wait_queue);

	return sim->pop_data_cb(sim->private_data, sim->temp_frame,
				sim->frame_size, timestamp);
}

int touch_sim_peak_and_load_frame(struct touch_sim *sim)
{
	size_t ret = 0;
	u32 frame_size;

	if (kfifo_is_empty(&sim->fifo))
		return -EBUSY;

	if (kfifo_out_peek(&sim->fifo, &frame_size, sizeof(frame_size)) !=
	    sizeof(frame_size)) {
		pr_warn("peek failed");
		return -EIO;
	}

	if (frame_size != sim->frame_size) {
		pr_err("frame size not match first frame_size(%u vs %u)",
		       frame_size, sim->frame_size);
		return -EIO;
	}

	if (frame_size > kfifo_len(&sim->fifo)) {
		pr_warn("size not enough frame_size(%u) kfifo_len:%u",
			frame_size, kfifo_len(&sim->fifo));
		return -EBUSY;
	}

	ret = kfifo_out_peek(&sim->fifo, sim->temp_frame_header,
			     sizeof(struct TouchOffloadFrameHeader));
	if (ret != sizeof(struct TouchOffloadFrameHeader)) {
		pr_warn("fifo out faile");
		return -EIO;
	}
	return 0;
}

static ssize_t touch_sim_add_frame_data(struct touch_sim *sim,
					struct file *file, u8 *buf,
					size_t count)
{
	ssize_t handle_count = 0;

	if (sim == NULL || buf == NULL)
		return -EINVAL;

	while (handle_count < count) {
		if (file->f_flags & O_NONBLOCK) {
			if (kfifo_is_full(&sim->fifo))
				return -EAGAIN;
		} else {
			if (wait_event_interruptible(
				    sim->event_wait_queue,
				    !kfifo_is_full(&sim->fifo))) {
				return -ERESTARTSYS;
			}
		}

		if (touch_sim_has_error(sim)) {
			pr_warn("write: touch sim has error");
			return -EINVAL;
		}

		handle_count += kfifo_in(&sim->fifo, buf + handle_count,
					 count - handle_count);
	}
	return handle_count;
}

static bool touch_sim_is_ready(struct touch_sim *sim)
{
	if (sim == NULL || !kfifo_initialized(&sim->fifo) ||
	    sim->temp_frame == NULL || sim->temp_frame_header == NULL ||
	    !emulator_has_function(sim->emulator, is_ready) ||
	    !sim->emulator->ops->is_ready(sim->emulator)) {
		return false;
	}

	return true;
}

static bool touch_sim_has_error(struct touch_sim *sim)
{
	if (emulator_has_function(sim->emulator, has_error))
		return sim->emulator->ops->has_error(sim->emulator);

	return true;
}

static int touch_sim_start(struct touch_sim *sim, u32 frame_size)
{
	int ret = 0;

	if (sim == NULL)
		return -EINVAL;

	if (sim->pop_data_cb == NULL || sim->private_data == NULL)
		return -EINVAL;

	if (frame_size < 600 || frame_size > 7000)
		return -EINVAL;

	pr_info("create fifo queue %u", frame_size * 8);
	ret = kfifo_alloc(&sim->fifo, frame_size * 8, GFP_KERNEL);
	if (ret != 0)
		return -ENOMEM;

	sim->temp_frame_header =
		kzalloc(sizeof(struct TouchOffloadFrameHeader), GFP_KERNEL);
	if (sim->temp_frame_header == NULL)
		return -ENOMEM;

	sim->temp_frame = kzalloc(frame_size, GFP_KERNEL);
	if (sim->temp_frame == NULL)
		return -ENOMEM;

	sim->frame_size = frame_size;

	sim->emulator = (struct emulator *)&sw_emulator;
	sim->emulator->parent = sim;
	if (!emulator_has_function(sim->emulator, start))
		return -EINVAL;

	if (sim->emulator->ops->start(sim->emulator))
		return -EINVAL;

	return 0;
}

void touch_sim_stop(struct touch_sim *sim)
{
	if (sim == NULL)
		return;

	if (emulator_has_function(sim->emulator, stop))
		sim->emulator->ops->stop(sim->emulator);

	if (sim->temp_frame_header != NULL) {
		kfree(sim->temp_frame_header);
		sim->temp_frame_header = NULL;
	}

	if (sim->temp_frame != NULL) {
		kfree(sim->temp_frame);
		sim->temp_frame = NULL;
	}

	kfifo_free(&sim->fifo);
}

/*-----------------------------------------------------------------------------
 * touch_sim: file operations implement
 */

static ssize_t touch_sim_read(struct file *file, char __user *user_buf,
			      size_t count, loff_t *offset)
{
	u32 reported_frame_count = 0;

	if (count < sizeof(reported_frame_count)) {
		pr_err("Read of touch sim device require dest buffer of size >= %zd",
		       sizeof(reported_frame_count));
		return -EINVAL;
	}

	struct touch_sim *sim = file->private_data;

	if (touch_sim_has_error(sim)) {
		pr_warn("read: touch sim has error");
		return -EINVAL;
	}

	reported_frame_count = (u32)atomic_read(&sim->reported_frame_count);

	long ret = copy_to_user(user_buf, &reported_frame_count,
				sizeof(reported_frame_count));
	if (ret != 0) {
		pr_err("copy_to_user(,%zd) failed, ret %ld",
		       sizeof(reported_frame_count), ret);
		return -EFAULT;
	}

	return sizeof(reported_frame_count);
}

static int peek_frame_size(u8 *buf, size_t count, u32 *frame_size)
{
	if (buf == NULL || count < 4 || frame_size == NULL)
		return -EINVAL;

	memcpy(frame_size, buf, sizeof(u32));
	pr_info("%s: %u", __func__, *frame_size);
	return 0;
}

static ssize_t touch_sim_write(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *offset)
{
	struct touch_sim *sim = file->private_data;
	int ret = 0;
	u8 *buf;

	if (!sim || !sim->temp_page ||
	    (count > (PAGE_SIZE << DEFAULT_TEMP_PAGE_ORDER))) {
		pr_err("No available sim resources or buffer too large(%zd)!",
		       count);
		ret = -EINVAL;
		goto err_write;
	}

	buf = (u8 *)sim->temp_page;
	ret = copy_from_user(buf, user_buf, count);
	if (ret < 0) {
		pr_err("copy_from_user(,,%zd) failed, ret %d", count, ret);
		ret = -EFAULT;
		goto err_write;
	}

	if (sim->frame_size == 0) {
		ret = peek_frame_size((u8 *)buf, count, &sim->frame_size);
		if (ret) {
			pr_warn("peek_frame_size() failed %d", ret);
			goto err_write;
		}

		ret = touch_sim_start(sim, sim->frame_size);
		if (ret) {
			pr_warn("start() failed %d", ret);
			touch_sim_stop(sim);
			goto err_write;
		}
	}

	if (!touch_sim_is_ready(sim)) {
		pr_warn("is not ready!");
		ret = -EIO;
		goto err_write;
	}

	ret = touch_sim_add_frame_data(sim, file, (u8 *)buf, count);
	pr_debug("%s: write %d bytes(count %zd) at offset %lld ... DONE",
		 __func__, ret, count, *offset);

err_write:
	if (ret < 0)
		pr_err("%s: failed, ret %d!", __func__, ret);
	return ret;
}

static int touch_sim_open(struct inode *inode, struct file *file)
{
	struct touch_sim *sim;

	sim = container_of(inode->i_cdev, struct touch_sim, cdev);

	if (sim == NULL || sim->pop_data_cb == NULL ||
	    sim->private_data == NULL) {
		pr_warn("%s: pop_data_cb function is null.", __func__);
		return -EINVAL;
	}

	if (atomic_cmpxchg(&sim->device_is_locked, 0, 1) != 0) {
		pr_warn("%s: other cmd is running.", __func__);
		return -EBUSY;
	}

	sim->frame_size = 0;
	if (sim->temp_page) {
		free_page(sim->temp_page);
		sim->temp_page = 0;
	}
	sim->temp_page = __get_free_pages(GFP_KERNEL, DEFAULT_TEMP_PAGE_ORDER);
	atomic_set(&sim->reported_frame_count, 0);
	sim->total_diff_ns = 0;
	sim->max_diff_ns = S64_MIN;
	sim->min_diff_ns = S64_MAX;

	file->private_data = sim;
	return 0;
}

static int touch_sim_release(struct inode *inode, struct file *file)
{
	struct touch_sim *sim;

	sim = container_of(inode->i_cdev, struct touch_sim, cdev);
	touch_sim_stop(sim);
	if (sim->temp_page) {
		free_pages(sim->temp_page, DEFAULT_TEMP_PAGE_ORDER);
		sim->temp_page = 0;
	}

	u64 frame_count = (u32)atomic_read(&sim->reported_frame_count);

	atomic_set(&sim->device_is_locked, 0);
	pr_info("Frame count %llu, Average diff: %lld us, Max diff: %lld us, Min diff: %lld us",
		frame_count,
		frame_count > 0 ? (s64)(sim->total_diff_ns / (s64)frame_count) /
					  NSEC_PER_USEC :
				  0,
		sim->max_diff_ns / NSEC_PER_USEC,
		sim->min_diff_ns / NSEC_PER_USEC);
	pr_info("Device released");

	return 0;
}

static unsigned int touch_sim_poll(struct file *file, poll_table *wait)
{
	struct touch_sim *sim = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &sim->event_wait_queue, wait);

	if (touch_sim_has_error(sim)) {
		pr_warn("poll: touch sim has error");
		return EPOLLERR | EPOLLIN | EPOLLOUT | EPOLLWRNORM;
	}

	if (!kfifo_is_full(&sim->fifo))
		mask |= EPOLLOUT | EPOLLWRNORM;

	if (kfifo_is_empty(&sim->fifo) &&
	    atomic_read(&sim->reported_frame_count) > 0)
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

const struct file_operations touch_sim_fops = {
	.write = touch_sim_write,
	.read = touch_sim_read,
	.open = touch_sim_open,
	.release = touch_sim_release,
	.poll = touch_sim_poll,
};
