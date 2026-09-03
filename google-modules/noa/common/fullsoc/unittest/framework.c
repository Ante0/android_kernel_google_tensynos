// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Unittest Framework
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#include <linux/module.h>
#include <core/print.h>
#include <unittest/platform.h>

int lvm_unittest_framework_init(void)
{
	return 0;
}

void lvm_unittest_framework_deinit(void)
{

}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Yen <henryyen@google.com>");
MODULE_DESCRIPTION("LVM Unittest Framework");
