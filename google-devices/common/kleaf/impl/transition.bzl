# SPDX-License-Identifier: GPL-2.0-only

"""Implements transitions for BSTE."""

def _device_platform_transition_impl(_settings, _attr):
    """Sets the target platform to device platform."""
    return {"//command_line_option:platforms": ["@kleaf//build/kernel/kleaf/impl:android_arm64"]}

device_platform_transition = transition(
    implementation = _device_platform_transition_impl,
    inputs = [],
    outputs = ["//command_line_option:platforms"],
)
