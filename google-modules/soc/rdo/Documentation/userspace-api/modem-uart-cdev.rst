.. SPDX-License-Identifier: GPL-2.0-only

=========================================
Modem UART Log Character Device Interface
=========================================

Overview
========

The ``/dev/modem_uart`` character device is the userspace interface for controlling the
storage and retrieval of modem UART logs. The kernel driver handles communication with
GDMC to route logs to a dedicated DRAM carveout.

User Workflow
-------------

1. Open the device file.
2. Use IOCTL commands to start/stop log storage or clear the buffer.
3. Use the read() operation to retrieve the saved logs from the DRAM buffer.

IOCTL Interface
===============


MODEM_UART_IOC_START
--------------------

Request to start saving modem UART logs to the DRAM carveout.

MODEM_UART_IOC_STOP
-------------------

Request to stop saving modem UART logs.

MODEM_LOG_IOC_CLEAR
-------------------

Erase all saved logs in the DRAM carveout.

Read
==============

Attempt to read saved modem UART logs from the DRAM buffer. The logs are saved in the
carveout as a linear buffer, so after reading all available logs, the user must call
MODEM_LOG_IOC_CLEAR to flush the buffer.
