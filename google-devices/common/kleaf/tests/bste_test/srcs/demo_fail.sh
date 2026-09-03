# SPDX-License-Identifier: GPL-2.0-only

function demo_fail {
  fail "this is an explicit custom failure demo"
}

function demo_assert_fail {
  assert_eq "expected_value" "actual_value" "this is an assertion failure demo"
}
