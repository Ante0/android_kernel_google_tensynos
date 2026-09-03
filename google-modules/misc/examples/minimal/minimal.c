// SPDX-License-Identifier: GPL-2.0-only

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

#include <minimal.h>

#include "minimal_priv.h"

void minimal_say_hello(void)
{
	pr_info(MINIMAL_MESSAGE);
}
EXPORT_SYMBOL_GPL(minimal_say_hello);

static int __init minimal_init(void)
{
	minimal_say_hello();
	return 0;
}

static void __exit minimal_exit(void)
{
	pr_info("Goodbye from minimal module!\n");
}

module_init(minimal_init);
module_exit(minimal_exit);

MODULE_AUTHOR("Example Author");
MODULE_DESCRIPTION("A minimal kernel module example");
MODULE_LICENSE("GPL");
