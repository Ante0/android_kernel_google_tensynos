#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# BSTE Frontend
# Handles management of test run execution, status tracking, and connection maintenance.

set -e

# Load shared state and I/O primitives
source "$(dirname "$(realpath "$0")")/common.sh"

readonly RUNNER="${HARNESS_DIR}/bste_test_runner.sh"

usage() {
  cat <<EOF
Usage: bste_test <command> [options] [arguments]

The Base System Test Environment (BSTE) frontend harness.

Commands:
  run [-d|--detach] [filter...] Start a new test session.
  attach                        Stream the execution log of current session.
  status [--raw] [run_id]       Display state of a session (defaults to current).
  summary [--raw] [run_id]      Display pass/fail results of a session (defaults to current).
  kill                          Terminate the active session.
  history                       View list of past execution sessions.
  list [filter...]              Discover installed test suites and test cases.
  log [run_id]                  Dump the full session log (defaults to latest).
  help                          Show this help message.

Examples:
  bste_test run
  bste_test run my_suite*
  bste_test status --raw
EOF
}

is_runner_alive() {
  local pid="$1"
  [[ -n "${pid}" ]] || return 1
  if pgrep -f "${RUNNER}" -s "${pid}" >/dev/null; then
    return 0
  fi
  return 1
}

finalize_runner_state() {
  local status="$1"

  load_state
  STATE_STATUS="${status}"
  save_state

  local pretty_id="${STATE_SUITE}"
  if [[ -n "${STATE_CASE}" ]]; then
    pretty_id="${pretty_id}.${STATE_CASE}"
  fi
  if [[ -n "${STATE_PHASE}" ]]; then
    echo "phase:${pretty_id}:${STATE_PHASE}=${status}" >> "${CURRENT_SUMMARY_FILE}"
  fi

  if [[ -n "${STATE_CASE}" ]]; then
    echo "result:${STATE_SUITE}.${STATE_CASE}=${status}" >> "${CURRENT_SUMMARY_FILE}"
  fi

  if [[ -n "${STATE_SUITE}" ]]; then
    echo "result:${STATE_SUITE}=${status}" >> "${CURRENT_SUMMARY_FILE}"
  fi

  echo "result=${status}" >> "${CURRENT_SUMMARY_FILE}"

  fsync "${CURRENT_SUMMARY_FILE}" >/dev/null 2>&1 || true
}

perform_recovery_if_needed() {
  load_state || return 0

  if [[ "${STATE_STATUS}" != "RUNNING" ]]; then
    return 0
  fi

  if ! is_runner_alive "${STATE_PID}"; then
    finalize_runner_state "CRASHED"
    echo "Notice: Detected crashed session." >&2
  fi
}

cmd_run() {
  if load_state; then
    if [[ "${STATE_STATUS}" == "RUNNING" ]] && is_runner_alive "${STATE_PID}"; then
      echo "Error: A session is already RUNNING (PID ${STATE_PID})" >&2
      exit 3
    fi
  fi

  local detach="false"
  local run_args
  run_args=()
  while (( $# > 0 )); do
    case "$1" in
      -d | --detach)
        detach="true"
        shift
        ;;
      *)
        run_args+=("$1")
        shift
        ;;
    esac
  done

  rm -f "${CURRENT_LINK}"
  nohup setsid "${RUNNER}" "${run_args[@]}" >/dev/null 2>&1 &

  local wait_count=0
  while (( wait_count < 10 )) && ! load_state; do
    sleep 0.1
    wait_count="$((wait_count + 1))"
  done

  echo "Started run ${STATE_RUN_ID} with PID ${STATE_PID}"

  if [[ "${detach}" == "false" ]]; then
    cmd_attach
  fi
}

