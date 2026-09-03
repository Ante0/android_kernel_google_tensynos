# SPDX-License-Identifier: GPL-2.0-only

suite_init() {
  echo "Iso suite initialized"
}

test_case_1() {
  # Modify it in child subshell
  GLOBAL_VAR="MODIFIED"
  LEAK_VAR="LEAKED"
}

test_case_2() {
  # Verify original value persists and no leak from test_case_1
  if [ "${GLOBAL_VAR}" != "ORIGINAL" ]; then
    echo "FAIL: GLOBAL_VAR changed!"
    return 1
  fi
  if [ -n "${LEAK_VAR}" ]; then
    echo "FAIL: LEAK_VAR was seen!"
    return 1
  fi
  return 0
}
