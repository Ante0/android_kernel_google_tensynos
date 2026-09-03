# SPDX-License-Identifier: GPL-2.0-only

"""Implements the bste_prebuilt_binary rule."""

load("//private/devices/google/common/kleaf/impl:transition.bzl", "device_platform_transition")

def _bste_prebuilt_binary_impl(ctx):
    src_file = ctx.file.src
    out_file = ctx.actions.declare_file(ctx.attr.name)

    # Copy source and set executable bit
    ctx.actions.run_shell(
        inputs = [src_file],
        outputs = [out_file],
        command = "cp -L \"$1\" \"$2\" && chmod +x \"$2\"",
        arguments = [src_file.path, out_file.path],
        mnemonic = "BstePrebuiltRename",
        progress_message = "Formatting prebuilt binary for {}".format(ctx.label),
    )

    # Inherit runfiles from src, but explicitly exclude the original binary itself
    src_runfiles = ctx.attr.src[DefaultInfo].default_runfiles.files.to_list()
    filtered_runfiles = [f for f in src_runfiles if f != src_file]

    runfiles = ctx.runfiles(
        files = [out_file] + ctx.files.data + filtered_runfiles,
    )

    # Merge other transitive runfiles from data deps
    runfiles = runfiles.merge_all([
        t[DefaultInfo].default_runfiles
        for t in ctx.attr.data
    ])

    return [
        DefaultInfo(
            files = depset([out_file]),
            runfiles = runfiles,
            executable = out_file,
        ),
    ]

bste_prebuilt_binary = rule(
    implementation = _bste_prebuilt_binary_impl,
    doc = "Includes existing binaries or scripts into the test environment.",
    executable = True,
    attrs = {
        "src": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = "Source file.",
        ),
        "data": attr.label_list(
            allow_files = True,
            doc = "Runtime dependencies.",
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
    cfg = device_platform_transition,
)
