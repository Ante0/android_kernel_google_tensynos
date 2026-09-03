#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# BSTE Backend Runner
# Orchestrates the heavy execution lifecycles and persistent sandbox logging.

set -e

# Load shared state and I/O primitives
source "$(dirname "$(realpath "$0")")/common.sh"

readonly PID="$$"
# shellcheck disable=SC2155
readonly RUN_ID="$(date +"%Y%m%d_%H%M%S")_$(printf "%05d" "${PID}")"
readonly RUN_DIR="${RUNS_DIR}/${RUN_ID}"

readonly RET_SKIPPED=0
readonly RET_PASSED=1
readonly RET_FAILED=2
readonly RET_ERROR=3

load_my_state() {
  load_state "${RUN_DIR}/state.txt"
}

save_my_state() {
  save_state "${RUN_DIR}/state.txt"
}

ret_from_result() {
  case "$1" in
    SKIPPED) echo -n 0 ;;
    PASSED)  echo -n 1 ;;
    FAILED)  echo -n 2 ;;
    ERROR)   echo -n 3 ;;
    *)       echo -n 128 ;;
  esac
}

result_from_ret() {
  case "$1" in
    "${RET_SKIPPED}") echo -n "SKIPPED" ;;
    "${RET_PASSED}")  echo -n "PASSED" ;;
    "${RET_FAILED}")  echo -n "FAILED" ;;
    "${RET_ERROR}")   echo -n "ERROR" ;;
    *)                echo -n "UNKNOWN" ;;
  esac
}

init() {
  mkdir -p "${RUN_DIR}"
  set_current "${RUN_ID}"

  readonly STATE_RUN_ID="${RUN_ID}"
  readonly STATE_PID="${PID}"
  STATE_STATUS="RUNNING"
  STATE_PHASE=""
  STATE_SUITE=""
  STATE_CASE=""
  save_my_state
}

append_summary() {
  local content="$1"
  echo "${content}" >> "${RUN_DIR}/summary.txt"
  fsync "${RUN_DIR}/summary.txt" >/dev/null 2>&1 || true
}

run_phase() {
  local phase="$1"
  local func="$2"

  if [[ -z "${func}" ]]; then
    return 0
  fi

  local pretty_id="${TEST_SUITE}"
  if [[ -n "${TEST_CASE}" ]]; then
    pretty_id="${pretty_id}.${TEST_CASE}"
  fi

  # GoogleTest style RUN line
  printf "[ RUN      ] %s (%s)\n" "${pretty_id}" "${phase}" >&3

  local log_file="/dev/null"
  if [[ -n "${OUT}" ]]; then
    log_file="${OUT}/test.log"
  fi

  echo "=== ${pretty_id} (${phase}) ===" >> "${log_file}"

  # Export to test helpers
  export BSTE_RESULT_PATH="${OUT}/.${phase}.result"

  local start_ms
  start_ms=$(get_epoch_ms)
  local status="ERROR"
  # Run the function in a subshell, so it can call exit safely.
  if (
    readonly STATE_PHASE="${phase}"
    save_my_state

    set +e
    "${func}" >> "${log_file}" 2>&1
  ); then
    status="OK"
  fi
  append_summary "phase:${pretty_id}:${phase}=${status}"

  STATE_PHASE=""
  save_my_state

  local end_ms
  end_ms=$(get_epoch_ms)
  local elapsed_ms="$(( end_ms - start_ms ))"

  local result="ERROR"
  if [[ "${status}" == "OK" ]]; then
    if [[ -f "${BSTE_RESULT_PATH}" ]]; then
      result="$(cat "${BSTE_RESULT_PATH}")"
    else
      result="PASSED"
    fi
  fi

  local bracket
  case "${result}" in
    PASSED)  bracket="[       OK ]" ;;
    FAILED)  bracket="[  FAILED  ]" ;;
    SKIPPED) bracket="[  SKIPPED ]" ;;
    ERROR)   bracket="[  ERROR   ]" ;;
    *)       bracket="[  UNKNOWN ]" ;;
  esac
  printf "%s %s (%s) (%d ms)\n" "${bracket}" "${pretty_id}" "${phase}" "${elapsed_ms}" >&3

  return "$(ret_from_result "${result}")"
}

