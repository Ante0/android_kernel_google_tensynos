# SPDX-License-Identifier: GPL-2.0-only

"""Malibu platform definitions for power controller power domain tests."""

MBU_PD_CC_BINARY_SRCS = [
    "power-domain/platform/mbu/pd_defs.c",
]

MBU_PD_TEST_SUITE_SRCS = [
    "power-domain/platform/mbu/mbu_tests.sh",
]

MBU_PD_TEST_CASES = [
    "test_domain_aurdsp",
    "test_domain_aoss_pg",
    "test_domain_codec_3p",
    "test_domain_cpuacc",
    "test_domain_g2d",
    "test_domain_gcv",
    "test_domain_gpu",
    "test_domain_tpu",
    "test_domain_memss_dta",
    "test_domain_dpu",
    # "test_domain_hsio_n",  # TODO: b/526571556 - Enable when we define a way to test this.
    "test_domain_hsio_s",
    "test_domain_ispfe",
    "test_domain_ispbe",
    "test_domain_lsio_e",
    "test_domain_lsio_s",
    "test_domain_pcie",
]
