# SPDX-License-Identifier: GPL-2.0-only

"""Helper function to get the path relative to the package of a file"""

load("@bazel_skylib//lib:paths.bzl", "paths")

def path_relative_to_package(file):
    return paths.relativize(
        file.short_path,
        paths.join(
            paths.join("..", file.owner.workspace_name) if file.owner.workspace_name else "",
            file.owner.package,
        ),
    )
