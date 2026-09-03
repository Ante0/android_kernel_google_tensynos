// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/module.h>
#include <example/foo.h>

static void bar_helper_func(void)
{
	pr_info("bar: helper func calling foo_api_func\n");
	foo_api_func();
}

static int __init bar_init(void)
{
	pr_info("bar: init\n");
	bar_helper_func();
	return 0;
}

static void __exit bar_exit(void)
{
	pr_info("bar: exit\n");
}

module_init(bar_init);
module_exit(bar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Example Author");
MODULE_DESCRIPTION("Example bar module depending on foo");
