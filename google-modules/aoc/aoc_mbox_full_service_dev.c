// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2026 Google LLC. All Rights Reserved.
 *
 * aoc service to handle full mailbox buffer
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "aoc.h"
#include "aoc-interface.h"

#define MBOX_FULL_SERVICE_NAME "aoc_mbox_full_sdev"
static const char * const mbox_full_service_names[] = {
	"mbox_full_a3",
	"mbox_full_sc",
	"mbox_full_f1",
	NULL,
};

static void aoc_mbox_full_service_handler(struct aoc_service_dev *dev)
{
	uint8_t offset;
	while (aoc_service_read(dev, &offset, sizeof(offset), false) > 0)
		schedule_service_work(offset);
}

static int aoc_mbox_full_service_probe(struct aoc_service_dev *sd)
{
	sd->handler = aoc_mbox_full_service_handler;
	return 0;
}

static int aoc_mbox_full_service_remove(struct aoc_service_dev *sd)
{
	return 0;
}

static struct aoc_driver aoc_mbox_full_sdev = {
	.drv = {
		.name = MBOX_FULL_SERVICE_NAME,
	},
	.service_names = mbox_full_service_names,
	.probe = aoc_mbox_full_service_probe,
	.remove = aoc_mbox_full_service_remove,
};

static int __init aoc_mbox_full_service_init(void)
{
	aoc_driver_register(&aoc_mbox_full_sdev);
	return 0;
}

static void __exit aoc_mbox_full_service_exit(void)
{
	aoc_driver_unregister(&aoc_mbox_full_sdev);
}

module_init(aoc_mbox_full_service_init);
module_exit(aoc_mbox_full_service_exit);

MODULE_DESCRIPTION("AOC mbox full driver");
MODULE_LICENSE("GPL");
