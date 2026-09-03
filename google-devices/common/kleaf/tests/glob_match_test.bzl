# SPDX-License-Identifier: GPL-2.0-only

"""Tests for glob_match library."""

load("//private/devices/google/common/kleaf:glob_match.bzl", "glob_match")

def _glob_match_test_impl(ctx):
    tests = [
        # (path, pattern, expected)
        ("a", "a", True),
        ("a", "b", False),
        ("a/b", "a/b", True),
        ("a/b", "a/c", False),

        # * matches single segment
        ("a", "*", True),
        ("a/b", "*", False),  # * should not match /
        ("a/b", "a/*", True),
        ("a/b/c", "a/*", False),
        ("a/b/c", "a/*/c", True),

        # * in segment
        ("foo", "fo*", True),
        ("foo", "*oo", True),
        ("foo", "f*o", True),
        ("foo", "*o*", True),
        ("foo", "*fo", False),
        ("foo", "oo*", False),
        ("f", "f*f", False),
        ("ff", "f*f", True),
        ("fof", "f*f", True),

        # ** matches multiple segments
        ("a", "**", True),
        ("a/b", "**", True),
        ("a/b/c", "**", True),
        ("a/b/c", "a/**", True),
        ("a/b/c", "**/c", True),
        ("a/b/c", "a/**/c", True),
        ("a/b/c", "**/b", False),
        ("a/b/c", "b/**", False),
        ("a/b/c/d/e", "a/**/e", True),
        ("a/b/c/d/e", "a/**/d/*", True),
        ("a/b/c/d/e", "a/**/c/**", True),
        ("a/b/c/d/e", "a/**/c/*", False),
        ("a/b/x/b/a", "a/**/b/*", True),
        ("a/b/c", "a/**/b/**/c", True),
        ("a/b/c", "a/**/b/**/d", False),

        # Complex cases
        ("a/b/c.txt", "a/**/*.txt", True),
        ("a/b/c.txt", "a/*.txt", False),
        ("a/c.txt", "a/*.txt", True),
    ]

    for path, pattern, expected in tests:
        actual = glob_match(path, pattern)
        if actual != expected:
            fail("Expected glob_match('{}', '{}') to be {}, but got {}".format(
                path,
                pattern,
                expected,
                actual,
            ))

    out = ctx.actions.declare_file(ctx.label.name + ".sh")
    ctx.actions.write(out, "#!/bin/sh\nexit 0", is_executable = True)
    return [DefaultInfo(executable = out)]

glob_match_rule_test = rule(
    implementation = _glob_match_test_impl,
    test = True,
)

def glob_match_test(name):
    glob_match_rule_test(
        name = name,
        size = "small",
    )
