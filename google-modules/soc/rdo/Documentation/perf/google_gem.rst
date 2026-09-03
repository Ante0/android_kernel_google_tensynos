.. SPDX-License-Identifier: GPL-2.0-only

=====================
Google GEM Controller
=====================

PMU Driver
==========

Under Linux perf subsystem architecture, the google_gem driver exposes
PMU events, communicating with GDMC to decode the IP list string it
sends and determine the available GEMs and events on the platform (see
section "IP list" for the string format specification). Since such
information describing the availability and configuration of GEMs and
events is received at runtime, the driver then dynamically prepares the
associated data structures and callback functions for each PMU event
representing a GEM event to deliver the GEM event monitoring functions.

Callback functions are implemented by communicating with GDMC using the
mailbox with dedicated message formats. The formats include all the
necessary operations to complete an iteration of the event counter
monitoring process, and some optional operations are also included
(e.g.: event filter).

In addition to exposing GEM features via the Linux perf subsystem, the
driver also offers debugfs interfaces. These debugfs interfaces allow
developers to validate and test GEM functions directly, bypassing the
Linux perf subsystem and communicating directly with GDMC on the
mailbox. See section "debugfs entries" for more information.

IP list
=======

GDMC uses an ASCII string named "ip-list" to describe the relationship between
IPs and associated information, such as the supported event list and supported
filter list. GDMC sends this string when serving the GEM command
`GEMCTRL_CMD_IP_LIST`. The driver, as the receiver, is responsible for parsing
the string and determining the supported IPs for the platform. Here's the
definition of the "ip-list" format:

 - format version: 1.2.0 (magic = "g120")

 - format definition::

  ip-list := <event-group>[|<event-group>]*

  event-group := <event-group-name>|<ip-set>

  event-group-name := <name>

    <name> is used to look up matched name entry in the array of `struct eventgrp_desc` returned by
    `of_device_get_match_data()`.

  ip-set := <ip>[,<ip>]*

  ip := <id>:<name>:<num-counters>

