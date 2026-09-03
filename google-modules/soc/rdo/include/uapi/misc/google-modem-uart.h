/* SPDX-License-Identifier: GPL-2.0  WITH Linux-syscall-note */
/*
 * Copyright (C) 2026 Google LLC.
 *
 */

#ifndef _UAPI_GOOGLE_MODEM_UART_H
#define _UAPI_GOOGLE_MODEM_UART_H

#include <linux/ioctl.h>

#define MODEM_UART_IOC_MAGIC 'u'

#define MODEM_UART_IOC_START _IO(MODEM_UART_IOC_MAGIC, 1)
#define MODEM_UART_IOC_STOP _IO(MODEM_UART_IOC_MAGIC, 2)
#define MODEM_UART_IOC_CLEAR _IO(MODEM_UART_IOC_MAGIC, 3)
#define MODEM_UART_IOC_STATUS _IOR(MODEM_UART_IOC_MAGIC, 4, int)

#endif /* _UAPI_GOOGLE_MODEM_UART_H */