cmd_attach() {
  if ! load_state; then
    echo "No current session." >&2
    exit 0
  fi

  local logfile="${CURRENT_LINK}/session.log"

  local wait_count=0
  while (( wait_count < 10 )) && [[ ! -f "${logfile}" ]]; do
    sleep 0.1
    wait_count="$((wait_count + 1))"
  done

  if [[ ! -f "${logfile}" ]]; then
    echo "Log file not found: ${logfile}" >&2
    exit 3
  fi

  echo "Attaching to run ${STATE_RUN_ID} (Ctrl+C to detach)..."

  tail -n +1 -f "${logfile}" &
  local tail_pid="$!"

  # shellcheck disable=SC2064
  trap "kill ${tail_pid} 2>/dev/null; echo; echo 'Detaching...'; exit 130" INT

  while true; do
    if ! is_runner_alive "${STATE_PID}"; then
      break
    fi
    sleep 0.5
  done

  # Wait a little bit more to clear the log.
  sleep 0.5
  kill "${tail_pid}" 2>/dev/null
  wait "${tail_pid}" 2>/dev/null || true
  trap - INT

  echo "Session detached (Completed)."
  cmd_summary
}

cmd_kill() {
  if ! load_state; then
    echo "No current session." >&2
    return 0
  fi

  if [[ "${STATE_STATUS}" != "RUNNING" ]] || [[ -z "${STATE_PID}" ]]; then
    echo "No active session to kill."
  fi

  echo "Killing session group (PID ${STATE_PID})"
  pkill -s "${STATE_PID}" || true

  local wait_count=0
  while (( wait_count < 10 )) && pgrep -s "${STATE_PID}" >/dev/null; do
    sleep 0.1
    wait_count="$((wait_count + 1))"
  done

  if pgrep -s "${STATE_PID}" >/dev/null; then
    echo "Failed to kill session" >&2
    exit 1
  fi

  echo "Killed"
  finalize_runner_state "KILLED"
}

cmd_status() {
  local raw="false"
  local run_id=""

  while (( $# > 0 )); do
    case "$1" in
      --raw)
        raw="true"
        shift
        ;;
      *)
        if [[ -n "${run_id}" ]]; then
          echo "Error: Multiple run IDs specified: ${run_id} and $1" >&2
          exit 3
        fi
        run_id="$1"
        shift
        ;;
    esac
  done

  local state_file=""
  if [[ -n "${run_id}" ]]; then
    state_file="${RUNS_DIR}/${run_id}/state.txt"
    if [[ ! -f "${state_file}" ]]; then
      echo "Error: Run ID '${run_id}' not found." >&2
      exit 3
    fi
  else
    state_file="${CURRENT_STATE_FILE}"
  fi

  if ! load_state "${state_file}"; then
    if [[ -n "${run_id}" ]]; then
      echo "Error: Failed to load state for run '${run_id}'" >&2
      exit 3
    else
      echo "No current session." >&2
      return 0
    fi
  fi

  if [[ "${raw}" == "true" ]]; then
    cat "${state_file}" 2>/dev/null || true
  else
    echo "=== BSTE Session Status ==="
    echo "Run ID:     ${STATE_RUN_ID}"
    echo "PID:        ${STATE_PID}"
    echo "Status:     ${STATE_STATUS}"
    echo "Test Suite: ${STATE_SUITE}"
    echo "Test Case:  ${STATE_CASE}"
    echo "Phase:      ${STATE_PHASE}"
  fi
}

