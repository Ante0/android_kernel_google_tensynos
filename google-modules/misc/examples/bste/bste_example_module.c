// SPDX-License-Identifier: GPL-2.0-only
/*
 * BSTE Example Kernel Module
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>

#define BSTE_IOC_MAGIC 'b'
#define BSTE_IOC_PING _IO(BSTE_IOC_MAGIC, 1)

static long bste_example_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case BSTE_IOC_PING:
		pr_info("bste_example: received PING ioctl\n");
		return 42;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations bste_example_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = bste_example_ioctl,
};

static struct miscdevice bste_example_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "bste_example",
	.fops = &bste_example_fops,
};

static int __init bste_example_init(void)
{
	int ret;

	ret = misc_register(&bste_example_miscdev);
	if (ret) {
		pr_err("bste_example: failed to register misc device\n");
		return ret;
	}

	pr_info("bste_example: module loaded, device /dev/bste_example created\n");
	return 0;
}

static void __exit bste_example_exit(void)
{
	misc_deregister(&bste_example_miscdev);
	pr_info("bste_example: module unloaded\n");
}

module_init(bste_example_init);
module_exit(bste_example_exit);

MODULE_AUTHOR("Google");
MODULE_DESCRIPTION("BSTE Example Kernel Module");
MODULE_LICENSE("GPL");
