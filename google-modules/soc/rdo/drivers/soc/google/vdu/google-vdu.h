/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 *
 */

#ifndef GOOGLE_VDU_H
#define GOOGLE_VDU_H

#include <linux/cdev.h>
#include <linux/atomic.h>

struct class;
struct device;
struct gdmc_iface;

struct gvdu_character_device {
	struct cdev cdev;
	dev_t devt;
	struct class *class;

	char *data_buffer;
	size_t buffer_length;
	size_t max_buffer_size;

	struct mutex data_lock;
	atomic_t is_open;
};

struct gvdu_base {
	/* Parent platform device */
	struct device *dev;

	struct gvdu_character_device char_dev;
	struct gdmc_iface *gdmc_iface;
	uint32_t vdu_directive_max_size;

	char *grant_buffer;
	ssize_t grant_length;

	char *delegate_buffer;
	ssize_t delegate_length;

	const char *default_debug_vector;
	char *default_policy_type;
};

#if IS_ENABLED(CONFIG_KUNIT)
ssize_t gvdu_decode_base64_cdev_directive(struct device *dev);
ssize_t gvdu_decode_base64_buffer(struct device *dev, const char *source,
				  size_t count, char **dest);
int64_t gvdu_handle_mailbox_error(struct device *dev, int message_res,
					 uint32_t *header);
#endif

#endif /* GOOGLE_VDU_H */
