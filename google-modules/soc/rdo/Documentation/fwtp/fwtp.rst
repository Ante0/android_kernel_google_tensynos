.. SPDX-License-Identifier: GPL-2.0

=================================
Pixel Firmware Tracepoint Drivers
=================================

Introduction
============

The Pixel Firmware Tracepoint (FWTP) Drivers provide support for reading and
publishing tracepoints from various Pixel firmware. A driver is provided for
each different firmware (e.g., GDMC, CPM, etc.). These drivers make use of a
common set of services (e.g., firmware communication services).

FWTP interfaces
===============

A system may have multiple firmwares that produce tracepoints. In order to
communicate with these firmwares and control tracepoint operations, the FWTP
services make use of FWTP interfaces.

The interface must set the ``send_message`` field to a function that may be used
to send messages through the interface.

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_if.h
   :identifiers: fwtp_if

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_if.h
   :identifiers: fwtp_if_send_message

FWTP interface platform layer
=============================

The FWTP interface services make use of a platform layer to provide some
platform specific services and definitions. Each platform must provide these
services and definitions.

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_platform.h
   :identifiers: fwtp_if_platform FWTP_MALLOC FWTP_FREE FWTP_IF_LOG_ERR
                 FWTP_IF_LOG_WARN

FWTP IPC clients
================

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_ipc_client.h
   :doc: FWTP IPC clients

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_ipc_client.h
   :identifiers: fwtp_ipc_client

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_ipc_client.c
   :identifiers: fwtp_ipc_client_register fwtp_ipc_client_unregister

FWTP protocol
=============

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :doc: FWTP protocol

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :identifiers: fwtp_msg_base fwtp_msg_type

FWTP protocol versioning
------------------------

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :doc: FWTP protocol versioning

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :identifiers: fwtp_msg_version

FWTP string tables
------------------

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :doc: FWTP string tables

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :identifiers: fwtp_msg_get_strings

FWTP tracepoint rings
---------------------

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :doc: FWTP tracepoint rings

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :identifiers: fwtp_msg_get_ring_info fwtp_msg_get_tracepoints

FWTP tracepoint ring notifications
----------------------------------

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :doc: FWTP tracepoint ring notifications

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :identifiers: fwtp_msg_ring_notify fwtp_msg_ring_subscribe

FWTP tracepoint filters
-----------------------

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :doc: FWTP filters

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :identifiers: fwtp_msg_get_filter_info fwtp_msg_filter_config_entry
                 fwtp_msg_set_filter_config

FWTP add tracepoint
-----------------------

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :doc: FWTP add tracepoint

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_protocol.h
   :identifiers: fwtp_msg_add_tracepoint

FWTP to firmware message interface
==================================

Communication between FWTP drivers and the firmware makes use of a mailbox and
DMA. Since the size of mailbox messages is limited to 16 bytes, DMA is used to
pass messages to the firmware.

FWTP drivers may send a message to firmware and the firmware may send a message
back using a DMA message buffer. FWTP drivers send a message by writing the
message data to the DMA message buffer and sending a mailbox message. The
firmware sends a message back by writing response message data to the same DMA
message buffer and sending a mailbox message back.

The FWTP mailbox message contains the physical address and size of the FWTP
message buffer in DMA memory. When FWTP drivers send a message, the mailbox
message contains the size of the message data in DMA. When the firmware sends a
response message back, the mailbox message contains the size of the response
message data in DMA.

The format of the FWTP mailbox message is shown below. Values are stored in
little-endian format.

.. list-table::
   :align: left

   * - **Bytes**
     - **Value**
   * - 0-3
     - Mailbox service ID (e.g., as set by ``goog_mba_nq_xport_set_service_id``).
   * - 4-7
     - Lower 32-bits of message DMA physical address.
   * - 8-11
     - Upper 32-bits of message DMA physical address.
   * - 12-13
     - Size of message buffer.
   * - 14-15
     - Size of data in message buffer.

FWTP to user space message interface
====================================

Communication between FWTP drivers and user space makes use of Netlink. Netlink
was chosen because it supports sending unsolicited messages from the kernel
which is needed to stream tracepoints to user space.

Debugfs interface
=================

FWTP drivers provide a debugfs interface for controlling and inspecting FWTP
tracepoint operations. The debugfs files are located in a directory named after
the device under the debugfs root.

request_subscribe
-----------------

Write-only. Enables or disables tracepoint subscriptions. Writing non-zero
enables, zero disables.

Examples::

  echo 1 > /sys/kernel/debug/fwtp_gdmc/request_subscribe
  echo 1 > /sys/kernel/debug/fwtp-cpm/cpm/request_subscribe

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_cpm.c
   :identifiers: fwtp_cpm_debugfs_write_subscribe

request_poll
------------

GDMC-specific. Write-only. Triggers polling of tracepoints from firmware.

