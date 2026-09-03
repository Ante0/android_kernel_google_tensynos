# SPDX-License-Identifier: GPL-2.0-only

function demo_pass {
  pass
}

function demo_assert_pass {
  assert_true "test 1 -eq 1"
  assert_false "test 1 -eq 2"
  assert_ne "a" "b"
  pass
}
