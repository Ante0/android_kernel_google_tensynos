# SPDX-License-Identifier: GPL-2.0-only

"""Implements the bste_test_package rule for custom, flexible packaging."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@kleaf//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")
load("//private/devices/google/common/kleaf:path_relative_to_package.bzl", "path_relative_to_package")

def _bste_test_package_impl(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)
    out_file = ctx.actions.declare_file(ctx.attr.name + ".tar.gz")

    # map for source to ultimate archive destination path
    copy_map = {}

    # 1. Process Harness Container and auto-discovered dependencies
    for f in ctx.attr._harness[DefaultInfo].default_runfiles.files.to_list():
        copy_map[path_relative_to_package(f)] = f

    # 2. Test Suites (Accepts bste_test_suite targets or filegroups aggregating them)
    for suite in ctx.attr.test_suites:
        for f in suite[DefaultInfo].files.to_list():
            rel_path = path_relative_to_package(f)
            archive_path = paths.join("test_suites", rel_path)
            if archive_path in copy_map:
                fail("Test Suite Name Collision: duplicate file detected at '{}'".format(archive_path))
            copy_map[archive_path] = f

    # Assemble commands
    cmds = [
        hermetic_tools.setup,
        "set -e",
        # Create temporary build staging tree
        "STAGING=$(mktemp -d)",
        "trap \"rm -rf '$STAGING'\" EXIT",
    ]

    all_inputs = []
    for dest, src_f in copy_map.items():
        all_inputs.append(src_f)
        cmds.append("mkdir -p \"$STAGING/$(dirname '{}')\"".format(dest))

        # Use symlinks; tar -h will resolve them and natively inherit attributes
        cmds.append("ln -sf \"$PWD/{}\" \"$STAGING/{}\"".format(src_f.path, dest))

    # Archive and Compress
    cmds.append("tar -czhf \"{}\" -C \"$STAGING\" .".format(out_file.path))

    ctx.actions.run_shell(
        mnemonic = "BsteCreateTar",
        inputs = depset(all_inputs),
        outputs = [out_file],
        tools = hermetic_tools.deps,
        command = "\n".join(cmds),
        progress_message = "Bundling custom relocatable BSTE package {}".format(ctx.label),
    )

    return [DefaultInfo(files = depset([out_file]))]

bste_test_package = rule(
    implementation = _bste_test_package_impl,
    doc = """Aggregates test suites and the harness logic into a relocatable package.""",
    attrs = {
        "test_suites": attr.label_list(
            mandatory = True,
            doc = "List of test suite targets (or filegroups aggregating them) to aggregate.",
        ),
        "_harness": attr.label(
            default = "//private/devices/google/common/kleaf/impl/bste:bste_test",
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
