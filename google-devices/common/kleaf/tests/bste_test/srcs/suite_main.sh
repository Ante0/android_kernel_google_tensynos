#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

suite_init() {
  echo "Initializing sample test environment..."
  [ -f "./sample_cc_binary" ] || { echo "Missing sample_cc_binary" >&2; return 1; }
  [ -f "./sample_prebuilt_binary" ] || { echo "Missing sample_prebuilt_binary" >&2; return 1; }
  return 0
}

test_case_1() {
  echo "Executing compiled C++ binary component..."
  ./sample_cc_binary > "${OUT}/cc_stdout.log" 2>&1
  local ret=$?
  echo "Exit Status: ${ret}" >> "${OUT}/cc_stdout.log"
  return ${ret}
}

test_case_2() {
  echo "Executing prebuilt script component..."
  ./sample_prebuilt_binary > "${OUT}/prebuilt_stdout.log" 2>&1
  local ret=$?

  echo "Verifying runtime configuration data access..."
  if [ -f "data/module_config.bin" ]; then
    cat "data/module_config.bin" >> "${OUT}/prebuilt_stdout.log"
  else
    echo "Error: Config bin missing!" >&2
    ret=1
  fi

  return ${ret}
}

test_case_long() {
  echo "Executing long test case (5 seconds)..."
  sleep 5
  return 0
}

suite_exit() {
  echo "Cleaning up sample test environment..."
  return 0
}

test_init() {
  echo "Initializing test case environment..."
  return 0
}

test_exit() {
  echo "Cleaning up test case environment..."
  return 0
}
