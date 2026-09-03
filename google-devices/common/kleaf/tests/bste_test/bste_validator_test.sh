#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

# BSTE Validator Test
# Verifies that bste_validator correctly identifies valid and invalid BSTE sources.

set -u

VALIDATOR="$1"

if [[ -z "${VALIDATOR}" ]]; then
  echo "ERROR: Missing validator binary argument" >&2
  exit 1
fi

if [[ ! -f "${VALIDATOR}" ]]; then
  echo "ERROR: Cannot find validator binary at ${VALIDATOR}" >&2
  exit 1
fi

# Create temporary directory for test cases
TMPDIR=$(mktemp -d)
trap 'rm -rf "${TMPDIR}"' EXIT

# --- Test Case 1: Valid Script ---
cat <<'EOF' > "${TMPDIR}/valid.sh"
#!/bin/sh

# A valid BSTE source
suite_init() {
  echo "init"
}

test_case_1() {
  if true; then
    echo "ok"
  fi
}
EOF

if ! "${VALIDATOR}" "${TMPDIR}/valid.sh" > "${TMPDIR}/test1.log" 2>&1; then
  echo "FAIL: Expected valid.sh to pass validation, but it failed." >&2
  cat "${TMPDIR}/test1.log" >&2
  exit 1
fi
echo "PASS: valid.sh passed as expected."

# --- Test Case 2: Invalid Script (Top-level command) ---
cat <<'EOF' > "${TMPDIR}/invalid_cmd.sh"
#!/bin/sh

setup() {
  echo "setup"
}

# Top-level command (not allowed)
echo "running setup"

test_1() {
  echo "test"
}
EOF

if "${VALIDATOR}" "${TMPDIR}/invalid_cmd.sh" > "${TMPDIR}/test2.log" 2>&1; then
  echo "FAIL: Expected invalid_cmd.sh to fail validation, but it passed." >&2
  exit 1
fi

# Verify error message contains expected text
if ! grep -q "Top-level command or invalid syntax found" "${TMPDIR}/test2.log"; then
  echo "FAIL: invalid_cmd.sh failed but error message was unexpected:" >&2
  cat "${TMPDIR}/test2.log" >&2
  exit 1
fi
echo "PASS: invalid_cmd.sh failed as expected with correct error."

# --- Test Case 3: Invalid Script (Unclosed function) ---
cat <<'EOF' > "${TMPDIR}/invalid_unclosed.sh"
#!/bin/sh

setup() {
  echo "setup"
  # missing closing brace
EOF

if "${VALIDATOR}" "${TMPDIR}/invalid_unclosed.sh" > "${TMPDIR}/test3.log" 2>&1; then
  echo "FAIL: Expected invalid_unclosed.sh to fail validation, but it passed." >&2
  exit 1
fi

if ! grep -q "Shell syntax check failed" "${TMPDIR}/test3.log"; then
  echo "FAIL: invalid_unclosed.sh failed but error message was unexpected:" >&2
  cat "${TMPDIR}/test3.log" >&2
  exit 1
fi
echo "PASS: invalid_unclosed.sh failed as expected with correct error."

# --- Test Case 4: Valid Script with Tricky Heredoc ---
cat <<'EOF' > "${TMPDIR}/valid_heredoc.sh"
#!/bin/sh

my_func() {
  cat <<INNER_EOF
  This is a heredoc with { braces }
  and unbalanced { brace
INNER_EOF
}
EOF

if ! "${VALIDATOR}" "${TMPDIR}/valid_heredoc.sh" > "${TMPDIR}/test4.log" 2>&1; then
  echo "FAIL: Expected valid_heredoc.sh to pass validation, but it failed." >&2
  cat "${TMPDIR}/test4.log" >&2
  exit 1
fi
echo "PASS: valid_heredoc.sh passed as expected."

# --- Test Case 5: Missing expected function ---
# valid.sh defines `suite_init` and `test_case_1`.
# We expect `suite_init`, `test_case_1`, and `missing_func`.
if "${VALIDATOR}" "${TMPDIR}/valid.sh" \
    --expected_functions suite_init test_case_1 missing_func \
    > "${TMPDIR}/test5.log" 2>&1; then
  echo "FAIL: Expected validation to fail due to missing function, but it passed." >&2
  exit 1
fi

if ! grep -q "expected functions are not defined" "${TMPDIR}/test5.log"; then
  echo "FAIL: Validation failed but error message was unexpected:" >&2
  cat "${TMPDIR}/test5.log" >&2
  exit 1
fi
echo "PASS: Missing expected function detected correctly."

# --- Test Case 6: Success with expected functions ---
if ! "${VALIDATOR}" "${TMPDIR}/valid.sh" \
    --expected_functions suite_init test_case_1 \
    > "${TMPDIR}/test6.log" 2>&1; then
  echo "FAIL: Expected validation to pass with correct expected functions, but it failed." >&2
  cat "${TMPDIR}/test6.log" >&2
  exit 1
fi
echo "PASS: Validation passed with correct expected functions."

# --- Test Case 7: Shell syntax error ---
cat <<'EOF' > "${TMPDIR}/invalid_syntax.sh"
#!/bin/sh
setup() {
  if true; then
    echo "missing fi"
}
EOF

if "${VALIDATOR}" "${TMPDIR}/invalid_syntax.sh" \
    > "${TMPDIR}/test7.log" 2>&1; then
  echo "FAIL: Expected validation to fail due to syntax error, but it passed." >&2
  exit 1
fi

if ! grep -q "syntax error" "${TMPDIR}/test7.log"; then
  echo "FAIL: Validation failed but error message was unexpected:" >&2
  cat "${TMPDIR}/test7.log" >&2
  exit 1
fi
echo "PASS: Shell syntax error detected correctly."

echo "ALL TESTS PASSED"
exit 0
