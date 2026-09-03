// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/relay.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/wait.h>

#include "mtk_port.h"
#include "mtk_port_io_tty.h"

int mtk_port_tty_register(void)
{
	int ret;

	tty_drv = tty_alloc_driver(MTK_TTY_MAX_NUM, 0);
	if (IS_ERR(tty_drv)) {
		ret = PTR_ERR(tty_drv);
		pr_err("Failed to alloc tty driver, ret = %d\n", ret);
		return ret;
	}

	tty_drv->driver_name = MTK_TTY_DRV_NAME;
	/* Alloc memory for copying port name later, can't
	 * use devm_kzalloc here because this is called
	 * early in module_init before probed device, there
	 * is no pointer of struct device.
	 */
	tty_drv->name = kzalloc(MTK_DFLT_FULL_NAME_LEN, GFP_KERNEL);
	/* Get major_id and minor base from port_mngr, to
	 * register tty port under ccci_major and following
	 * ccci char device
	 */
	tty_drv->major = ccci_major;
	tty_drv->minor_start = MTK_TTY_MINOR_BASE;

	tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	tty_drv->subtype = SERIAL_TYPE_NORMAL;
	tty_drv->flags = TTY_DRIVER_RESET_TERMIOS |
		TTY_DRIVER_REAL_RAW |
		TTY_DRIVER_DYNAMIC_DEV |
		TTY_DRIVER_UNNUMBERED_NODE;
	tty_drv->init_termios = tty_std_termios;
	tty_drv->init_termios.c_iflag = 0x00;
	tty_drv->init_termios.c_oflag = 0x00;
	tty_drv->init_termios.c_cflag |= CLOCAL;
	tty_drv->init_termios.c_lflag = 0x00;
	tty_set_operations(tty_drv, &tty_fops);

	ret = tty_register_driver(tty_drv);
	if (ret < 0) {
		tty_driver_kref_put(tty_drv);
		pr_err("Failed to register tty, ret=%d\n", ret);
	}

	return ret;
}

void mtk_port_tty_unregister(void)
{
	if (!tty_drv)
		return;
	/* can't use devm_kfree, because the pci_driver has been
	 * unregistered before here, no struct device pointer.
	 */
	kfree(tty_drv->name);
	tty_unregister_driver(tty_drv);
	tty_driver_kref_put(tty_drv);
	tty_drv = NULL;
}

static int mtk_port_tty_open(struct tty_struct *tty, struct file *file)
{
	struct tty_port *tty_port;
	struct tty_driver *drv;
	struct mtk_port *port;
	int tty_id;
	int ret;

	drv = tty->driver;
	tty_id = tty->index;
	tty_port = drv->ports[tty_id];
	if (!tty_port) {
		pr_err("[PORT_IO][%s][%d] Invalied: no such ttyport for id %d\n",
		       __func__, __LINE__, tty_id);
		ret = -ENODEV;
		goto err;
	}

	ret = tty_port_open(tty_port, tty, file);
	if (ret) {
		pr_err("[PORT_IO][%s][%d] Failed to open ttyport for id %d\n",
		       __func__, __LINE__, tty_id);
		goto err;
	}

	port = container_of(tty_port, struct mtk_port, priv.tty_port);
	ret = mtk_port_get_locked(port);
	if (ret)
		goto err;

	ret = mtk_port_common_open(port);
	if (ret) {
		mtk_port_put_locked(port);
		goto err;
	}

	tty->driver_data = port;

	if (file->f_flags & O_NONBLOCK)
		port->info.flags &= ~PORT_F_BLOCKING;
	else
		port->info.flags |= PORT_F_BLOCKING;

err:
	return ret;
}

static void mtk_port_tty_close(struct tty_struct *tty, struct file *file)
{
	struct tty_port *tty_port;
	struct mtk_port *port;

	port = tty->driver_data;
	tty_port = &port->priv.tty_port;

	/* Avoid close port twice */
	if (!test_bit(PORT_S_OPEN, &port->status)) {
		pr_err("[PORT_IO][%s][%d] Port has been closed\n", __func__, __LINE__);
		return;
	}

	tty_port_close(tty_port, tty, file);

	mtk_port_common_close(port);
	mtk_port_put_locked(port);
}

