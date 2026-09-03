# SPDX-License-Identifier: GPL-2.0-only

"""Public API for the Base System Test Environment (BSTE)."""

load("//private/devices/google/common/kleaf/impl/bste:bste_cc_binary.bzl", _bste_cc_binary = "bste_cc_binary")
load("//private/devices/google/common/kleaf/impl/bste:bste_prebuilt_binary.bzl", _bste_prebuilt_binary = "bste_prebuilt_binary")
load("//private/devices/google/common/kleaf/impl/bste:bste_test_package.bzl", _bste_test_package = "bste_test_package")
load("//private/devices/google/common/kleaf/impl/bste:bste_test_suite.bzl", _bste_test_suite = "bste_test_suite")

bste_cc_binary = _bste_cc_binary
bste_prebuilt_binary = _bste_prebuilt_binary
bste_test_suite = _bste_test_suite
bste_test_package = _bste_test_package
