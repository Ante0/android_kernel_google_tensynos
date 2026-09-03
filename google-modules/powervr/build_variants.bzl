# SPDX-License-Identifier: GPL-2.0-or-later
"""This module defines build variants for PowerVR."""

load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

def define_build_variants(name = ""):
    """Defines build variants using flags and config_settings.

    This function sets up flags for SoC selection and bringup/production modes,
    along with corresponding config_settings to enable conditional builds.

    Args:
      name: Name of the macro. By convention, every public macro needs a "name"
            argument (even if it doesn't use it).
    """

    # Set this flag to True for bring-up mode
    bool_flag(
        name = "powervr_bringup_mode_flag",
        build_setting_default = False,
    )

    # Settings for bringup/production modes

    native.config_setting(
        name = "powervr_bringup_mode",
        flag_values = {
            ":powervr_bringup_mode_flag": "true",
        },
    )

    native.config_setting(
        name = "powervr_production_mode",
        flag_values = {
            ":powervr_bringup_mode_flag": "false",
        },
    )

    # Combined settings for SoC + mode

    native.config_setting(
        name = "rdo_bringup",
        flag_values = {
            "//private/devices/google/common:soc": "rdo",
            ":powervr_bringup_mode_flag": "true",
        },
    )

    native.config_setting(
        name = "rdo_production",
        flag_values = {
            "//private/devices/google/common:soc": "rdo",
            ":powervr_bringup_mode_flag": "false",
        },
    )

    native.config_setting(
        name = "lga_bringup",
        flag_values = {
            "//private/devices/google/common:soc": "laguna",
            ":powervr_bringup_mode_flag": "true",
        },
    )

    native.config_setting(
        name = "lga_production",
        flag_values = {
            "//private/devices/google/common:soc": "laguna",
            ":powervr_bringup_mode_flag": "false",
        },
    )

    native.config_setting(
        name = "mbu_bringup",
        flag_values = {
            "//private/devices/google/common:soc": "malibu",
            ":powervr_bringup_mode_flag": "true",
        },
    )

    native.config_setting(
        name = "mbu_production",
        flag_values = {
            "//private/devices/google/common:soc": "malibu",
            ":powervr_bringup_mode_flag": "false",
        },
    )
