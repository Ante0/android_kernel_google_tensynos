.. SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */

==============================
Google CPM interface Ping Test
==============================

This test module provides commands to
- configure the Google CPM Interface Test parameters and
- Trigger the ping test to send mailbox ping messages to CPM FW
- If applicable, validate the response received
- Return the status Pass/Fail with error if any

debugfs entries
===============

    - /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/msg_type
        type of message to be sent to CPM
            0: ONEWAY_MSG: a one-way message indicates that no response is expect for this msg.
            1: REQUEST: a request message indicates that a response is expected back from the CPM FW.

        Example:
            echo 1 > /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/msg_type
            cat /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/msg_type
            output: Y

    - /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/nr_threads
        number of threads to parallelly run the test
            range: 1 to 100

        Example:
            echo 20 > /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/nr_threads
            cat /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/nr_threads
            output: 20

    - /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/nr_iterations
        number of iterations to run the test for
            range: 1 to 1000

        Example:
            echo 200 > /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/nr_iterations
            cat /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/nr_iterations
            output: 200

    - /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/tout_ms
        request timeout in milliseconds
	    range: 0 to 100

        Example:
            echo 100 > /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/tout_ms
            cat /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/tout_ms
            output: Y

    - /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/trigger_test
	command to trigger the ping test

        Example:
            cat /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/trigger_test
            output: [Pass/Fail]

    - /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/test_usage
	prints the usage for the ping test

	Example:
	    cat /sys/kernel/debug/goog_mba_cpm_iface_test/ping_test/test_usage
	    Output:
	    Usage:
            echo <msg_type>      > <test_path>/msg_type      (0 (ONEWAY), 1 (REQUEST))
            echo <nr_threads>    > <test_path>/nr_threads    (Range: 1 to 100)
            echo <nr_iterations> > <test_path>/nr_iterations (Range: 1 to 1000)
            echo <tout_ms>       > <test_path>/tout_ms       (Range: 1 to 100)
