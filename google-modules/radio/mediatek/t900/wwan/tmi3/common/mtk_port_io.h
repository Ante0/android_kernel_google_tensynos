/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_PORT_IO_H__
#define __MTK_PORT_IO_H__

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "mtk_bm.h"
#include "mtk_debug.h"
#include "mtk_port.h"

/* Default rx buffer */
#define MTK_RX_BUF_SIZE			(1024 * 1024)
#define MTK_RX_BUF_MAX_SIZE		(2 * 1024 * 1024)

/* CDEV and Proprietary port minor region:0~127
 */
#define MTK_CDEV_MINOR_BASE			(0)
#define MTK_CDEV_MAX_NUM			(128)

#define MTK_DFLT_DUMP_RX_BUDGET			(16)

#define MTK_IOC_MAGIC					'M'
#define MTK_IOC_SET_RX_BUF_SIZE			_IOW(MTK_IOC_MAGIC, 0xA0, unsigned int)
#define MTK_IOC_ALLOW_DROP_PACKET			_IO(MTK_IOC_MAGIC, 0xA1)
#define MTK_IOC_FORBID_DROP_PACKET			_IO(MTK_IOC_MAGIC, 0xA2)
/* This IOCTL is used to retrieve the wMaxCommand for the device,
 * defining the message limit for both reading and writing.
 */
#define MTK_IOCTL_WDM_MAX_COMMAND			_IOR('H', 0xA0, unsigned short)

extern struct mutex port_mngr_grp_mtx;

struct port_ops {
	int (*init)(struct mtk_port *port);
	int (*exit)(struct mtk_port *port);
	int (*reset)(struct mtk_port *port);
	int (*enable)(struct mtk_port *port);
	int (*disable)(struct mtk_port *port);
	int (*recv)(struct mtk_port *port, struct sk_buff *skb);
	int (*match)(struct mtk_port *port, struct sk_buff *skb);
	void (*dump)(struct mtk_port *port);
};

union user_buf {
	void __user *ubuf;
	void *kbuf;
};

int mtk_port_io_init(void);
void mtk_port_io_exit(void);

int mtk_port_search(char *name, dev_t *result, unsigned int max_cnt);
int mtk_port_open(dev_t devt, int flag);
int mtk_port_close(dev_t devt);
int mtk_port_write(dev_t devt, void *buf, unsigned int len);
int mtk_port_read(dev_t devt, void *buf, unsigned int len);
long mtk_port_ioctl(dev_t devt, unsigned int cmd, unsigned long arg);

void *mtk_port_internal_open(struct mtk_md_dev *mdev, char *name, int flag);
int mtk_port_internal_close(void *i_port);
int mtk_port_internal_write(void *i_port, struct sk_buff *skb);
long mtk_port_internal_ioctl(void *i_port, unsigned int cmd, unsigned long arg);
void mtk_port_internal_recv_register(void *i_port,
				     int (*cb)(void *priv, struct sk_buff *skb),
				     void *arg);

int mtk_port_send_brom_cmd(struct mtk_port *port, unsigned char cmd);
int mtk_port_read_brom_cmd_ack(struct mtk_port *port, unsigned char expected_cmd,
			       unsigned int sleep_time);

static inline struct sk_buff *mtk_port_alloc_tx_skb(struct mtk_port *port)
{
	if (port->port_mngr) {
		if (port->tx_frag_size > Q_MTU_3_5K)
			return mtk_mem_alloc_skb(port->port_mngr->ctrl_blk->bm_pool_63K,
						 Q_MTU_63K, 0);
		else
			return mtk_mem_alloc_skb(port->port_mngr->ctrl_blk->bm_pool,
						 Q_MTU_3_5K, 0);
	}

	return NULL;
}

static inline void mtk_port_free_tx_skb(struct mtk_port *port, struct sk_buff *skb)
{
	if (!port->port_mngr) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (port->tx_frag_size > Q_FRAG_3_5K)
		mtk_mem_free_skb(port->port_mngr->ctrl_blk->bm_pool_63K, skb);
	else
		mtk_mem_free_skb(port->port_mngr->ctrl_blk->bm_pool, skb);
}

static inline void mtk_port_free_rx_skb(struct mtk_port *port, struct sk_buff *skb)
{
	if (!port || !port->port_mngr || !test_bit(PORT_S_ENABLE, &port->status)) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (port->rx_frag_size > Q_FRAG_3_5K)
		mtk_mem_free_skb(port->port_mngr->ctrl_blk->bm_pool_63K, skb);
	else
		mtk_mem_free_skb(port->port_mngr->ctrl_blk->bm_pool, skb);
}

int mtk_port_io_init(void);
void mtk_port_io_exit(void);

#endif /* __MTK_PORT_IO_H__ */
