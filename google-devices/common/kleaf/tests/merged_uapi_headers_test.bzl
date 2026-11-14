# SPDX-License-Identifier: GPL-2.0-only

"""Tests for merged_uapi_headers rule"""

load("//build/kernel/kleaf:kernel.bzl", "ddk_uapi_headers")
load("//private/devices/google/common/kleaf:merged_uapi_headers.bzl", "merged_uapi_headers")
load(":utils/failure_test.bzl", "failure_test")
load(":utils/file_content_test.bzl", "file_content_test")
load(":utils/files_test.bzl", "files_test")

def merged_uapi_headers_test(name):
    """Test merged_uapi_headers rule.

    Args:
        name: Name.
    """
    tests = []

    ddk_uapi_headers(
        name = "{}/uapi_headers_1".format(name),
        srcs = native.glob(["data/uapi_headers_1/**/*.h"]),
        out = "uapi_headers_1.tar.gz",
        kernel_build = "//common/ack:kernel_aarch64",
    )

    ddk_uapi_headers(
        name = "{}/uapi_headers_2".format(name),
        srcs = native.glob(["data/uapi_headers_2/**/*.h"]),
        out = "uapi_headers_2.tar.gz",
        kernel_build = "//common/ack:kernel_aarch64",
    )

    ddk_uapi_headers(
        name = "{}/uapi_headers_expected".format(name),
        srcs = native.glob(["data/uapi_headers_expected/**/*.h"]),
        out = "uapi_headers_expected.tar.gz",
        kernel_build = "//common/ack:kernel_aarch64",
    )

    merged_uapi_headers(
        name = "{}/merged_uapi_headers".format(name),
        uapi_headers = [
            "{}/uapi_headers_1".format(name),
            "{}/uapi_headers_2".format(name),
        ],
    )

    file_content_test(
        name = "{}/merge_test".format(name),
        actual = "{}/merged_uapi_headers".format(name),
        expected = "{}/uapi_headers_expected".format(name),
        size = "small",
    )
    tests.append("{}/merge_test".format(name))

    ddk_uapi_headers(
        name = "{}/cleaned_uapi_headers_expected".format(name),
        srcs = native.glob(["data/cleaned_uapi_headers_expected/**/*.h"]),
        out = "cleaned_uapi_headers_expected.tar.gz",
        kernel_build = "//common/ack:kernel_aarch64",
    )

    merged_uapi_headers(
        name = "{}/cleaned_uapi_headers".format(name),
        clean = True,
        uapi_headers = [
            "{}/uapi_headers_1".format(name),
            "{}/uapi_headers_2".format(name),
        ],
    )

    file_content_test(
        name = "{}/clean_test".format(name),
        actual = "{}/cleaned_uapi_headers".format(name),
        expected = "{}/cleaned_uapi_headers_expected".format(name),
        size = "small",
    )
    tests.append("{}/clean_test".format(name))

    files_test(
        name = "{}/default_out_test".format(name),
        target_under_test = "{}/merged_uapi_headers".format(name),
        expected_files = [
            "{}/merged_uapi_headers/kernel-uapi-headers.tar.gz".format(name),
        ],
        size = "small",
    )
    tests.append("{}/default_out_test".format(name))

    merged_uapi_headers(
        name = "{}/merged_uapi_headers_with_custom_out".format(name),
        out = "{}/custom-uapi-headers.tar.gz".format(name),
        uapi_headers = [
            "{}/uapi_headers_1".format(name),
            "{}/uapi_headers_2".format(name),
        ],
    )

    files_test(
        name = "{}/custom_out_test".format(name),
        target_under_test = "{}/merged_uapi_headers_with_custom_out".format(name),
        expected_files = [
            "{}/custom-uapi-headers.tar.gz".format(name),
        ],
        size = "small",
    )
    tests.append("{}/custom_out_test".format(name))

    merged_uapi_headers(
        name = "{}/merged_uapi_headers_with_invalid_out".format(name),
        out = "{}/not_tar_gz.txt".format(name),
        uapi_headers = [
            "{}/uapi_headers_1".format(name),
            "{}/uapi_headers_2".format(name),
        ],
    )

    failure_test(
        name = "{}/invalid_out_test".format(name),
        target_under_test = "{}/merged_uapi_headers_with_invalid_out".format(name),
        reason = "out filename must end with \".tar.gz\"",
        size = "small",
    )
    tests.append("{}/invalid_out_test".format(name))

    native.test_suite(
        name = name,
        tests = tests,
    )
