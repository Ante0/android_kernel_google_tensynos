# SPDX-License-Identifier: GPL-2.0-only

"""Expect failure for a target"""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")

def _failure_test_impl(ctx):
    env = analysistest.begin(ctx)

    asserts.expect_failure(env, ctx.attr.reason)

    return analysistest.end(env)

failure_test = analysistest.make(
    _failure_test_impl,
    attrs = {
        "reason": attr.string(),
    },
    expect_failure = True,
)
