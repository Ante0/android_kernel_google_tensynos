#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

REMOTE_DIR="/data/local/tmp/bste_test_package"
REMOTE_TAR="${REMOTE_DIR}.tar.gz"
REMOTE_BIN="${REMOTE_DIR}/bste_test"

usage() {
  cat <<EOF
Usage: bste_test [global_options] <command> [args...]

Host-side tool to install and run BSTE test packages on an Android device.

Global Options:
  -s SERIAL             Set ANDROID_SERIAL to specify the target device.
  -p, --package PKG     Install a bste_test_package tarball onto the device before
                        running the command. Extracts into /data/local/tmp/bste_test_package.
  -t, --timeout SECS    Timeout for auto-reattach in seconds (default: 60).
  -h, --help            Show this help message.

Commands:
  <cmd> [args...]       Run /data/local/tmp/bste_test_package/bste_test <cmd> [args...]
                        on the device (e.g., run, attach, summary, status, kill, list).

Examples:
  bste_test -p out/my_package.tar.gz run -d my_suite*
  bste_test -s FA123456 summary
EOF
}

_has_detach_option() {
  local arg
  for arg in "$@"; do
    if [[ "${arg}" == "-d" || "${arg}" == "--detach" ]]; then
      return 0
    fi
  done
  return 1
}

_now() {
  echo "${EPOCHREALTIME%.*}"
}

_is_device_or_recovery() {
  local state
  state="$(adb get-state 2>/dev/null)" || true
  case "${state}" in
    device|recovery)
      return 0
      ;;
  esac
  return 1
}

_ensure_root() {
  # With old adb version, adb root might timeout in recovery mode.
  timeout 3 adb root || _is_device_or_recovery
}

auto_reattach() {
  local timeout="$1"

  echo ">>> Connection lost. Starting auto-reattach loop (timeout: ${timeout}s)..."

  while true; do
    local start_time
    start_time="$(_now)"
    local end_time="$(( start_time + timeout ))"

    local now
    local remaining
    local next_report_remaining="${timeout}"
    until _is_device_or_recovery && _ensure_root; do
      now="$(_now)"
      remaining="$(( end_time - now ))"
      if (( remaining <= 0 )); then
        echo "ERROR: Re-attach timed out after ${timeout} seconds." >&2
        exit 1
      fi

      if (( remaining <= next_report_remaining )); then
        echo ">>> Trying to reconnect... (${remaining}s remaining)"
        next_report_remaining="$(( next_report_remaining - 10 ))"
      fi

      sleep 0.5
    done

    echo ">>> Device reconnected. Attaching to session..."
    set +e
    adb shell "${REMOTE_BIN}" attach
    local ret=$?
    set -e

    if (( ret == 0 )); then
      echo ">>> Session completed."
      return 0
    elif (( ret == 130 )); then
      echo ">>> Detached by user."
      exit 130
    else
      echo ">>> Connection lost again during attach."
    fi
  done
}

main() {
  local package=""
  local timeout=60
  while (( $# > 0 )); do
    case "$1" in
      -s)
        export ANDROID_SERIAL="$2"
        shift 2
        ;;
      -p | --package)
        package="$2"
        shift 2
        ;;
      -t | --timeout)
        timeout="$2"
        shift 2
        ;;
      -h | --help)
        usage
        exit 0
        ;;
      --)
        shift
        break
        ;;
      -*)
        echo "ERROR: Unknown global option: $1" >&2
        usage >&2
        exit 1
        ;;
      *)
        break
        ;;
    esac
  done

  if (( $# == 0 )); then
    usage >&2
    exit 1
  fi

  echo ">>> Acquiring device root access..."
  _ensure_root

  if [[ -n "${package}" ]]; then
    if [[ ! -f "${package}" ]]; then
      echo "ERROR: Package file not found: ${package}" >&2
      exit 1
    fi

    echo ">>> Preparing remote directory: ${REMOTE_DIR}..."
    adb shell "rm -rf '${REMOTE_DIR}' '${REMOTE_TAR}' && mkdir -p '${REMOTE_DIR}'"

    echo ">>> Pushing package to device..."
    adb push "${package}" "${REMOTE_TAR}"

    echo ">>> Extracting package on device..."
    adb shell "tar -xzf '${REMOTE_TAR}' -C '${REMOTE_DIR}' && rm -f '${REMOTE_TAR}'"
    adb shell sync

    echo ">>> Installation completed successfully!"
  fi
  echo "----------------------------------------------------------------------"

  local cmd="$1"
  shift

  if [[ "${cmd}" == "run" ]] && ! _has_detach_option "$@"; then
    set +e
    adb shell "${REMOTE_BIN}" "run" "$@"
    local ret=$?
    set -e

    if (( ret != 0 )) && (( ret != 130 )); then
      auto_reattach "${timeout}"
    fi
  else
    adb shell "${REMOTE_BIN}" "${cmd}" "$@"
  fi
}

main "$@"