Example::

  echo 1 > /sys/kernel/debug/fwtp_gdmc/request_poll

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_google_gdmc.c
   :identifiers: google_gdmc_tracepoint_debugfs_write_tracepoint_poll

filter_list
-----------

Read-only. Shows the list of available tracepoint filters.

Example::

  cat /sys/kernel/debug/fwtp-cpm/cpm/filter_list

Output example::

  POWER
  BM
  APCPWR
  IDS
  CPM
  ..

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp.c
   :identifiers: fwtp_debugfs_filter_list_show

filter_block
------------

Write-only. Configures filters to block tracepoints. Accepts a comma-separated
list of filter names, or `all` to select all filters.

Examples::

  echo "POWER,BM" > /sys/kernel/debug/fwtp-cpm/cpm/filter_block
  echo "all" > /sys/kernel/debug/fwtp-cpm/cpm/filter_block

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp.c
   :identifiers: fwtp_debugfs_filter_block_write

filter_unblock
--------------

Write-only. Configures filters to not block tracepoints. Accepts a
comma-separated list of filter names, or `all` to select all filters.

Examples::

  echo "GWDT" > /sys/kernel/debug/fwtp-cpm/cpm/filter_unblock
  echo "all" > /sys/kernel/debug/fwtp-cpm/cpm/filter_unblock

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp.c
   :identifiers: fwtp_debugfs_filter_unblock_write

add_tracepoint
--------------

Write-only. Adds a tracepoint with a given payload value.

Examples::

  echo 42 > /sys/kernel/debug/fwtp_gdmc/add_tracepoint
  echo 42 > /sys/kernel/debug/fwtp-cpm/cpm/add_tracepoint

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp.c
   :identifiers: fwtp_debugfs_add_tracepoint_write

trace
-----

Read-only. Shows the contents of the tracepoint buffer.

Examples::

  cat /sys/kernel/debug/fwtp_gdmc/trace
  cat /sys/kernel/debug/fwtp-cpm/cpm/trace

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp.c
   :identifiers: fwtp_debugfs_trace_show

notify_bytes
------------

Read-write. Configures the threshold byte count for triggering a notification to
the kernel.

Examples::

  echo 512 > /sys/kernel/debug/fwtp-cpm/cpm/notify_bytes
  cat /sys/kernel/debug/fwtp_gdmc/notify_bytes

.. kernel-doc:: private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp.c
   :identifiers: fwtp_debugfs_notify_bytes_read fwtp_debugfs_notify_bytes_write

Testing
=======

Testing GDMC tracepoints
------------------------

The GDMC FWTP driver may be tested by writing "1" to
`/sys/kernel/debug/fwtp_gdmc/request_poll` and reading tracepoints from the
kernel log.

.. code:: none

   $ adb shell mount -t debugfs none /sys/kernel/debug ; \
     adb shell dmesg -C && \
     adb shell "echo 1 > /sys/kernel/debug/fwtp_gdmc/request_poll" && \
     adb shell dmesg | grep fwtp_gdmc
   [  521.273558] fwtp-google-gdmc fwtp_gdmc: 448.909679: EHLD CPU 6
   [  521.280268] fwtp-google-gdmc fwtp_gdmc: 448.909681: EHLD Event id 2
   [  521.287356] fwtp-google-gdmc fwtp_gdmc: 448.912617: EHLD CPU 7
   .
   .
   .
   $

Testing CPM tracepoints
-----------------------

The CPM FWTP driver may be tested by installing it and looking for a probe
message in the kernel logs. The legacy CPM tracepoint driver must be removed
first.

.. code:: none

   $ adb shell find /vendor/lib/modules/* -name fwtp_cpm.ko
   /vendor/lib/modules/6.6.82-android15-8-ga5bc094e36eb-4k/extra/private/google-modules/soc/rdo/drivers/soc/google/fwtp/fwtp_cpm.ko
   $ adb shell rmmod google_cpm_tracepoint
   $ adb shell insmod `adb shell find /vendor/lib/modules/* -name fwtp_cpm.ko`
   $ adb shell lsmod | grep ^fwtp_cpm
   fwtp_cpm               16384  0
   $ adb shell dmesg | grep fwtp-cpm
   [ 1529.635014] fwtp-cpm fwtp_cpm: assigned reserved memory node cpm_tracepoint_ipc_dma
   [ 1529.653267] fwtp-cpm fwtp_cpm: Successfully probed CPM firmware tracepoint device.
   $

Linting
=======

The ``lint.sh`` script is provided as a convenience to run a set of linters on
the FWTP sources. It must be run from the root of the common kernel directory.

.. code:: none

   $ find . -name kernel-doc
   ./aosp/scripts/kernel-doc
   .
   .
   .
   $ (export KERNEL=`pwd` && cd aosp && \
      $KERNEL/private/google-modules/soc/rdo/drivers/soc/google/fwtp/lint.sh)
   Linting fwtp_google_gdmc.c
   Linting lint.sh
   $
