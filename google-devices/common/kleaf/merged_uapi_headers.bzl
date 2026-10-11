# SPDX-License-Identifier: GPL-2.0-only

"""
Merges the UAPI headers from merged_kernel_uapi_headers and ddk_uapi_headers.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@kernel_toolchain_info//:dict.bzl", "CLANG_VERSION")
load("//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")

def _merged_uapi_headers_impl(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)
    out_file = (ctx.outputs.out or
                ctx.actions.declare_file("{}/kernel-uapi-headers.tar.gz".format(ctx.attr.name)))
    inputs = []
    outputs = [out_file]
    tools = [hermetic_tools.deps]

    if not out_file.path.endswith(".tar.gz"):
        fail("out filename must end with \".tar.gz\"")

    for f in reversed(ctx.files.uapi_headers):
        if f.basename.endswith(".tar.gz"):
            inputs.append(f)

    intermediates_dir = paths.join(
        ctx.bin_dir.path,
        paths.dirname(ctx.build_file_path),
        ctx.attr.name + "_intermediates",
    )

    command = hermetic_tools.setup
    command += """
        # Extract all UAPI headers
        mkdir -p {intermediates_dir}/usr

        all_uapi_headers_archives=({all_uapi_headers_archives})

        # Unpack and repack all archives to combine them
        for archive in "${{all_uapi_headers_archives[@]}}"; do
            tar xf ${{archive}} -C {intermediates_dir}
        done
    """.format(
        intermediates_dir = intermediates_dir,
        all_uapi_headers_archives = " ".join([archive.path for archive in inputs]),
    )

    if ctx.attr.clean:
        command += """
            export ANDROID_BUILD_TOP=.
            export PYTHONNOUSERSITE=1
            mkdir -p prebuilts/clang/host/linux-x86/clang-stable
            ln -s $(realpath {clang_lib_dir}) prebuilts/clang/host/linux-x86/clang-stable/lib
            for f in $(find {intermediates_dir} -type f -printf '%P\n'); do
                {python3} {clean_header_py} -u -k {intermediates_dir} -d {intermediates_dir} ${{f}}
            done
        """.format(
            intermediates_dir = intermediates_dir,
            python3 = ctx.attr._clang_python3[PyRuntimeInfo].interpreter.path,
            clean_header_py = ctx.file._clean_header_py.path,
            clang_lib_dir = paths.join(
                ctx.attr._clang_binaries.label.workspace_root,
                ctx.attr._clang_binaries.label.package,
                "lib",
            ),
        )

        tools += [
            ctx.attr._clang_binaries.files,
            ctx.attr._clang_python3[PyRuntimeInfo].files,
            ctx.attr._clean_header_deps.files,
            ctx.attr._clean_header_py.files,
        ]

    command += """
        tar czf {out_name} -C {intermediates_dir} usr
    """.format(
        intermediates_dir = intermediates_dir,
        out_name = out_file.path,
    )

    ctx.actions.run_shell(
        mnemonic = "MergedUAPIHeaders",
        inputs = inputs,
        outputs = outputs,
        tools = tools,
        progress_message = "Merging UAPI headers",
        command = command,
    )

    return [DefaultInfo(files = depset(outputs))]

merged_uapi_headers = rule(
    implementation = _merged_uapi_headers_impl,
    doc = """Merges the UAPI headers from merged_kernel_uapi_headers or ddk_uapi_headers.

If there are conflicts of file names in the source tarballs, files higher in
the uapi_headers list have higher priority.
""",
    attrs = {
        "uapi_headers": attr.label_list(
            doc = "A list of labels referring to uapi headers, including merged_kernel_uapi_headers() or ddk_uapi_headers() targets.",
            allow_files = True,
            mandatory = True,
        ),
        "out": attr.output(
            doc = "Output file, defaults to {name}/kernel-uapi-headers.tar.gz",
        ),
        "clean": attr.bool(
            doc = "Whether to clean headers",
            default = False,
        ),
        "_clean_header_py": attr.label(
            default = "@bionic//:libc/kernel/tools/clean_header_py",
            allow_single_file = True,
        ),
        "_clean_header_deps": attr.label(
            default = "@bionic//:libc/kernel/tools/clean_header_deps",
            allow_files = True,
        ),
        # HACK: Need to load CLANG_VERSION from kernel_toolchain_info() to find out the python3
        # runtime. Don't do this elsewhere!
        "_clang_binaries": attr.label(
            default = "//prebuilts/clang/host/linux-x86/clang-{}:binaries".format(
                CLANG_VERSION,
            ),
        ),
        "_clang_python3": attr.label(
            default = "//prebuilts/clang/host/linux-x86/clang-{}:python3".format(
                CLANG_VERSION,
            ),
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
