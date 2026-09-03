// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include "foo_internal.h"

void foo_internal_func(void)
{
	pr_info("foo: internal func from helper\n");
}
