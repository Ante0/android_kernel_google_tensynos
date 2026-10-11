# SPDX-License-Identifier: GPL-2.0-only

"""
Create a file by concatenating source files and appending contents.
"""

load("//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")

def _create_file_impl(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)
    srcs = list(ctx.files.srcs)
    out = ctx.outputs.out

    sep = ""
    if ctx.attr.type == "text":
        sep = "\n"

    if ctx.attr.contents:
        contents_file = ctx.actions.declare_file(ctx.attr.name + "/contents")
        ctx.actions.write(
            output = contents_file,
            content = sep.join(ctx.attr.contents),
        )
        srcs.append(contents_file)

    command = hermetic_tools.setup

    # Create the file even when there is no srcs or contents.
    command += """
        : > "{out}"
    """.format(out = out.path)

    # Add newlines between source files.
    command += """
        echo -n "{sep}" >> "{out}"
    """.format(sep = sep, out = out.path).join([
        """
        cat "{src}" >> "{out}"
        """.format(src = src.path, out = out.path)
        for src in srcs
    ])

    if ctx.attr.type == "text":
        # Ensure the file ends with a newline.
        command += """
        test -z "$(tail -c 1 "{out}")" || echo "" >> "{out}"
        """.format(out = out.path)

    ctx.actions.run_shell(
        mnemonic = "CreateFile",
        inputs = srcs,
        tools = hermetic_tools.deps,
        outputs = [out],
        command = command,
    )

    return [DefaultInfo(files = depset([out]))]

create_file = rule(
    implementation = _create_file_impl,
    doc = "Create a file by concatenating source files and appending contents",
    attrs = {
        "out": attr.output(
            doc = "Path of the output file, relative to this package.",
            mandatory = True,
        ),
        "srcs": attr.label_list(
            doc = "List of source files which will be concatenated to the output file.",
            allow_files = True,
        ),
        "contents": attr.string_list(
            doc = "List of strings which will be appended to the output file after `srcs`.",
        ),
        "type": attr.string(
            doc = (
                "The type of this file. Default: text\n" +
                "  text: Add newlines between each srcs and contents and at the end of the file.\n" +
                "  raw: Concatenate srcs and contents directly.\n"
            ),
            values = ["text", "raw"],
            default = "text",
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
