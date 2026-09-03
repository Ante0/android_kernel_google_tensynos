// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles tethering offload related information.
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Mark Chien <markchien@google.com>
 */
#include "noa_tethering_manager.h"

int32_t value;
Tether4Entry v4_entry;
Tether4Key v4_key;
TetherUpstream6Entry upstream6_entry;
TetherUpstream6Key upstream6_key;
TetherDownstream6Entry downstream6_entry;
TetherDownstream6Key downstream6_key;
TetherConfig tether_config;
TetherStats stats;
NetlinkConfig netlink_conf;

dev_t dev;
static struct class *dev_class;
static struct cdev noa_cdev;
struct file_prvdata *s_prvdata;

/*
** Function Prototypes
*/
static int      noa_open(struct inode *inode, struct file *file);
static int      noa_release(struct inode *inode, struct file *file);
static ssize_t  noa_read(struct file *filp, char __user *buf, size_t len,loff_t *off);
static ssize_t  noa_write(struct file *filp, const char *buf, size_t len, loff_t *off);
static long     noa_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/*
** File operation structure
*/
static struct file_operations fops = {
	.owner          = THIS_MODULE,
	.read           = noa_read,
	.write          = noa_write,
	.open           = noa_open,
	.unlocked_ioctl = noa_ioctl,
	.release        = noa_release,
};

static bool is_callback_ready_to_read(struct file_prvdata *prvdata)
{
	return atomic_read(&prvdata->pending_cb_count) > 0;
}

static ssize_t noa_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	struct file_prvdata *prvdata = file->private_data;
	struct noa_callback_node *node = NULL;
	ssize_t retval = 0;

	pr_info("%s\n", __func__);

	// Waiting for a callback
	while(!is_callback_ready_to_read(prvdata)) {
		if (file->f_flags & O_NONBLOCK) {
			return -EAGAIN;
		}

		// Sleep until a callback come.
		retval = wait_event_interruptible(
			prvdata->read_queue,
			is_callback_ready_to_read(prvdata));

		if (retval == -ERESTARTSYS) {
			// If the wait was interrupted, we are probably
			// being killed. Quit.
			return -EINTR;
		}
	}

	// Process a callback
	// pop pending callback from the list.
	mutex_lock(&prvdata->pending_cb_lock);
	node = list_first_entry_or_null(&prvdata->pending_neteng_callbacks, struct noa_callback_node, cb_list);
	mutex_unlock(&prvdata->pending_cb_lock);

	retval = copy_to_user(buf, node->cb_buffer, sizeof(NoaCallbackCommand));
	// copy_to_user returns bytes that couldn't be copied
	retval = sizeof(NoaCallbackCommand) - retval;

	mutex_lock(&prvdata->pending_cb_lock);
	list_del(&node->cb_list);
	atomic_dec(&prvdata->pending_cb_count);
	mutex_unlock(&prvdata->pending_cb_lock);

	kfree(node);
	return retval;
}

// Create a callback node and attach it to the pending list
void send_callback(uint32_t type, const void *data) {
	struct noa_callback_node *node = NULL;
	pr_info ("%s: Add callback to list.", __func__);

	if (s_prvdata == NULL) {
		pr_err("%s: no user care the callback, dops", __func__);

		return;
	}

	node = kzalloc(sizeof(struct noa_callback_node), GFP_KERNEL);

	INIT_LIST_HEAD(&node->cb_list);
	node->cb.type = type;
	if (type == CMD_CALLBACK_EXTEND_TIMEOUT) {
		memcpy(node->cb.data, data, sizeof(Tether4Key));
	}

	mutex_lock(&s_prvdata->pending_cb_lock);
	list_add_tail(&node->cb_list, &s_prvdata->pending_neteng_callbacks);
	atomic_inc(&s_prvdata->pending_cb_count);
	mutex_unlock(&s_prvdata->pending_cb_lock);

	wake_up(&s_prvdata->read_queue);
}

const static struct neteng_callback_ops neteng_cb_ops = {
	.write = send_callback,
};

static ssize_t noa_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
	pr_debug("Device write should never be used: %s\n", __func__);
	return len;
}

