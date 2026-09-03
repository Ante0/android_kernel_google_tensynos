# SPDX-License-Identifier: GPL-2.0-only

"""Tests for select_files rule"""

load("//private/devices/google/common/kleaf:select_files.bzl", "select_files")
load(":utils/failure_test.bzl", "failure_test")
load(":utils/files_test.bzl", "files_test")

def select_files_test(name):
    """Test select_files rule.

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

    select_files(
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

    select_files(
        name = "{}/include".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        include = ["**/file1.txt", "**/dir4/*"],
    )

    files_test(
        name = "{}/include_test".format(name),
        target_under_test = "{}/include".format(name),
        expected_files = [
            "data/filegroup_1/file1.txt",
            "data/filegroup_2/dir4/file4.txt",
        ],
        size = "small",
    )
    tests.append("{}/include_test".format(name))

    select_files(
        name = "{}/exclude".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        exclude = ["**/file2.txt", "**/file3.txt"],
    )

    files_test(
        name = "{}/exclude_test".format(name),
        target_under_test = "{}/exclude".format(name),
        expected_files = [
            "data/filegroup_1/file1.txt",
            "data/filegroup_2/dir4/file4.txt",
        ],
        size = "small",
    )
    tests.append("{}/exclude_test".format(name))

    select_files(
        name = "{}/include_exclude".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
            "{}/filegroup_2".format(name),
        ],
        include = ["**/*.txt"],
        exclude = ["**/dir*/*"],
    )

    files_test(
        name = "{}/include_exclude_test".format(name),
        target_under_test = "{}/include_exclude".format(name),
        expected_files = [
            "data/filegroup_1/file1.txt",
            "data/filegroup_2/file3.txt",
        ],
        size = "small",
    )
    tests.append("{}/include_exclude_test".format(name))

    select_files(
        name = "{}/empty_allow".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
        ],
        include = ["non_existent_file"],
        allow_empty = True,
    )

    files_test(
        name = "{}/empty_allow_test".format(name),
        target_under_test = "{}/empty_allow".format(name),
        expected_files = [],
        size = "small",
    )
    tests.append("{}/empty_allow_test".format(name))

    select_files(
        name = "{}/empty_fail".format(name),
        srcs = [
            "{}/filegroup_1".format(name),
        ],
        include = ["non_existent_file"],
    )

    failure_test(
        name = "{}/empty_fail_test".format(name),
        target_under_test = "{}/empty_fail".format(name),
        size = "small",
    )
    tests.append("{}/empty_fail_test".format(name))

    native.test_suite(
        name = name,
        tests = tests,
    )
