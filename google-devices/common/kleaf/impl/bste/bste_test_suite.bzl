# SPDX-License-Identifier: GPL-2.0-only

"""Implements the bste_test_suite rule."""

load("//private/devices/google/common/kleaf:path_relative_to_package.bzl", "path_relative_to_package")

def _validate_func(name):
    """Validates if the function name adheres to shell naming rules."""
    if not name:
        return False
    chars = list(name.elems())
    valid_first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_"
    valid_rest = valid_first + "0123456789"

    if chars[0] not in list(valid_first.elems()):
        return False
    for c in chars[1:]:
        if c not in list(valid_rest.elems()):
            return False
    return True

def _bste_test_suite_impl(ctx):
    suite_dir = ctx.attr.name

    if not ctx.attr.test_cases:
        fail("{}: 'test_cases' attribute cannot be empty.".format(ctx.label))

    # 1. Validate function names
    all_funcs = list(ctx.attr.test_cases)
    if ctx.attr.suite_init:
        all_funcs.append(ctx.attr.suite_init)
    if ctx.attr.suite_exit:
        all_funcs.append(ctx.attr.suite_exit)
    if ctx.attr.test_init:
        all_funcs.append(ctx.attr.test_init)
    if ctx.attr.test_exit:
        all_funcs.append(ctx.attr.test_exit)

    for func in all_funcs:
        if not _validate_func(func):
            fail("{}: Function name '{}' is invalid. Must match [a-zA-Z_][a-zA-Z0-9_]*".format(ctx.label, func))

    input_map = {}
    src_files = []

    # 2. Process Sources
    for src_target in ctx.attr.srcs:
        for f in src_target.files.to_list():
            if not f.basename.endswith(".sh"):
                fail("{}: File in 'srcs' does not end in .sh: {}".format(ctx.label, f.basename))
            src_files.append(f)
            rel_path = path_relative_to_package(f)
            dest_path = suite_dir + "/" + rel_path
            if dest_path in input_map:
                fail("{}: Collision detected for destination path: {}".format(ctx.label, dest_path))
            input_map[dest_path] = f

    # 2.5 Validate Sources and Function Definitions
    val_token = ctx.actions.declare_file(
        suite_dir + "/_validation/suite.validated",
    )

    args = ctx.actions.args()
    args.add_all(src_files)
    args.add("--output", val_token.path)
    if all_funcs:
        args.add_all("--expected_functions", all_funcs)

    ctx.actions.run(
        outputs = [val_token],
        inputs = src_files,
        executable = ctx.executable._validator,
        arguments = [args],
        mnemonic = "BsteValidateSuite",
        progress_message = (
            "Validating BSTE suite {} for top-level commands and " +
            "function definitions"
        ).format(ctx.label),
    )
    validation_outputs = [val_token]

    # 3. Process Data Dependencies (Files + DefaultRunfiles)
    data_depsets = []
    for data_target in ctx.attr.data:
        data_depsets.append(data_target.files)
        if DefaultInfo in data_target:
            data_depsets.append(data_target[DefaultInfo].default_runfiles.files)

    all_data = depset(transitive = data_depsets).to_list()
    for f in all_data:
        rel_path = path_relative_to_package(f)
        dest_path = suite_dir + "/" + rel_path
        if dest_path in input_map:
            if input_map[dest_path] != f:
                fail("{}: Collision detected for data file mapping to '{}'".format(ctx.label, dest_path))

            # It's the same physical file, skipping.
            continue
        input_map[dest_path] = f

    # 4. Generate Actions for symlinking
    output_files = []
    for dest, src_file in input_map.items():
        out_file = ctx.actions.declare_file(dest)
        ctx.actions.symlink(
            output = out_file,
            target_file = src_file,
            progress_message = "Mapping test suite file {}".format(dest),
        )
        output_files.append(out_file)

    # 5. Generate bste_test_metadata.sh
    metadata_file = ctx.actions.declare_file(suite_dir + "/bste_test_metadata.sh")

    cases_fmt = " ".join(["\"{}\"".format(c) for c in ctx.attr.test_cases])
    metadata_lines = [
        "# SPDX-License-Identifier: GPL-2.0-only",
        "BSTE_TEST_CASES=({})".format(cases_fmt),
    ]
    if ctx.attr.suite_init:
        metadata_lines.append("BSTE_SUITE_INIT=\"{}\"".format(ctx.attr.suite_init))
    if ctx.attr.suite_exit:
        metadata_lines.append("BSTE_SUITE_EXIT=\"{}\"".format(ctx.attr.suite_exit))
    if ctx.attr.test_init:
        metadata_lines.append("BSTE_TEST_INIT=\"{}\"".format(ctx.attr.test_init))
    if ctx.attr.test_exit:
        metadata_lines.append("BSTE_TEST_EXIT=\"{}\"".format(ctx.attr.test_exit))

    metadata_content = "\n".join(metadata_lines) + "\n"
    ctx.actions.write(metadata_file, metadata_content)
    output_files.append(metadata_file)

    # 6. Generate bste_test_sources.sh
    sources_file = ctx.actions.declare_file(suite_dir + "/bste_test_sources.sh")
    sources_lines = ["# SPDX-License-Identifier: GPL-2.0-only"]
    for sf in src_files:
        rpath = path_relative_to_package(sf)
        sources_lines.append("alias pass='bste_test_pass \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("alias fail='bste_test_fail \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("alias skip='bste_test_skip \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("alias error='bste_test_error \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("alias assert_true='bste_test_assert_true \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("alias assert_false='bste_test_assert_false \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("alias assert_eq='bste_test_assert_eq \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("alias assert_ne='bste_test_assert_ne \"{}\" \"$LINENO\"'".format(rpath))
        sources_lines.append("source " + rpath)

    sources_content = "\n".join(sources_lines) + "\n"

    tmp_sources_file = ctx.actions.declare_file(suite_dir + "/bste_test_sources.sh.tmp")
    ctx.actions.write(tmp_sources_file, sources_content)

    ctx.actions.run_shell(
        inputs = [tmp_sources_file] + validation_outputs,
        outputs = [sources_file],
        command = "cp {} {}".format(tmp_sources_file.path, sources_file.path),
        mnemonic = "BsteFinalizeSources",
        progress_message = "Finalizing BSTE sources for {}".format(ctx.label),
    )
    output_files.append(sources_file)

    return [
        DefaultInfo(files = depset(output_files)),
    ]

bste_test_suite = rule(
    implementation = _bste_test_suite_impl,
    doc = "Primary packaging unit for a test suite.",
    attrs = {
        "srcs": attr.label_list(
            allow_files = [".sh"],
            doc = "Test source files (must end in .sh).",
        ),
        "data": attr.label_list(
            allow_files = True,
            doc = "Runtime dependencies (binaries, data files).",
        ),
        "test_cases": attr.string_list(
            mandatory = True,
            doc = "List of test case function names.",
        ),
        "suite_init": attr.string(doc = "Suite-level setup function."),
        "suite_exit": attr.string(doc = "Suite-level cleanup function."),
        "test_init": attr.string(doc = "Test-level setup function."),
        "test_exit": attr.string(doc = "Test-level cleanup function."),
        "_validator": attr.label(
            default = ":bste_validator",
            executable = True,
            cfg = "exec",
        ),
    },
)