static int noa_open(struct inode *inode, struct file *file)
{
	int rc = 0;
	struct file_prvdata *prvdata;

	pr_info("%s: major %d minor %d\n", __func__, MAJOR(inode->i_rdev), MINOR(inode->i_rdev));

	if (s_prvdata != NULL) {
		rc = -EMFILE;
		goto r_already_open;
	}

	prvdata = kmalloc(sizeof(struct file_prvdata), GFP_KERNEL);
	if (!prvdata) {
		rc = -ENOMEM;
		goto r_kmalloc;
	}
	file->private_data = prvdata;
	s_prvdata = prvdata;

	INIT_LIST_HEAD(&prvdata->pending_neteng_callbacks);
	atomic_set(&prvdata->pending_cb_count, 0);

	init_waitqueue_head(&prvdata->read_queue);

	pr_info("neteng_cb registered\n");
	noa_register_neteng_callback(&neteng_cb_ops);

r_kmalloc:
r_already_open:
	return rc;
}

static int noa_release(struct inode *inode, struct file *file)
{
	struct file_prvdata *prvdata = file->private_data;

	pr_info("%s\n", __func__);

	if (!prvdata)
		return -ENODEV;

	// free the pending callbacks here.

	kfree(prvdata);
	s_prvdata = NULL;
	file->private_data = NULL;
	return 0;
}

