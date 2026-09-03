# SPDX-License-Identifier: GPL-2.0-only

"""Tests for checkpatch_strict rule"""

load(
    "//private/devices/google/common/kleaf:checkpatch.bzl",
    "CHECKPATCH_DEFAULT_IGNORES",
    "checkpatch_strict",
)
load(":utils/file_content_test.bzl", "file_content_test")

def checkpatch_strict_test(name):
    """Test checkpatch_strict rule.

    Args:
        name: Name.
    """
    tests = []

    checkpatch_strict(
        name = "{}_test_default".format(name),
    )

    file_content_test(
        name = "{}_test_default_ignores".format(name),
        actual = "{}_test_default_ignorelist.txt".format(name),
        expected_content = "\n".join(CHECKPATCH_DEFAULT_IGNORES) + "\n",
        size = "small",
    )
    tests.append("{}_test_default_ignores".format(name))

    checkpatch_strict(
        name = "{}_test_custom".format(name),
        ignores = ["CUSTOM_IGNORE"],
    )

    file_content_test(
        name = "{}_test_custom_ignores".format(name),
        actual = "{}_test_custom_ignorelist.txt".format(name),
        expected_content = "\n".join(CHECKPATCH_DEFAULT_IGNORES + ["CUSTOM_IGNORE"]) + "\n",
        size = "small",
    )
    tests.append("{}_test_custom_ignores".format(name))

    native.test_suite(
        name = name,
        tests = tests,
    )
