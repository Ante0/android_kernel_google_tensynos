# SPDX-License-Identifier: GPL-2.0-only

"""
Common compiler flags for mipi-dsi2h module.
"""

DW_MIPI_DSI2H_COPTS = [
    "-Wall",
    "-Werror",
    "-Wextra",
    "-Wunused",
    "-ferror-limit=0",
    "-Wold-style-definition",
    "-Wframe-larger-than=4096",
    "-Wdisabled-optimization",
    "-Wmissing-prototypes",
    "-Wmissing-declarations",
    "-Wmissing-field-initializers",
    "-Wmissing-include-dirs",
    "-Wcast-function-type",

    # Keep it around to detect unused #define
    #"-Wunused-macros",
    "-Wunused-but-set-variable",
    "-Wunused-but-set-parameter",
    "-Wunused-value",
    "-Wunused-result",
    "-Wunused-const-variable",
    "-Wsometimes-uninitialized",
    "-Wimplicit-fallthrough",
    "-Wformat-security",
    "-Wthread-safety",

    # Disable these legacy flags
    "-Wno-sign-compare",
    "-Wno-shift-negative-value",
    "-Wno-array-bounds",
    "-Wno-type-limits",
    "-Wno-unused-parameter",
]
