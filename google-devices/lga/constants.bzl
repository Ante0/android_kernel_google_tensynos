# SPDX-License-Identifier: GPL-2.0-only

"""
LGA constants.
"""

LGA_DTBS = [
    # keep sorted
    "lga-a0.dtb",
    "lga-b0.dtb",
]

LGA_MODULE_OUTS = [
    # keep sorted
    "drivers/dma-buf/heaps/cma_heap.ko",
    "drivers/gpu/drm/display/drm_display_helper.ko",
    "drivers/hwtracing/coresight/coresight.ko",
    "drivers/hwtracing/coresight/coresight-catu.ko",
    "drivers/hwtracing/coresight/coresight-etm4x.ko",
    "drivers/hwtracing/coresight/coresight-funnel.ko",
    "drivers/hwtracing/coresight/coresight-replicator.ko",
    "drivers/hwtracing/coresight/coresight-stm.ko",
    "drivers/hwtracing/coresight/coresight-tmc.ko",
    "drivers/hwtracing/coresight/coresight-trbe.ko",
    "drivers/hwtracing/stm/stm_core.ko",
    "drivers/i2c/i2c-dev.ko",
    "drivers/misc/eeprom/at24.ko",
    "drivers/nvmem/nvmem-rmem.ko",
    "drivers/perf/arm-cmn.ko",
    "drivers/perf/arm_dsu_pmu.ko",
    "drivers/scsi/sg.ko",
    "drivers/spi/spi-dw.ko",
    "drivers/spi/spi-loopback-test.ko",
    "drivers/watchdog/softdog.ko",
    "net/mac80211/mac80211.ko",
    "net/wireless/cfg80211.ko",
]
