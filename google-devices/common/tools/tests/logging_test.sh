#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

SCRIPT_DIR=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
readonly SCRIPT_DIR
readonly LOGGING_SH="${SCRIPT_DIR}/../logging.sh"

source "${SCRIPT_DIR}/test_base.sh"

function run_logging() {
  (
    # shellcheck source=tools/logging.sh
    source "${LOGGING_SH}"
    local -a cmd_args=()
    for arg in "$@"; do
      if [[ "${arg}" == --* || "${arg}" == -* ]]; then
        logging::set_option_or_out "${arg}"
      else
        cmd_args+=("${arg}")
      fi
    done

    if (( ${#cmd_args[@]} > 0 )); then
      "${cmd_args[@]}"
    fi
  ) 2>&1
}

echo -n "Testing default log level (info)... "
OUT=$(run_logging logging::debug "debug msg")
if [[ -n "${OUT}" ]]; then fail "Expected no output, got: ${OUT}"; fi
OUT=$(run_logging logging::verbose "verbose msg")
if [[ -n "${OUT}" ]]; then fail "Expected no output, got: ${OUT}"; fi
OUT=$(run_logging logging::info "info msg")
if [[ "${OUT}" != "[   INFO] info msg" ]]; then fail "Expected info msg, got: ${OUT}"; fi
OUT=$(run_logging logging::warning "warn msg")
if [[ "${OUT}" != "[WARNING] warn msg" ]]; then fail "Expected warn msg, got: ${OUT}"; fi
OUT=$(run_logging logging::error "error msg")
if [[ "${OUT}" != "[  ERROR] error msg" ]]; then fail "Expected error msg, got: ${OUT}"; fi
pass

echo -n "Testing --loglevel=debug... "
OUT=$(run_logging --loglevel=debug logging::debug "debug msg")
if [[ "${OUT}" != "[  DEBUG] debug msg" ]]; then fail "Expected debug msg, got: ${OUT}"; fi
OUT=$(run_logging --loglevel=debug logging::verbose "verbose msg")
if [[ "${OUT}" != "[VERBOSE] verbose msg" ]]; then fail "Expected verbose msg, got: ${OUT}"; fi
pass

echo -n "Testing --verbose (-v)... "
OUT=$(run_logging -v logging::debug "debug msg")
if [[ -n "${OUT}" ]]; then fail "Expected no output, got: ${OUT}"; fi
OUT=$(run_logging -v logging::verbose "verbose msg")
if [[ "${OUT}" != "[VERBOSE] verbose msg" ]]; then fail "Expected verbose msg, got: ${OUT}"; fi
pass

echo -n "Testing --quiet (-q)... "
OUT=$(run_logging -q logging::info "info msg")
if [[ -n "${OUT}" ]]; then fail "Expected no output, got: ${OUT}"; fi
OUT=$(run_logging -q logging::warning "warn msg")
if [[ -n "${OUT}" ]]; then fail "Expected no output, got: ${OUT}"; fi
OUT=$(run_logging -q logging::error "error msg")
if [[ "${OUT}" != "[  ERROR] error msg" ]]; then fail "Expected error msg, got: ${OUT}"; fi
pass

echo -n "Testing invalid --loglevel... "
set +e
OUT=$(
  (
    # shellcheck source=tools/logging.sh
    source "${LOGGING_SH}"
    logging::set_option_or_out "--loglevel=invalid"
  ) 2>&1
)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )); then fail "Expected failure for invalid loglevel"; fi
if ! echo "${OUT}" | grep -q "Invalid --loglevel: invalid"; then
  fail "Expected error message, got: ${OUT}"
fi
pass

echo -n "Testing logging::set_option return values... "
OUT=$(
  (
    # shellcheck source=tools/logging.sh
    source "${LOGGING_SH}"
    if ! logging::set_option "--verbose"; then
      echo "FAILED: --verbose should return 0"
    fi
    if logging::set_option "--unknown-flag"; then
      echo "FAILED: --unknown-flag should return 1"
    fi
  ) 2>&1
)
if [[ -n "${OUT}" ]]; then
  fail "${OUT}"
fi
pass

echo -n "Testing --timestamp=elapsed... "
OUT=$(run_logging --timestamp=elapsed logging::info "info msg")
if ! echo "${OUT}" | grep -Eq "^\[[ 0-9]+\.[0-9]{3}\]\[   INFO\] info msg$"; then
  fail "Expected elapsed timestamp, got: ${OUT}"
fi
pass

echo -n "Testing --timestamp=datetime... "
OUT=$(run_logging --timestamp=datetime logging::info "info msg")
DATETIME_RE="^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\]\[   INFO\] info msg$"
if ! echo "${OUT}" | grep -Eq "${DATETIME_RE}"; then
  fail "Expected datetime timestamp, got: ${OUT}"
fi
pass

echo -n "Testing --timestamp (default elapsed)... "
OUT=$(run_logging --timestamp logging::info "info msg")
if ! echo "${OUT}" | grep -Eq "^\[[ 0-9]+\.[0-9]{3}\]\[   INFO\] info msg$"; then
  fail "Expected elapsed timestamp, got: ${OUT}"
fi
pass

echo -n "Testing multi-line logging... "
OUT=$(run_logging logging::info "line 1"$'\n'"line 2")
EXPECTED="[   INFO] line 1
[   INFO] line 2"
if [[ "${OUT}" != "${EXPECTED}" ]]; then
  fail "Expected multi-line info msg, got: ${OUT}"
fi
OUT=$(run_logging logging::info "line 1"$'\n')
EXPECTED="[   INFO] line 1
[   INFO]"
if [[ "${OUT}" != "${EXPECTED}" ]]; then
  fail "Expected multi-line info msg (trailing newline), got: ${OUT}"
fi
pass

echo -n "Testing push/pop prefix... "
OUT=$(
  (
    # shellcheck source=tools/logging.sh
    source "${LOGGING_SH}"
    logging::push_prefix "[P1]"
    logging::info "msg 1"
    logging::push_prefix "[P2]"
    logging::info "msg 2"
    logging::pop_prefix
    logging::info "msg 3"
    logging::pop_prefix
    logging::pop_prefix # Extra pop
    logging::info "msg 4"
  ) 2>&1
)
EXPECTED="[   INFO] [P1]msg 1
[   INFO] [P1][P2]msg 2
[   INFO] [P1]msg 3
[   INFO] msg 4"
if [[ "${OUT}" != "${EXPECTED}" ]]; then
  fail "Expected prefix msg, got: ${OUT}"
fi
pass

finish_tests "logging"
