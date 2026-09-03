#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Validator for BSTE test source files.

Ensures that test source files (.sh) only contain function definitions,
comments, and empty lines at the top level, and verifies that all
expected functions are defined.
"""

import argparse
import re
import subprocess
import sys

def clean_line_for_braces(line):
    """Removes strings and comments from a line to make brace counting safer."""
    # Remove double quoted strings
    line = re.sub(r'"([^"\\]|\\.)*"', '""', line)
    # Remove single quoted strings
    line = re.sub(r"'([^'\\]|\\.)*'", "''", line)
    # Remove comments
    line = line.split('#')[0]
    return line

def check_syntax(filepath):
    """Checks shell script syntax using 'bash -n'."""
    # TODO: Use mksh for syntax checking once it is available in the build
    # system, as mksh is the target shell on Android devices.
    try:
        result = subprocess.run(
            ["bash", "-n", filepath],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            print(f"ERROR: {filepath}: Shell syntax check failed:", file=sys.stderr)
            for line in result.stderr.splitlines():
                print(f"  {line}", file=sys.stderr)
            return False
        return True
    except FileNotFoundError:
        print(
            f"WARNING: 'bash' binary not found. Skipping syntax check for {filepath}.",
            file=sys.stderr,
        )
        return True
    except Exception as e:
        print(
            f"ERROR: Failed to run syntax check for {filepath}: {e}",
            file=sys.stderr,
        )
        return False

def validate_file(filepath):
    """Validates the file and returns the set of defined function names.

    Returns:
        A set of string function names if valid, or None if invalid.
    """
    if not check_syntax(filepath):
        return None

    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
    except IOError as e:
        print(f"ERROR: Failed to read {filepath}: {e}", file=sys.stderr)
        return None

    in_function = False
    brace_depth = 0
    in_heredoc = False
    heredoc_marker = ""
    defined_functions = set()

    # Regex for function definition start.
    # Supports:
    # func() {
    # function func {
    # function func() {
    # Assumes the opening brace '{' is on the same line.
    func_start_re = re.compile(
        r'^\s*(?:function\s+)?([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:\(\s*\))?\s*\{'
    )

    # Regex for heredoc start.
    # Matches: <<EOF, <<'EOF', <<"EOF", <<-EOF, <<-"EOF"
    # Group 1: '-' if present (indicates tabs are allowed before end marker)
    # Group 3: the marker word
    heredoc_start_re = re.compile(r'<<(-?)\s*([\'"]?)([a-zA-Z0-9_]+)\2')

    for line_num, line in enumerate(lines, 1):
        stripped = line.strip()

        if in_heredoc:
            # Check if this line is the end marker.
            if stripped == heredoc_marker:
                in_heredoc = False
            continue

        # Skip empty lines
        if not stripped:
            continue

        # Skip comments (including shebang on line 1)
        if stripped.startswith('#'):
            continue

        if not in_function:
            # We are at the top level. We only allow function definitions.
            match = func_start_re.match(stripped)
            if match:
                in_function = True
                func_name = match.group(1)
                defined_functions.add(func_name)
                clean_line = clean_line_for_braces(line)
                brace_depth += clean_line.count('{') - clean_line.count('}')
                if brace_depth <= 0:
                    # Single line function (e.g. f() { :; })
                    in_function = False
                    brace_depth = 0
            else:
                print(
                    f"ERROR: {filepath}:{line_num}: Top-level command or "
                    f"invalid syntax found: '{stripped}'",
                    file=sys.stderr,
                )
                print(
                    "Only function definitions, comments, and empty lines "
                    "are allowed at the top level.",
                    file=sys.stderr,
                )
                return None
        else:
            # Inside a function, track braces and heredocs.
            clean_line = clean_line_for_braces(line)

            hd_match = heredoc_start_re.search(clean_line)
            if hd_match:
                in_heredoc = True
                heredoc_marker = hd_match.group(3)

            brace_depth += clean_line.count('{') - clean_line.count('}')
            if brace_depth <= 0:
                in_function = False
                brace_depth = 0

    if in_heredoc:
        print(
            f"ERROR: {filepath}: Unclosed heredoc (marker "
            f"'{heredoc_marker}' not found).",
            file=sys.stderr,
        )
        return None

    if in_function:
        print(
            f"ERROR: {filepath}: Unclosed function definition "
            f"(brace mismatch). Check if all functions are closed.",
            file=sys.stderr,
        )
        return None

    return defined_functions

def main():
    parser = argparse.ArgumentParser(description="Validate BSTE test source files.")
    parser.add_argument("srcs", nargs="*", help="Source files to validate")
    parser.add_argument("--output", help="Output file to touch on success")
    parser.add_argument(
        "--expected_functions",
        nargs="*",
        default=[],
        help="Expected function names that must be defined",
    )
    args = parser.parse_args()

    if not args.srcs:
        if args.expected_functions:
            print(
                f"ERROR: No source files provided, but expected functions are "
                f"required: {', '.join(args.expected_functions)}",
                file=sys.stderr,
            )
            sys.exit(1)

    success = True
    all_defined_functions = set()
    for src in args.srcs:
        defined = validate_file(src)
        if defined is None:
            success = False
        else:
            all_defined_functions.update(defined)

    if not success:
        sys.exit(1)

    # Check expected functions
    missing_functions = []
    for func in args.expected_functions:
        if func not in all_defined_functions:
            missing_functions.append(func)

    if missing_functions:
        print(
            f"ERROR: The following expected functions are not defined in any "
            f"source file: {', '.join(missing_functions)}",
            file=sys.stderr,
        )
        sys.exit(1)

    if args.output:
        try:
            with open(args.output, 'w') as f:
                f.write("OK\n")
        except IOError as e:
            print(
                f"ERROR: Failed to write output file {args.output}: {e}",
                file=sys.stderr,
            )
            sys.exit(1)

if __name__ == "__main__":
    main()
