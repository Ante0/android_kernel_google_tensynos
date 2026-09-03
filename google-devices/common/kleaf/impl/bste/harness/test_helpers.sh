# SPDX-License-Identifier: GPL-2.0-only

# BSTE Test Helpers API
# Sourced by the runner before loading test sources.

if [[ -n "${BASH_VERSION}" ]]; then
  shopt -s expand_aliases
fi

bste_test_message() {
  local caller_file="$1"
  local caller_line="$2"
  local content="$3"

  printf "%s:%s: %s\n\n" "${caller_file}" "${caller_line}" "${content}" >&3
  printf "%s:%s: %s\n\n" "${caller_file}" "${caller_line}" "${content}" >&2
}

bste_test_pass() {
  echo "PASSED" > "${BSTE_RESULT_PATH}"
  exit 0
}

bste_test_fail() {
  local caller_file="$1"
  local caller_line="$2"
  local msg="$3"
  local content="fail"
  if [[ -n "${msg}" ]]; then
    content="fail"$'\n'"${msg}"
  fi
  bste_test_message "${caller_file}" "${caller_line}" "${content}"
  echo "FAILED" > "${BSTE_RESULT_PATH}"
  exit 0
}

bste_test_skip() {
  local caller_file="$1"
  local caller_line="$2"
  local msg="$3"
  local content="skip"
  if [[ -n "${msg}" ]]; then
    content="skip"$'\n'"${msg}"
  fi
  bste_test_message "${caller_file}" "${caller_line}" "${content}"
  echo "SKIPPED" > "${BSTE_RESULT_PATH}"
  exit 0
}

bste_test_error() {
  local caller_file="$1"
  local caller_line="$2"
  local msg="$3"
  local content="error"
  if [[ -n "${msg}" ]]; then
    content="error"$'\n'"${msg}"
  fi
  bste_test_message "${caller_file}" "${caller_line}" "${content}"
  exit 1
}


bste_test_assert_true() {
  local caller_file="$1"
  local caller_line="$2"
  local expr="$3"
  local msg="$4"
  local details
  details=$(printf "Value of: %s\n  Actual: false\nExpected: true" "${expr}")
  if [[ -n "${msg}" ]]; then
    details=$(printf "%s\n%s" "${details}" "${msg}")
  fi
  if ! eval "${expr}"; then
    bste_test_fail "${caller_file}" "${caller_line}" "${details}"
  fi
}

bste_test_assert_false() {
  local caller_file="$1"
  local caller_line="$2"
  local expr="$3"
  local msg="$4"
  local details
  details=$(printf "Value of: %s\n  Actual: true\nExpected: false" "${expr}")
  if [[ -n "${msg}" ]]; then
    details=$(printf "%s\n%s" "${details}" "${msg}")
  fi
  if eval "${expr}"; then
    bste_test_fail "${caller_file}" "${caller_line}" "${details}"
  fi
}

bste_test_assert_eq() {
  local caller_file="$1"
  local caller_line="$2"
  local expected="$3"
  local actual="$4"
  local msg="$5"
  local details
  details=$(printf "Expected equality of these values:\n  Expected: %s\n  Actual:   %s" \
    "${expected}" "${actual}")
  if [[ -n "${msg}" ]]; then
    details=$(printf "%s\n%s" "${details}" "${msg}")
  fi
  if [[ "${expected}" != "${actual}" ]]; then
    bste_test_fail "${caller_file}" "${caller_line}" "${details}"
  fi
}

bste_test_assert_ne() {
  local caller_file="$1"
  local caller_line="$2"
  local val1="$3"
  local val2="$4"
  local msg="$5"
  local details
  details=$(printf "Expected inequality of these values:\n  Value 1: %s\n  Value 2: %s %s" \
    "${val1}" "${val2}" "(expected different)")
  if [[ -n "${msg}" ]]; then
    details=$(printf "%s\n%s" "${details}" "${msg}")
  fi
  if [[ "${val1}" == "${val2}" ]]; then
    bste_test_fail "${caller_file}" "${caller_line}" "${details}"
  fi
}

