# SPDX-License-Identifier: GPL-2.0-only

"""Tests for create_file rule"""

load("//private/devices/google/common/kleaf:create_file.bzl", "create_file")
load(":utils/file_content_test.bzl", "file_content_test")

def create_file_test(name):
    """Test create_file rule.

    Args:
        name: Name.
    """
    tests = []

    create_file(
        name = name + "_file1",
        contents = ["text", "content"],
        out = name + "_file1.txt",
    )

    file_content_test(
        name = name + "_text_content_test",
        actual = name + "_file1",
        expected_content = "text\ncontent\n",
        size = "small",
    )
    tests.append(name + "_text_content_test")

    create_file(
        name = name + "_file2",
        contents = ["raw", "content"],
        out = name + "_file2.bin",
        type = "raw",
    )

    file_content_test(
        name = name + "_raw_content_test",
        actual = name + "_file2",
        expected_content = "rawcontent",
        size = "small",
    )
    tests.append(name + "_raw_content_test")

    create_file(
        name = name + "_file3",
        srcs = [
            name + "_file1",
            name + "_file2",
        ],
        out = name + "_file3.txt",
    )

    file_content_test(
        name = name + "_text_file_test",
        actual = name + "_file3",
        expected_content = "text\ncontent\n\nrawcontent\n",
        size = "small",
    )
    tests.append(name + "_text_file_test")

    create_file(
        name = name + "_file4",
        srcs = [
            name + "_file1",
            name + "_file2",
        ],
        out = name + "_file4.bin",
        type = "raw",
    )

    file_content_test(
        name = name + "_raw_file_test",
        actual = name + "_file4",
        expected_content = "text\ncontent\nrawcontent",
        size = "small",
    )
    tests.append(name + "_raw_file_test")

    create_file(
        name = name + "_file5",
        srcs = [
            name + "_file1",
            name + "_file2",
        ],
        contents = ["new"],
        out = name + "_file5.txt",
    )

    file_content_test(
        name = name + "_text_file_and_content_test",
        actual = name + "_file5",
        expected_content = "text\ncontent\n\nrawcontent\nnew\n",
        size = "small",
    )
    tests.append(name + "_text_file_and_content_test")

    create_file(
        name = name + "_file6",
        srcs = [
            name + "_file1",
            name + "_file2",
        ],
        contents = ["new"],
        out = name + "_file6.bin",
        type = "raw",
    )

    file_content_test(
        name = name + "_raw_file_and_content_test",
        actual = name + "_file6",
        expected_content = "text\ncontent\nrawcontentnew",
        size = "small",
    )
    tests.append(name + "_raw_file_and_content_test")

    create_file(
        name = name + "_empty",
        out = "_empty.txt",
    )

    file_content_test(
        name = name + "_empty_file_test",
        actual = name + "_empty",
        expected_content = "",
        size = "small",
    )
    tests.append(name + "_empty_file_test")

    native.test_suite(
        name = name,
        tests = tests,
    )
