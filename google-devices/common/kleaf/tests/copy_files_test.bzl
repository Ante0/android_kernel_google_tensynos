# SPDX-License-Identifier: GPL-2.0-only

"""Tests for copy_files rule"""

load("//private/devices/google/common/kleaf:copy_files.bzl", "STRIP_PREFIX_FILES_ONLY", "copy_files")
load(":utils/failure_test.bzl", "failure_test")
load(":utils/files_test.bzl", "files_test")

def copy_files_test(name):
    """Test copy_files rule.

    Args:
        name: Name.
    """
    tests = []

    native.filegroup(
        name = "{}/filegroup_1".format(name),
        srcs = native.glob(["data/filegroup_1/**/*"]),
    )

    native.filegroup(
        name = "{}/filegroup_2".format(name),
        srcs = native.glob(["data/filegroup_2/**/*"]),
    )

    copy_files(
        name = "{}/default".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
    )

    files_test(
        name = "{}/default_test".format(name),
        target_under_test = "{}/default".format(name),
        expected_files = [
            "data/filegroup_1/file1.txt",
            "data/filegroup_1/dir2/file2.txt",
            "data/filegroup_2/file3.txt",
            "data/filegroup_2/dir4/file4.txt",
        ],
        size = "small",
    )
    tests.append("{}/default_test".format(name))

    copy_files(
        name = "{}/prefix".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        prefix = "prefix",
    )

    files_test(
        name = "{}/prefix_test".format(name),
        target_under_test = "{}/prefix".format(name),
        expected_files = [
            "prefix/data/filegroup_1/file1.txt",
            "prefix/data/filegroup_1/dir2/file2.txt",
            "prefix/data/filegroup_2/file3.txt",
            "prefix/data/filegroup_2/dir4/file4.txt",
        ],
        size = "small",
    )
    tests.append("{}/prefix_test".format(name))

    copy_files(
        name = "{}/strip_prefix".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        prefix = "strip_prefix",
        strip_prefix = "data/filegroup_1",
    )

    files_test(
        name = "{}/strip_prefix_test".format(name),
        target_under_test = "{}/strip_prefix".format(name),
        expected_files = [
            "strip_prefix/file1.txt",
            "strip_prefix/dir2/file2.txt",
            "strip_prefix/data/filegroup_2/file3.txt",
            "strip_prefix/data/filegroup_2/dir4/file4.txt",
        ],
        size = "small",
    )
    tests.append("{}/strip_prefix_test".format(name))

    copy_files(
        name = "{}/files_only".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        prefix = "files_only",
        strip_prefix = STRIP_PREFIX_FILES_ONLY,
    )

    files_test(
        name = "{}/files_only_test".format(name),
        target_under_test = "{}/files_only".format(name),
        expected_files = [
            "files_only/file1.txt",
            "files_only/file2.txt",
            "files_only/file3.txt",
            "files_only/file4.txt",
        ],
        size = "small",
    )
    tests.append("{}/files_only_test".format(name))

    copy_files(
        name = "{}/renames".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        prefix = "renames",
        renames = {
            "file1.txt": "renamed_file1.txt",
            "data/filegroup_2/file3.txt": "data/filegroup_2/renamed_file3.txt",
        },
        strip_prefix = "data/filegroup_1",
    )

    files_test(
        name = "{}/renames_test".format(name),
        target_under_test = "{}/renames".format(name),
        expected_files = [
            "renames/renamed_file1.txt",
            "renames/dir2/file2.txt",
            "renames/data/filegroup_2/renamed_file3.txt",
            "renames/data/filegroup_2/dir4/file4.txt",
        ],
        size = "small",
    )
    tests.append("{}/renames_test".format(name))

    copy_files(
        name = "{}/conflict".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        prefix = "conflict",
        renames = {
            "data/filegroup_2/file3.txt": "file1.txt",
        },
        strip_prefix = "data/filegroup_1",
    )

    failure_test(
        name = "{}/conflict_test".format(name),
        target_under_test = "{}/conflict".format(name),
        size = "small",
    )
    tests.append("{}/conflict_test".format(name))

    native.test_suite(
        name = name,
        tests = tests,
    )
