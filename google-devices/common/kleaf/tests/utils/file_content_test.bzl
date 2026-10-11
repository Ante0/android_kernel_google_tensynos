# SPDX-License-Identifier: GPL-2.0-only

"""Check the content of a file."""

load("//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")

def _file_content_test_impl(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)

    if ctx.attr.expected and ctx.attr.expected_content:
        fail('Setting both "expected" and "expected_content" is not allowed.')

    if ctx.attr.expected:
        expected = ctx.file.expected
    else:
        expected = ctx.actions.declare_file(ctx.attr.name + "/expected")
        ctx.actions.write(output = expected, content = ctx.attr.expected_content)

    script = hermetic_tools.run_setup + """
        diff {actual} {expected}
    """.format(
        actual = ctx.file.actual.short_path,
        expected = expected.short_path,
    )

    script_file = ctx.actions.declare_file(ctx.label.name + "/test.sh")
    ctx.actions.write(script_file, script, is_executable = True)

    runfiles = ctx.runfiles([
        script_file,
        ctx.file.actual,
        expected,
    ], transitive_files = hermetic_tools.deps)

    return DefaultInfo(
        files = depset([script_file]),
        executable = script_file,
        runfiles = runfiles,
    )

file_content_test = rule(
    implementation = _file_content_test_impl,
    doc = "Check the content of a file.",
    attrs = {
        "actual": attr.label(
            allow_single_file = True,
            doc = "The file to check.",
        ),
        "expected": attr.label(
            allow_single_file = True,
            doc = "The file with the expected content.",
        ),
        "expected_content": attr.string(
            doc = "Expected content.",
        ),
    },
    test = True,
    toolchains = [hermetic_toolchain.type],
)