static long noa_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	switch(cmd) {
	case TEST_SEND:
		if (copy_from_user(&value, (int32_t *) arg, sizeof(value))) {
			pr_err("noa_cont Data Write : Err!\n");
		}
		pr_info("%s: Greeting from userspace, %d\n", __func__, value);
		break;

	case PUT_UPSTREAM_4MAP:
		if (copy_from_user(&v4_entry, (const void __user *) arg, sizeof(Tether4Entry))) {
			pr_err("noa_cont Write Tether4Entry: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_PUT_UPSTREAM_4MAP, &v4_entry, sizeof(Tether4Entry),
									NULL, NULL);
		break;

	case PUT_DOWNSTREAM_4MAP:
		if (copy_from_user(&v4_entry, (const void __user *) arg, sizeof(Tether4Entry))) {
			pr_err("noa_cont Write Tether4Entry: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_PUT_DOWNSTREAM_4MAP, &v4_entry, sizeof(Tether4Entry),
									NULL, NULL);
		break;

	case REMOVE_UPSTREAM_4MAP:
		if (copy_from_user(&v4_key, (const void __user *) arg, sizeof(Tether4Key))) {
			pr_err("noa_cont Write Tether4Key: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_REMOVE_UPSTREAM_4MAP, &v4_key, sizeof(Tether4Key),
									NULL, NULL);
		break;

	case REMOVE_DOWNSTREAM_4MAP:
		if (copy_from_user(&v4_key, (const void __user *) arg, sizeof(Tether4Key))) {
			pr_err("noa_cont Write Tether4Key: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_REMOVE_DOWNSTREAM_4MAP, &v4_key, sizeof(Tether4Key),
									NULL, NULL);
		break;

	case PUT_UPSTREAM_6MAP:
		if (copy_from_user(&upstream6_entry, (const void __user *) arg,
				sizeof(TetherUpstream6Entry))) {
			pr_err("noa_cont Write TetherUpstream6Entry: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_PUT_UPSTREAM_6MAP, &upstream6_entry,
									sizeof(TetherUpstream6Entry), NULL, NULL);
		break;

	case REMOVE_UPSTREAM_6MAP:
		if (copy_from_user(&upstream6_key, (const void __user *) arg,
				sizeof(TetherUpstream6Key))) {
			pr_err("noa_cont Write TetherUpstream6Key: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_REMOVE_UPSTREAM_6MAP, &upstream6_key,
									sizeof(TetherUpstream6Key), NULL, NULL);
		break;

	case PUT_DOWNSTREAM_6MAP:
		if (copy_from_user(&downstream6_entry, (const void __user *) arg,
				sizeof(TetherDownstream6Entry))) {
			pr_err("noa_cont Write TetherDownstream6Entry: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_PUT_DOWNSTREAM_6MAP, &downstream6_entry,
									sizeof(TetherDownstream6Entry), NULL, NULL);
		break;

	case REMOVE_DOWNSTREAM_6MAP:
		if (copy_from_user(&downstream6_key, (const void __user *) arg,
				sizeof(TetherDownstream6Key))) {
			pr_err("noa_cont Write TetherDownstream6Entry: Err!\n");
		}
		ret = noa_nep_cmd_request_send(CMD_REMOVE_DOWNSTREAM_6MAP, &downstream6_key,
									sizeof(TetherDownstream6Key), NULL, NULL);
		break;

	case SET_CONFIG:
		if (copy_from_user(&tether_config, (const void __user *) arg, sizeof(TetherConfig))) {
			pr_err("noa_cont set_config: Err!\n");
			return -1;
		}
		pr_info("set_config: upstreamIif %d, downstreamIif %d, limit %llu",
				tether_config.upstreamIif, tether_config.downstreamIif, tether_config.limit);
		ret = noa_nep_cmd_request_send(CMD_SET_CONFIG, &tether_config, sizeof(TetherConfig),
									NULL, NULL);
		break;

	case REMOVE_CONFIG:
		pr_info("remove_config");
		ret = noa_nep_cmd_request_send(CMD_REMOVE_CONFIG, NULL, 0, NULL, NULL);
		break;

	case GET_STATS:
		if (!noa_nep_cmd_request_send(CMD_GET_STATS, NULL, 0, &stats, NULL)) {
			pr_info("get_stats(%d): rx pkt/b/err %llu, %llu, %llu, tx pkt/b/err/ %llu, %llu, %llu",
				ret,
				stats.value.rxPackets, stats.value.rxBytes, stats.value.rxErrors,
				stats.value.txPackets, stats.value.txBytes, stats.value.txErrors);
		  if (copy_to_user((void __user *) arg, &stats, sizeof(TetherStats))) {
			pr_err("noa_cont Read stats entry fail: Err!\n");
			return -1;
		  }
		} else {
			pr_info("get_stats fail");
		}
		break;

	case PUT_NETLINK_CONF:
		if (copy_from_user(&netlink_conf, (const void __user *) arg, sizeof(NetlinkConfig))) {
			pr_err("noa_cont set netlink_conf: Err!\n");
			return -1;
		}
		pr_debug("netlink_conf: type %d family %d prefixlen %d mac %pM ifindex %d state %x",
			netlink_conf.type, netlink_conf.family, netlink_conf.prefixlen,
			netlink_conf.mac, netlink_conf.ifindex, netlink_conf.state);
		if (netlink_conf.family == AF_INET) {
			pr_debug("netlink_conf: ip %pI4", &netlink_conf.addr.ip4addr.s_addr);
		} else if (netlink_conf.family == AF_INET6) {
			pr_debug("netlink_conf: ipv6 %pI6", &netlink_conf.addr.ip6addr.s6_addr);
		}

		// Pass the netlink config info to NOA.
		ret = noa_nep_cmd_request_send(CMD_PUT_NETLINK_CONF, &netlink_conf, sizeof(netlink_conf),
									NULL, NULL);
		break;

	default:
		pr_info("noa_cont Default\n");
		break;
	}
	return ret;
}

/*
** Module init for driver mode simulator
*/
int init_noa_tethering_manager(void)
{
	pr_info("module init: %s\n", __func__);
	/*Allocating Major number*/
	if ((alloc_chrdev_region(&dev, 0, 1, "noa_Dev")) <0) {
		pr_err("Cannot allocate major number\n");
		return -1;
	}
	pr_info("Major = %d Minor = %d \n", MAJOR(dev), MINOR(dev));

	/*Creating cdev structure*/
	cdev_init(&noa_cdev,&fops);

	/*Adding character device to the system*/
	if ((cdev_add(&noa_cdev,dev,1)) < 0) {
	pr_err("Cannot add the device to the system\n");
	goto r_class;
	}

	/*Creating struct class*/
#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 3, 13))
	dev_class = class_create("noa_class");
#else
	dev_class = class_create(THIS_MODULE, "noa_class");
#endif
	if (IS_ERR(dev_class)) {
		pr_err("Cannot create the struct class\n");
		goto r_class;
	}

	/*Creating device*/
	if (IS_ERR(device_create(dev_class, NULL, dev, NULL, "noa_device"))) {
		pr_err("Cannot create the Device 1\n");
		goto r_device;
	}
	pr_info("Device Driver Insert...Done!!!\n");

#if IS_ENABLED(CONFIG_NOA_VPN_OFFLOAD_SUPPORT)
	noa_vpn_manager_init();
	pr_info("noa_vpn_manager_init Done\n");
#endif

	return 0;

r_device:
	class_destroy(dev_class);
r_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

/*
** Module exit for driver mode simulator
*/
void deinit_noa_tethering_manager(void)
{
	pr_debug("module exit: %s\n", __func__);
	device_destroy(dev_class,dev);
	class_destroy(dev_class);
	cdev_del(&noa_cdev);
	unregister_chrdev_region(dev, 1);
}