static int mtk_port_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	union user_buf user_buf;
	struct mtk_port *port;

	user_buf.kbuf = (char *)buf;
	port = tty->driver_data;

	return mtk_port_common_write(port, user_buf, (unsigned int)count, false);
}

#if KERNEL_VERSION(5, 14, 0) <= LINUX_VERSION_CODE
static unsigned int mtk_port_tty_write_room(struct tty_struct *tty)
#else
static int mtk_port_tty_write_room(struct tty_struct *tty)
#endif
{
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_port *port;
	int ret = 0;

	port = tty->driver_data;

	if (mtk_port_status_check(port))
		goto end;

	ctrl_blk = port->port_mngr->ctrl_blk;

end:
	return ret;
}

static int mtk_port_tty_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	union user_buf user_buf;
	struct mtk_port *port;

	user_buf.ubuf = (void __user *)arg;
	port = tty->driver_data;

	return mtk_port_common_ioctl(port, cmd, user_buf, true);
}

#ifdef CONFIG_COMPAT
static long mtk_port_tty_compat_ioctl(struct tty_struct *tty, unsigned int cmd, unsigned long arg)
{
	union user_buf user_buf;
	struct mtk_port *port;

	user_buf.ubuf = (void __user *)compat_ptr(arg);
	port = tty->driver_data;

	return mtk_port_common_ioctl(port, cmd, user_buf, true);
}
#endif

static int mtk_port_tty_init(struct mtk_port *port)
{
	struct tty_port *pport;

	mtk_port_struct_init(port);
	port->enable = false;

	port->minor = ida_alloc_range(&ccci_minors, MTK_TTY_MINOR_BASE,
				      MTK_TTY_MINOR_BASE + MTK_TTY_MAX_NUM - 1,
				      GFP_KERNEL);

	pport = &port->priv.tty_port;
	tty_port_init(pport);
	pport->ops = &null_ops;
	tty_port_link_device(pport, tty_drv, port->minor - MTK_TTY_MINOR_BASE);

	return 0;
}

static int mtk_port_tty_exit(struct mtk_port *port)
{
	struct tty_port *pport;

	if (test_bit(PORT_S_ENABLE, &port->status))
		ports_ops[port->info.type]->disable(port);

	pport = &port->priv.tty_port;
	tty_port_destroy(pport);
	tty_drv->ports[port->minor - MTK_TTY_MINOR_BASE] = NULL;

	ida_free(&ccci_minors, port->minor);

	pr_err("[PORT_IO][%s][%d] Port(%s) exit is complete\n",
	       __func__, __LINE__, port->info.name);

	return 0;
}

static int mtk_port_reset(struct mtk_port *port)
{
	port->tx_seq = 0;
	port->rx_seq = -1;

	return 0;
}

static int mtk_port_tty_enable(struct mtk_port *port)
{
	struct mtk_port_mngr *port_mngr;
	int tty_id;
	int ret;

	if (test_bit(PORT_S_ENABLE, &port->status)) {
		MTK_INFO(port->port_mngr->ctrl_blk->mdev,
			 "Skip to enable port(%s)\n", port->info.name);
		return 0;
	}

	ret = mtk_port_ch_enable(port);
	if (ret && ret != -EBUSY)
		return ret;

	port_mngr = port->port_mngr;

	/* Port created in /dev/wwanD(id)(name)
	 * example: /dev/wwanD0port
	 */
	snprintf((char *)tty_drv->name, MTK_DFLT_FULL_NAME_LEN, "%s%d%s",
		 MTK_PORT_NAME_HDR, port_mngr->dev_id, port->info.name);

	tty_id = port->minor - MTK_TTY_MINOR_BASE;

	tty_register_device(tty_drv, tty_id, port_mngr->ctrl_blk->mdev->dev);

	set_bit(PORT_S_RDWR, &port->status);
	set_bit(PORT_S_ENABLE, &port->status);
	MTK_INFO(port->port_mngr->ctrl_blk->mdev,
		 "Port(%s) enable is complete\n", port->info.name);

	return 0;
}

