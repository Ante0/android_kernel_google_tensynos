/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_PORT_IO_TTY_H__
#define __MTK_PORT_IO_TTY_H__

#include <linux/tty.h>
#include <linux/tty_flip.h>

#define MTK_TTY_MINOR_BASE			(MTK_CDEV_MINOR_BASE + MTK_CDEV_MAX_NUM)
#define MTK_TTY_MAX_NUM			(128)

int mtk_port_tty_register(void);
void mtk_port_tty_unregister(void);

extern const struct tty_operations tty_fops;
extern struct tty_port_operations null_ops;
extern const struct port_ops port_tty_ops;

#endif

