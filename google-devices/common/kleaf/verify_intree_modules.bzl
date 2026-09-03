# SPDX-License-Identifier: GPL-2.0-only

"""
Rule to verify that all .ko files from kernel_build are present in intree_modules.
"""

load("@bazel_skylib//lib:new_sets.bzl", "sets")

def _verify_intree_modules_impl(ctx):
    missing_files = sets.to_list(sets.difference(
        sets.make([f for f in ctx.files.kernel_build if f.extension == "ko"]),
        sets.make([f for f in ctx.files.intree_modules if f.extension == "ko"]),
    ))

    if missing_files:
        msg = "\nThe following intree modules are built but are not in any module lists:\n"
        msg += "\n".join(["  {}".format(f.path) for f in missing_files])
        fail(msg)

    return []

verify_intree_modules = rule(
    implementation = _verify_intree_modules_impl,
    attrs = {
        "kernel_build": attr.label(
            doc = "The kernel_build target to extract .ko files from.",
            allow_files = True,
        ),
        "intree_modules": attr.label(
            doc = "The target_intree_modules filegroup.",
            allow_files = True,
        ),
    },
)
