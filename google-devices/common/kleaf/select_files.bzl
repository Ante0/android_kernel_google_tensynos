# SPDX-License-Identifier: GPL-2.0-only

"""
Select files from srcs.
"""

load(":glob_match.bzl", "glob_match_any")
load(":path_relative_to_package.bzl", "path_relative_to_package")

def _select_files_impl(ctx):
    srcs = ctx.files.srcs
    include = ctx.attr.include
    exclude = ctx.attr.exclude
    allow_empty = ctx.attr.allow_empty

    selected = []
    for src in srcs:
        path = path_relative_to_package(src)

        if include and not glob_match_any(path, include):
            continue
        if exclude and glob_match_any(path, exclude):
            continue

        selected.append(src)

    if not selected and not allow_empty:
        fail("No files selected for {}".format(ctx.label))

    return [DefaultInfo(files = depset(selected))]

select_files = rule(
    implementation = _select_files_impl,
    doc = """Select files from srcs.

    Files in `srcs` are filtered using `include` and `exclude` glob patterns.
    A file is selected if it matches at least one pattern in `include` (or if
    `include` is empty) and does not match any pattern in `exclude`.
    """,
    attrs = {
        "srcs": attr.label_list(
            doc = "List of source files.",
            allow_files = True,
        ),
        "include": attr.string_list(
            doc = "List of glob patterns to include. If empty, all files are included.",
            default = [],
        ),
        "exclude": attr.string_list(
            doc = "List of glob patterns to exclude. Exclusions take precedence over inclusions.",
            default = [],
        ),
        "allow_empty": attr.bool(
            doc = "Whether to allow empty selection. If False, fails when no files are selected.",
            default = False,
        ),
    },
)
