# SPDX-License-Identifier: GPL-2.0-only

"""
Tangorpro constants.
"""

TANGORPRO_DTBOS = [
    "gs201-tangorpro-dev2.dtbo",
    "gs201-tangorpro-evt2.dtbo",
    "gs201-tangorpro-evt2-bezelblack.dtbo",
    "gs201-tangorpro-evt2-wingboard.dtbo",
    "gs201-tangorpro-dvt1.dtbo",
    "gs201-tangorpro-pvt1.dtbo",
    "gs201-tangorpro-mp.dtbo",
]

TANGORPRO_MODULE_OUTS = [
    # keep sorted
    "drivers/leds/leds-pwm.ko",
    "drivers/regulator/rt4801-regulator.ko",
    "drivers/regulator/tps65132-regulator.ko",
    "drivers/video/backlight/lp855x_bl.ko",
]