cmd_summary() {
  local raw="false"
  local run_id=""

  while (( $# > 0 )); do
    case "$1" in
      --raw)
        raw="true"
        shift
        ;;
      *)
        if [[ -n "${run_id}" ]]; then
          echo "Error: Multiple run IDs specified: ${run_id} and $1" >&2
          exit 3
        fi
        run_id="$1"
        shift
        ;;
    esac
  done

  local state_file=""
  local summ_f=""
  if [[ -n "${run_id}" ]]; then
    state_file="${RUNS_DIR}/${run_id}/state.txt"
    summ_f="${RUNS_DIR}/${run_id}/summary.txt"
    if [[ ! -f "${state_file}" ]]; then
      echo "Error: Run ID '${run_id}' not found." >&2
      exit 3
    fi
  else
    state_file="${CURRENT_STATE_FILE}"
    summ_f="${CURRENT_LINK}/summary.txt"
  fi

  if ! load_state "${state_file}"; then
    if [[ -n "${run_id}" ]]; then
      echo "Error: Failed to load state for run '${run_id}'" >&2
      exit 3
    else
      echo "No current session." >&2
      return 0
    fi
  fi

  if [[ "${raw}" == "true" ]]; then
    cat "${summ_f}" 2>/dev/null || true
  else
    echo "=== Test Results ==="
    if [[ -f "${summ_f}" ]]; then
      local last_suite=""
      grep "^result:" "${summ_f}" 2>/dev/null | while read -r line; do
        local r="${line#result:}"
        local name="${r%%=*}"
        local result="${r#*=}"

        local cur_suite="${name%%.*}"
        if [[ -n "${last_suite}" ]] && [[ "${cur_suite}" != "${last_suite}" ]]; then
          echo ""
        fi
        last_suite="${cur_suite}"

        printf "[ %-7s ] %s\n" "${result}" "${name}"
      done

      echo ""
      # Summary counts
      local passed=0
      local failed=0
      local skipped=0
      local errors=0

      passed=$(grep -c "^result:[^:.]*\.[^:.]*=PASSED" "${summ_f}" 2>/dev/null || true)
      failed=$(grep -c "^result:[^:.]*\.[^:.]*=FAILED" "${summ_f}" 2>/dev/null || true)
      skipped=$(grep -c "^result:[^:.]*\.[^:.]*=SKIPPED" "${summ_f}" 2>/dev/null || true)
      errors=$(grep -c "^result:[^:.]*\.[^:.]*=ERROR" "${summ_f}" 2>/dev/null || true)

      echo "Summary: ${passed} passed, ${failed} failed," \
           "${skipped} skipped, ${errors} errors"
    else
      echo "No results yet."
    fi
  fi
}

cmd_list() {
  discover_plan "$@"

  local last_suite=""
  local entry

  for entry in "${BSTE_RESOLVED_PLAN[@]}"; do
    local suite="${entry%.*}"
    local case="${entry#*.}"

    if [[ "${suite}" != "${last_suite}" ]]; then
      echo "Test Suite: ${suite}"
      echo "  Test Cases:"
      last_suite="${suite}"
    fi

    echo "    - ${case}"
  done
}

cmd_history() {
  if [[ ! -d "${RUNS_DIR}" ]]; then
    return 0
  fi
  echo "RUN_ID                 | STATUS   "
  echo "-----------------------|----------"
  local state_file
  find "${RUNS_DIR}" -type f -name state.txt | sort -r | while read -r state_file; do
    local run_id
    local status
    run_id="$(grep "^RUN_ID=" "${state_file}" | cut -d'=' -f2)"
    status="$(grep "^STATUS=" "${state_file}" | cut -d'=' -f2)"
    printf "%-22s | %-8s\n" "${run_id}" "${status}"
  done
}

cmd_log() {
  local rid="$1"
  local lfile
  if [[ -z "${rid}" ]]; then
    if [[ -f "${CURRENT_LINK}/session.log" ]]; then
      lfile="${CURRENT_LINK}/session.log"
    else
      echo "No current session log." >&2
      exit 3
    fi
  else
    lfile="${RUNS_DIR}/${rid}/session.log"
  fi

  if [[ -f "${lfile}" ]]; then
    cat "${lfile}"
  else
    echo "Log file not found." >&2
    exit 3
  fi
}

main() {
  perform_recovery_if_needed

  local command="$1"
  shift

  case "${command}" in
    run)
      cmd_run "$@"
      ;;
    attach)
      cmd_attach "$@"
      ;;
    kill)
      cmd_kill "$@"
      ;;
    status)
      cmd_status "$@"
      ;;
    summary)
      cmd_summary "$@"
      ;;
    list)
      cmd_list "$@"
      ;;
    history)
      cmd_history "$@"
      ;;
    log)
      cmd_log "$@"
      ;;
    help|-h|--help)
      usage
      ;;
    *)
      usage >&2
      exit 3
      ;;
  esac
}

main "$@"