debugfs entries
===============

  - /sys/kernel/debug/gem/mode_ctrl
    Control the mode and period for the measurement run.
    mode: name in string to select operation mode:
        userctrl: User to stop it by writing '0' to counter-<counter_ID>/enable.
        once: It stops automatically by counting milliseconds set to
              period_ctrl.
        interval: User to stop it by writing '0' to counter-<counter_ID>/enable.
    period: controls scan or interval period in milliseconds.
        mode=userctrl => the period in milliseconds between each time the
                         counters are copied.
        mode=once => duration in milliseconds from arm to disarm.
        mode=interval => period in milliseconds between each time the IPs are
                         armed and disarmed.
    Example: echo <mode> <period> > mode_ctrl

  - /sys/kernel/debug/gem/event_group<EVENT_GRP_ID>
    Directory defines an event group. An event group has shared events and other
    attributes for the GEM IPs associated with this event group.

    - /sys/kernel/debug/gem/event_group<EVENT_GRP_ID>/events/<event-name>/ID
      Dump ID for an event.
      Example: cat ID
      Output example:
      13

    - /sys/kernel/debug/gem/event_group<EVENT_GRP_ID>/events/<event-name>/type
      Dump type for an event.
      Example: cat type
      Output example:
      accumulator

    - /sys/kernel/debug/gem/event_group<EVENT_GRP_ID>/filters/<filter-name>/ID
      Dump ID for an filter.
      Example: cat ID
      Output example:
      11

  - /sys/kernel/debug/gem/ip
    Directory that has all the GEM IPs exposed by the driver. Each GEM IP is
    represented as a directory which has associated information for this IP as
    sub-files or sub-dirs.

    - /sys/kernel/debug/gem/ip/<IP_NAME>/available_{events,filters}
      symlinks to the event_group<EVENT_GRP_ID>/available_{events,filters}, where
      event_group<EVENT_GRP_ID> is the IP <IP_NAME> belongs to.

    - /sys/kernel/debug/gem/ip/<IP_NAME>/ID
      Dump ID of a GEM IP.
      Example: cat ID
      Output example:
      40

    - /sys/kernel/debug/gem/ip/<IP_NAME>/num_counter
      Dump number of counters of the IP.
      Example: cat num_counter
      Output example:
      10

  - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>
    Directory that has sub-files to configure and read status for the
    counter.

    - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>/event
      Write to assign event to counter. Read to get assigned event of the
      counter.
      event_ID: the event ID.
      Example: echo <event_ID> > event
      Example: cat event
      Output example:
      <event_ID>

    - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>/enable
      Write to enable/disable the counter (1 to enable and 0 to disable). Read
      to get status of the counter.
      Example: echo <0|1> > enable
      Example: cat enable
      Output example:
      <0|1>

    - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>/value
      Read to get actual count value from the counter.
      Example: cat value
      Output example:
      700

    - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>/filters
    Directory that has sub-files to configure filters for the counter.

      - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>/filters/<filter-name>/value
      Write to assign filter value. Read to get assigned filter value.
      Example: echo 0xfff444 > value
      Example: cat value
      Output example:
      0xfff444

      - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>/filters/<filter-name>/mask
      Write to assign filter mask. Read to get assigned filter mask.
      Example: echo 0xffff0000ff > mask
      Example: cat mask
      Output example:
      0xffff0000ff

      - /sys/kernel/debug/gem/ip/<IP_NAME>/counter-<counter_ID>/filters/<filter-name>/enable
      Write to enable/disable the filter (1 to enable and 0 to disable). Read
      to get status of the filter.
      Example: echo <0|1> > enable
      Example: cat enable
      Output example:
      <0|1>

  - /sys/kernel/debug/gem/ip/<IP_NAME>/enable
    Start or stop measurement run for an IP. 1 to start and 0 to stop.
    Example: echo <0|1> > enable

  - /sys/kernel/debug/gem/ip/<IP_NAME>/set_event_filter
    Configure GEM filters for the specified event on an IP, using
    different arguments to specify add or reset actions. This affects
    only the PMU perf subsystem (e.g.: simpleperf).
    To add filter:
        <event-name> <filter-name> <value> <mask>
    Example: echo write-req-transfers axaddr 556 0xffff > set_event_filter
    Example: echo read-req-blocked axprot 17 0xff > set_event_filter
    To reset filter:
        <event-name> -
    Example: echo read-req-blocked - > set_event_filter

    event-name: can be found in ip/<IP_NAME>/events.
    filter-name: can be found in ip/<IP_NAME>/filters.
    value: value for the filter.
    mask: mask for the filter.

    Filters will be applied to all future event monitoring of `simpleperf`. For example, assuming
    above example "add filter" is applied at GEM IP <IP_1>, the filter configuration will take
    effect on the following `simpleperf` event monitoring:

    $ simpleperf stat -a -e IP_1/write-req-transfers/ -e IP_1/read-req-blocked/

  - /sys/kernel/debug/gem/ip/<IP_NAME>/trace
    Directory that has sub-files to configure trace for the GEM IP.

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/ID
      Write to assign trace ID.
      Example: echo 0x15 > ID

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/enable
      Write to enable/disable the trace (1 to enable and 0 to disable). Read
      to get status of it.
      Example: echo <0|1> > enable
      Example: cat enable

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/filters/<filter-name>/value
      Write to assign filter value. Read to get assigned filter value.
      Example: echo 0xfff444 > value
      Example: cat value
      Output example:
      0xfff444

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/filters/<filter-name>/mask
      Write to assign filter mask. Read to get assigned filter mask.
      Example: echo 0xffff0000ff > mask
      Example: cat mask
      Output example:
      0xffff0000ff

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/filters/<filter-name>/ID
      Dump ID of the filter.
      Example: cat ID
      Output example:
      11

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/filters/<filter-name>/enable
      Write to enable/disable the filter (1 to enable and 0 to disable). Read
      to get status of the filter.
      Example: echo <0|1> > enable
      Example: cat enable
      Output example:
      <0|1>

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/types/<type-name>/ID
      Dump ID of the trace type.
      Example: cat ID
      Output example:
      11

    - /sys/kernel/debug/gem/ip/<IP_NAME>/trace/types/<type-name>/enable
      Write to enable/disable the trace type (1 to enable and 0 to disable). Read
      to get status of it.
      Example: echo <0|1> > enable
      Example: cat enable
      Output example:
      <0|1>
