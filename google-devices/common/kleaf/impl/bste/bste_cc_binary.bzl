# SPDX-License-Identifier: GPL-2.0-only

"""Implements the bste_cc_binary macro."""

load("@rules_cc//cc:defs.bzl", "cc_binary")
load(":bste_prebuilt_binary.bzl", "bste_prebuilt_binary")

def bste_cc_binary(name, **kwargs):
    """Wraps cc_binary to produce static binaries for the target device.

    Args:
        name: The name of the target.
        **kwargs: Additional arguments forwarded to the internal cc_binary.
    """
    internal_name = name + "_internal"

    visibility = kwargs.pop("visibility", None)
    tags = kwargs.get("tags", [])

    # Force fully static link
    kwargs["linkstatic"] = True
    features = list(kwargs.get("features", []))
    if "fully_static_link" not in features:
        features.append("fully_static_link")
    kwargs["features"] = features

    cc_binary(
        name = internal_name,
        visibility = ["//visibility:private"],
        **kwargs
    )

    bste_prebuilt_binary(
        name = name,
        src = ":" + internal_name,
        visibility = visibility,
        tags = tags,
    )
