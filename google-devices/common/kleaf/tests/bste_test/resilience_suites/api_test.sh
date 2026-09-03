# SPDX-License-Identifier: GPL-2.0-only

test_success() {
  # Test state sharing via TEST_OUT
  echo "state_data" > "${TEST_OUT}/shared_state.txt"
  pass "custom pass message"
}

test_failure() {
  assert_eq "expected_val" "actual_val" "eq message"
}

test_skip() {
  skip "skip message"
}

test_error() {
  # Test state sharing via SUITE_OUT
  echo "suite_state" > "${SUITE_OUT}/suite_shared.txt"
  error "explicit error message"
}

test_asserts() {
  assert_true "test 1 -eq 1" "should be true"
  assert_false "test 1 -eq 2" "should be false"
  assert_ne "val1" "val2" "should be different"
  pass "asserts ok"
}
