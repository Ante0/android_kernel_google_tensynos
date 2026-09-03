# SPDX-License-Identifier: GPL-2.0-only

"""
MALIBU constants.
"""

MALIBU_DTBS = [
    # keep sorted
    "malibu-a0.dtb",
    "malibu-b0.dtb",
]

MALIBU_MODULE_OUTS = [
    # keep sorted
    "drivers/gpio/gpio-pca953x.ko",
    "drivers/gpu/drm/display/drm_display_helper.ko",
    "drivers/hwtracing/coresight/coresight.ko",
    "drivers/hwtracing/coresight/coresight-catu.ko",
    "drivers/hwtracing/coresight/coresight-dummy.ko",
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
    "drivers/pci/pwrctrl/pci-pwrctrl-core.ko",
    "drivers/perf/arm-ni.ko",
    "drivers/perf/arm_dsu_pmu.ko",
    "drivers/perf/arm_spe_pmu.ko",
    "drivers/scsi/sg.ko",
    "drivers/spi/spi-dw.ko",
    "drivers/spi/spi-loopback-test.ko",
    "drivers/watchdog/softdog.ko",
]
