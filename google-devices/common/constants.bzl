# SPDX-License-Identifier: GPL-2.0-only

"""
Common Pixel constants.
"""

load("//common:modules.bzl", "get_gki_modules_list")

PIXEL_GKI_MODULES_LIST = [
    m
    for m in get_gki_modules_list("arm64")
    # List of GKI modules to omit
    if m not in [
        # keep sorted
        "drivers/block/zram/zram.ko",
    ]
]
