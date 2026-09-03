#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Shared utility definitions for BSTE harness components.

# shellcheck disable=SC2155
readonly HARNESS_DIR="$(dirname "$(realpath "$0")")"
readonly PKG_DIR="$(dirname "${HARNESS_DIR}")"

readonly BSTE_RUN_ROOT="${TMPDIR:-/tmp}/bste_run"
# shellcheck disable=SC2034
readonly RUNS_DIR="${BSTE_RUN_ROOT}/runs"
readonly CURRENT_LINK="${BSTE_RUN_ROOT}/current"
readonly CURRENT_SUMMARY_FILE="${CURRENT_LINK}/summary.txt"
readonly CURRENT_STATE_FILE="${CURRENT_LINK}/state.txt"

set_current() {
  local run_id="$1"
  local run_dir="${BSTE_RUN_ROOT}/runs/${run_id}"
  if [[ ! -d "${run_dir}" ]]; then
    echo "${run_dir} not found" >&2
    exit 1
  fi
  ln -snf "runs/${run_id}" "${BSTE_RUN_ROOT}/current"
}

# Fast single-pass parser that safely extracts entire state snapshot into global cache.
load_state() {
  local state_file="${1:-${CURRENT_STATE_FILE}}"

  # Re-initialize clear state
  STATE_RUN_ID=""
  STATE_PID=""
  STATE_STATUS=""
  STATE_SUITE=""
  STATE_CASE=""
  STATE_PHASE=""

  [[ -f "${state_file}" ]] || return 1

  while IFS='=' read -r key val || [[ -n "${key}" ]]; do
    case "${key}" in
      RUN_ID) STATE_RUN_ID="${val}" ;;
      PID)    STATE_PID="${val}" ;;
      STATUS) STATE_STATUS="${val}" ;;
      SUITE)  STATE_SUITE="${val}" ;;
      CASE)   STATE_CASE="${val}" ;;
      PHASE)  STATE_PHASE="${val}" ;;
    esac
  done < "${state_file}"
}

save_state() {
  local state_file="${1:-${CURRENT_STATE_FILE}}"

  cat <<EOF > "${state_file}.tmp"
RUN_ID=${STATE_RUN_ID}
PID=${STATE_PID}
STATUS=${STATE_STATUS}
SUITE=${STATE_SUITE}
CASE=${STATE_CASE}
PHASE=${STATE_PHASE}
EOF
  mv "${state_file}.tmp" "${state_file}"
  fsync "${state_file}" >/dev/null 2>&1 || true
}

get_epoch_ms() {
  if [[ -n "${EPOCHREALTIME}" ]]; then
    # Remove the decimal point to get microseconds, then divide by 1000
    # to get milliseconds.
    local us="${EPOCHREALTIME/.}"
    echo "$(( us / 1000 ))"
  else
    # Fallback for shells without EPOCHREALTIME
    date +%s%3N
  fi
}

# Iterates through dynamic test suites, parses metadata, and compiles a unified,
# filter-matched Plan.
# Populates the global array: BSTE_RESOLVED_PLAN
discover_plan() {
  local suite_dir="${PKG_DIR}/test_suites"
  [[ -d "${suite_dir}" ]] || return 0

  # 1. Generate global unfiltered inventory in a SINGLE subshell for maximum velocity
  local raw_inventory
  raw_inventory=$( (
    local d
    for d in "${suite_dir}"/*; do
      [[ -d "${d}" ]] || continue
      local m="${d##*/}"
      [[ -f "${d}/bste_test_metadata.sh" ]] || continue

      # Safely source and cycle metadata sequentially within the same shell instance
      # shellcheck disable=SC1091
      source "${d}/bste_test_metadata.sh" >/dev/null 2>&1 || true

      local c
      for c in "${BSTE_TEST_CASES[@]}"; do
        echo "${m}.${c}"
      done
    done
  ) )

  # 2. Canonicalize user filter requests
  local filters
  filters=()
  local p
  for p in "$@"; do
    if [[ "${p}" != *"."* ]]; then
      filters+=("${p}.*")
    else
      filters+=("${p}")
    fi
  done

  # 3. Absolute minimal linear filtering pass
  BSTE_RESOLVED_PLAN=()
  local entry
  for entry in ${raw_inventory}; do
    if (( ${#filters[@]} == 0 )); then
      BSTE_RESOLVED_PLAN+=("${entry}")
      continue
    fi

    local pat
    for pat in "${filters[@]}"; do
      # shellcheck disable=SC2053
      if [[ "${entry}" == ${pat} ]]; then
        BSTE_RESOLVED_PLAN+=("${entry}")
        break
      fi
    done
  done
}
