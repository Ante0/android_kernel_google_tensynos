.. SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause

=====================
Google Crash Debug Dump
=====================

sysfs entries
===============

  - /sys/devices/platform/gcdd/system_dev_stat
    Write the status value to the device index (ref:google_cdd_system_device).
    Separate with a space, accept hex w/wo 0x prefix.
    Example: echo "0x4 0x9" > system_dev_stat
