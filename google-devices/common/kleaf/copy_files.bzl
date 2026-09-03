# SPDX-License-Identifier: GPL-2.0-only

"""
Copy and optionally rename the files.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@kleaf//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")
load(":glob_match.bzl", "glob_match_any")
load(":path_relative_to_package.bzl", "path_relative_to_package")

# For strip_prefix, strip all directories.
STRIP_PREFIX_FILES_ONLY = "."

def _strip_prefix(path, strip_prefix):
    if not strip_prefix:
        return path

    if strip_prefix == STRIP_PREFIX_FILES_ONLY:
        return paths.basename(path)

    path_norm = paths.normalize(path)
    strip_prefix_norm = paths.normalize(strip_prefix) + "/"

    if path_norm.startswith(strip_prefix_norm):
        return path_norm[len(strip_prefix_norm):]
    return path

def _rename(path, renames):
    if path in renames:
        return renames[path]
    return path

def _copy_files_impl(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)
    srcs = ctx.files.srcs

    include = ctx.attr.include
    exclude = ctx.attr.exclude
    strip_prefix = ctx.attr.strip_prefix.strip("/")
    prefix = ctx.attr.prefix.strip("/")
    renames = {k.strip("/"): v.strip("/") for k, v in ctx.attr.renames.items()}
    allow_empty = ctx.attr.allow_empty

    dst_src_map = {}
    for src in srcs:
        path = path_relative_to_package(src)

        if include and not glob_match_any(path, include):
            continue
        if exclude and glob_match_any(path, exclude):
            continue

        path = _strip_prefix(path, strip_prefix)
        path = _rename(path, renames)
        path = paths.join(prefix, path)

        dst = ctx.actions.declare_file(path)
        if dst in dst_src_map:
            fail("The following sources have the same destination: {}.\n  {}\n  {}.".format(
                path,
                src.path,
                dst_src_map[dst].path,
            ))
        dst_src_map[dst] = src

        command = hermetic_tools.setup + """
            cp -aL "{src}" "{dst}"
        """.format(
            src = src.path,
            dst = dst.path,
        )

        ctx.actions.run_shell(
            mnemonic = "CopyFile",
            inputs = [src],
            tools = hermetic_tools.deps,
            outputs = [dst],
            command = command,
        )

    if not dst_src_map and not allow_empty:
        fail("No files copied for {}".format(ctx.label))

    return [DefaultInfo(files = depset(dst_src_map.keys()))]

copy_files = rule(
    implementation = _copy_files_impl,
    doc = """Copy and optionally rename the files.

    Files in `srcs` are first filtered using `include` and `exclude` glob patterns.
    A file is processed if it matches at least one pattern in `include` (or if
    `include` is empty) and does not match any pattern in `exclude`.

    The output files' paths remain the same as in their source package.
    The following path modifications are applied in order:

    1) Remove `strip_prefix` from the path.
    2) Rename the path according to `renames`.
    3) Add `prefix` to the path.

    Example:
    ```
    filegroup(
        name = "source",
        srcs = [
            "src/foo.txt",
            "src/bar.txt",
        ],
    )

    copy_files(
        name = "copy_source",
        srcs = [":source"],
        prefix = "dst",
        renames = {
            "foo.txt": "baz.txt",
        },
        strip_prefix = "src",
    )
    ```
    The above rule will generate the following files:
    ```
    dst/baz.txt
    dst/bar.txt
    ```
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
        "strip_prefix": attr.string(
            doc = """Prefix to remove from the incoming files' paths

            Use STRIP_PREFIX_FILES_ONLY to strip all directories from the paths.
            """,
        ),
        "prefix": attr.string(
            doc = "Prefix to add to all output files' paths",
        ),
        "renames": attr.string_dict(
            doc = """Files to rename.

            Keys are the file paths after applying `strip_prefix`.
            Values are the file paths before applying `prefix`.
            """,
        ),
        "allow_empty": attr.bool(
            doc = "Whether to allow empty copy. If False, fails when no files are copied.",
            default = False,
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
