# SPDX-License-Identifier: GPL-2.0-only

"""
Rule to generate modules list or blocklist from module targets.
"""

def _modules_list_impl(ctx):
    out = ctx.outputs.out
    if not out:
        out = ctx.actions.declare_file(ctx.label.name)

    lines = []
    for f in ctx.files.modules:
        if f.extension != "ko":
            continue
        basename = f.basename

        if ctx.attr.format == "list":
            lines.append(basename)
        elif ctx.attr.format == "blocklist":
            module_name = basename[:-3].replace("-", "_")
            lines.append("blocklist " + module_name)

    ctx.actions.write(
        output = out,
        content = "\n".join(lines) + "\n",
    )
    return [DefaultInfo(files = depset([out]))]

modules_list = rule(
    implementation = _modules_list_impl,
    doc = "Generate modules list or blocklist from module targets.",
    attrs = {
        "modules": attr.label_list(
            doc = "List of module targets to extract .ko files from.",
            allow_files = True,
        ),
        "format": attr.string(
            doc = "Output format: 'list' (default) or 'blocklist'.",
            default = "list",
            values = ["list", "blocklist"],
        ),
        "out": attr.output(
            doc = "The output file. If omitted, defaults to the target name.",
        ),
    },
)
