/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GXP mailbox array client registers.
 *
 * Copyright (C) 2024 Google LLC
 */

#ifndef __GANYMEDE_MAILBOX_REGS_H__
#define __GANYMEDE_MAILBOX_REGS_H__

#include <linux/bitops.h>

#define MBOX_CLIENT_IRQ_TRIG 0x00

#define MBOX_CLIENT_IRQ_CFG 0x04
#define MBOX_CLIENT_IRQ_MSG_INT BIT(16)

#define MBOX_CLIENT_IRQ_STATUS 0x08
#define MBOX_CLIENT_STATUS_MSG_INT BIT(0)
#define MBOX_CLIENT_SHDW 0x0c
#define MBOX_CLIENT_SHDW_HOST_EN BIT(0)

/* Mailbox Shared Data Registers  */
#define MBOX_DATA_REG_BASE 0x0100

#define MBOX_DATA_STATUS_OFFSET 0x00
#define MBOX_DATA_DESCRIPTOR_ADDR_OFFSET 0x04
#define MBOX_DATA_CMD_TAIL_RESP_HEAD_OFFSET 0x08
#define MBOX_DATA_CMD_HEAD_RESP_TAIL_OFFSET 0x0C
#define MBOX_DATA_CONTROL_OFFSET 0x30

#endif /* __GANYMEDE_MAILBOX_REGS_H__ */