run_test_plan() {
  cd "${PKG_DIR}/test_suites" || return 1

  # Generate unique test suite workflow list from current plan
  local unique_suites
  unique_suites=()
  local current_last=""
  local scan_item
  for scan_item in "${BSTE_RESOLVED_PLAN[@]}"; do
    local ssuite="${scan_item%.*}"
    if [[ "${ssuite}" != "${current_last}" ]]; then
      unique_suites+=("${ssuite}")
      current_last="${ssuite}"
    fi
  done

  local total_suites="${#unique_suites[@]}"
  local total_cases="${#BSTE_RESOLVED_PLAN[@]}"
  local run_start_ms
  run_start_ms=$(get_epoch_ms)

  printf "[==========] Running %d tests from %d test suites.\n" \
    "${total_cases}" "${total_suites}" >&3

  local overall_ret=0
  for TEST_SUITE in "${unique_suites[@]}"; do
    # Harvest exact subset of tests mapped to this specific test suite
    local cases
    cases=()
    local case_item
    for case_item in "${BSTE_RESOLVED_PLAN[@]}"; do
      if [[ "${case_item%.*}" == "${TEST_SUITE}" ]]; then
        cases+=("${case_item#*.}")
      fi
    done
    local num_cases="${#cases[@]}"

    printf "[----------] %d tests from %s\n" "${num_cases}" "${TEST_SUITE}" >&3

    local suite_start_ms
    suite_start_ms=$(get_epoch_ms)
    local suite_ret=0
    (
      readonly STATE_SUITE="${TEST_SUITE}"
      save_my_state

      cd "${TEST_SUITE}"

      export SUITE_OUT="${RUN_DIR}/test_suites/${TEST_SUITE}/out"
      export OUT="${SUITE_OUT}"
      mkdir -p "${SUITE_OUT}"

      source "${HARNESS_DIR}/test_helpers.sh"
      # shellcheck disable=SC1091
      source ./bste_test_sources.sh
      # shellcheck disable=SC1091
      source ./bste_test_metadata.sh

      local s_ret=0
      run_phase "SUITE_INIT" "${BSTE_SUITE_INIT}" || s_ret="$?"
      if (( s_ret > RET_PASSED )); then
        exit "${s_ret}"
      fi

      for TEST_CASE in "${cases[@]}"; do
        local test_ret=0
        (
          readonly STATE_CASE="${TEST_CASE}"
          save_my_state

          export TEST_OUT="${RUN_DIR}/test_suites/${TEST_SUITE}/${TEST_CASE}/out"
          export OUT="${TEST_OUT}"
          mkdir -p "${TEST_OUT}"

          local ret=0
          run_phase "TEST_INIT" "${BSTE_TEST_INIT}" || ret="$?"
          if (( ret > RET_PASSED )); then
            exit "${ret}"
          fi

          ret=0 && run_phase "TEST_CASE" "${TEST_CASE}" || ret="$?"

          local exit_ret=0
          run_phase "TEST_EXIT" "${BSTE_TEST_EXIT}" || exit_ret="$?"
          if (( exit_ret > RET_PASSED )) && (( exit_ret > ret )); then
            exit "${exit_ret}"
          fi

          exit "${ret}"
        ) || test_ret="$?"
        append_summary "result:${TEST_SUITE}.${TEST_CASE}=$(result_from_ret "${test_ret}")"

        STATE_CASE=""
        save_my_state

        if (( test_ret > s_ret )); then
          s_ret="${test_ret}"
        fi
      done
      unset TEST_CASE

      local s_exit_ret=0
      run_phase "SUITE_EXIT" "${BSTE_SUITE_EXIT}" || s_exit_ret="$?"
      if (( s_exit_ret > RET_PASSED )) && (( s_exit_ret > s_ret )); then
        exit "${s_exit_ret}"
      fi

      exit "${s_ret}"
    ) || suite_ret="$?"
    append_summary "result:${TEST_SUITE}=$(result_from_ret "${suite_ret}")"

    STATE_SUITE=""
    save_my_state

    if (( suite_ret > overall_ret )); then
      overall_ret="${suite_ret}"
    fi

    local suite_end_ms
    suite_end_ms=$(get_epoch_ms)
    local suite_elapsed_ms="$(( suite_end_ms - suite_start_ms ))"
    printf "[----------] %d tests from %s (%d ms total)\n\n" \
      "${num_cases}" "${TEST_SUITE}" "${suite_elapsed_ms}" >&3
  done
  unset TEST_SUITE
  append_summary "result=$(result_from_ret "${overall_ret}")"

  local run_end_ms
  run_end_ms=$(get_epoch_ms)
  local run_elapsed_ms="$(( run_end_ms - run_start_ms ))"

  printf "[==========] %d tests from %d test suites ran. (%d ms total)\n" \
    "${total_cases}" "${total_suites}" "${run_elapsed_ms}" >&3
}

main() {
  init
  exec > "${RUN_DIR}/session.log" 2>&1 3>&1

  # Instantly compile the entire active roadmap using standardized traversal
  discover_plan "$@"

  if [[ ${#BSTE_RESOLVED_PLAN[@]} -eq 0 ]]; then
    echo "No test suites matched filters" >&3
    append_summary "result=SKIPPED"

    STATE_STATUS="FINISHED"
    save_my_state
    exit 0
  fi

  local run_ret=0
  (
    # run_test_plan should not change the status (RUNNING)
    readonly STATE_STATUS
    run_test_plan
  ) || run_ret="$?"

  if (( run_ret == 0 )); then
    STATE_STATUS="FINISHED"
  else
    STATE_STATUS="ERROR"
  fi
  save_my_state
}

main "$@"