static int mtk_port_tty_disable(struct mtk_port *port)
{
	struct tty_port *pport;
	int tty_id;

	if (!test_and_clear_bit(PORT_S_ENABLE, &port->status)) {
		MTK_INFO(port->port_mngr->ctrl_blk->mdev,
			 "Skip to disable port(%s)\n", port->info.name);
		return 0;
	}

	clear_bit(PORT_S_RDWR, &port->status);
	tty_id = port->minor - MTK_TTY_MINOR_BASE;
	pport = tty_drv->ports[tty_id];
	if (!pport) {
		MTK_INFO(port->port_mngr->ctrl_blk->mdev,
			 "Invalid tty id:%d\n", tty_id);
		return -EINVAL;
	}

	tty_unregister_device(tty_drv, tty_id);
	mtk_port_ch_disable(port);

	MTK_INFO(port->port_mngr->ctrl_blk->mdev,
		 "Port(%s) disable is complete\n", port->info.name);

	return 0;
}

static int mtk_port_tty_recv(struct mtk_port *port, struct sk_buff *skb)
{
	struct tty_port *tty_port = &port->priv.tty_port;
	int ret;

start_recv:
	if (!test_bit(PORT_S_OPEN, &port->status)) {
		/* If current port is not opened by any user,
		 * the received data will be dropped
		 */
		MTK_WARN_RATELIMITED(port->port_mngr->ctrl_blk->mdev,
				     "Unabled to recv: (%s) not opened\n", port->info.name);
		port->rx_err_cnt++;
		ret = -ENXIO;
		goto end_recv;
	}

	if (tty_buffer_space_avail(tty_port) < skb->len) {
		/* If the rx buffer is not enough, the data will be dropped */
		MTK_WARN_RATELIMITED(port->port_mngr->ctrl_blk->mdev,
				     "Unabled to recv: (%s)rx buffer full\n", port->info.name);
		if (port->info.flags & PORT_F_ALLOW_DROP) {
			port->rx_err_cnt++;
			ret = -ENOMEM;
			goto end_recv;
		} else {
			MTK_INFO_RATELIMITED(port->port_mngr->ctrl_blk->mdev,
					     "Retry recv", port->info.name);
			/* Avoid occupying the CPU long time when blocking */
			usleep_range(1000, 2000);
			goto start_recv;
		}
	}
	ret = tty_insert_flip_string(tty_port, skb->data, skb->len);
	if (ret != skb->len)
		MTK_WARN(port->port_mngr->ctrl_blk->mdev,
			 "Unabled to recv data: (%s) drop packet\n", port->info.name);

	tty_flip_buffer_push(tty_port);

end_recv:
	mtk_port_free_rx_skb(port, skb);

	return ret;
}

static int mtk_port_match(struct mtk_port *port, struct sk_buff *skb)
{
	return 0;
}

static void mtk_port_dump(struct mtk_port *port)
{
	MTK_INFO(port->port_mngr->ctrl_blk->mdev,
		 "Dump info for port: dev_str:%s; port name:%s;\n",
		 port->port_mngr->ctrl_blk->mdev->dev_str, port->info.name);
	MTK_INFO(port->port_mngr->ctrl_blk->mdev,
		 "Tx_seq:%d; port tx_err_cnt:%d; port rx_seq:%d; port rx_err_cnt:%d\n",
		 port->tx_seq, port->tx_err_cnt, port->rx_seq, port->rx_err_cnt);
	MTK_INFO(port->port_mngr->ctrl_blk->mdev,
		 "Rx_data_len:%d;\n", port->rx_data_len);

	/* Dump rx data header */
	mtk_port_rx_skb_dump(port);
}

const struct tty_operations tty_fops = {
	.open = mtk_port_tty_open,
	.close = mtk_port_tty_close,
	.write = mtk_port_tty_write,
	.write_room = mtk_port_tty_write_room,
	.ioctl = mtk_port_tty_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = mtk_port_tty_compat_ioctl,
#endif
};

struct tty_port_operations null_ops = {};

const struct port_ops port_tty_ops = {
	.init = mtk_port_tty_init,
	.exit = mtk_port_tty_exit,
	.reset = mtk_port_reset,
	.enable = mtk_port_tty_enable,
	.disable = mtk_port_tty_disable,
	.recv = mtk_port_tty_recv,
	.match = mtk_port_match,
	.dump = mtk_port_dump,
};

