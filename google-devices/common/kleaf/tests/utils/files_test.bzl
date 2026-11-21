# SPDX-License-Identifier: GPL-2.0-only

"""Check files of a target."""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load(
    "//private/devices/google/common/kleaf:path_relative_to_package.bzl",
    "path_relative_to_package",
)

def _files_test_impl(ctx):
    env = analysistest.begin(ctx)
    target_under_test = analysistest.target_under_test(env)

    files = sorted([
        path_relative_to_package(f)
        for f in target_under_test.files.to_list()
    ])
    expected_files = sorted(ctx.attr.expected_files)

    asserts.equals(env, expected_files, files)

    return analysistest.end(env)

files_test = analysistest.make(
    _files_test_impl,
    attrs = {
        "expected_files": attr.string_list(
            doc = "Paths of expected files in the target, relative to the package",
            mandatory = True,
        ),
    },
)
