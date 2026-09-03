# SPDX-License-Identifier: GPL-2.0-only
# shellcheck shell=bash

if [[ -z "${_LOGGING_START_TIME:-}" ]]; then
  _LOGGING_START_TIME="${EPOCHREALTIME}"
fi

readonly _LOGGING_LEVEL_DEBUG=1
readonly _LOGGING_LEVEL_VERBOSE=2
readonly _LOGGING_LEVEL_INFO=3
readonly _LOGGING_LEVEL_WARNING=4
readonly _LOGGING_LEVEL_ERROR=5

_LOGGING_CUSTOM_PREFIXES=()

if [[ -z "${_LOGGING_LEVEL:-}" ]]; then
  _LOGGING_LEVEL=${_LOGGING_LEVEL_INFO}
fi

function logging::push_prefix() {
  _LOGGING_CUSTOM_PREFIXES+=("$1")
}

function logging::pop_prefix() {
  if (( ${#_LOGGING_CUSTOM_PREFIXES[@]} > 0 )); then
    unset '_LOGGING_CUSTOM_PREFIXES[-1]'
  fi
}

# NOTICE: Set USAGE before sourcing this file.
USAGE="${USAGE:-"<no usage provided>"}"

function logging::usage() {
  cat <<EOF
${USAGE}

LOGGING OPTIONS:
  --loglevel=<level>    Set the log level. <level> can be debug, verbose, info,
                        warning, or error.
  -q, --quiet           Quiet mode. Hide info and warning messages (sets
                        loglevel to error).
  --timestamp[=<format>]
                        Print timestamp at the beginning of each log.
                        <format> can be 'elapsed' (default) or 'datetime'
                        (absolute time).
  -v, --verbose         Verbose mode (sets loglevel to verbose).
  -h, --help            Show this help and exit.
EOF
}

function logging::_print_log() {
  local prefix="$1"
  shift
  if [[ "${_LOGGING_TIMESTAMP:-}" == "elapsed" ]]; then
    local now="${EPOCHREALTIME}"
    local diff=$(( ${now/./} - ${_LOGGING_START_TIME/./} ))
    local sec=$(( diff / 1000000 ))
    local frac=$(( (diff % 1000000) / 1000 ))
    printf -v prefix "[%5d.%03d]%s" "${sec}" "${frac}" "${prefix}"
  elif [[ "${_LOGGING_TIMESTAMP:-}" == "datetime" ]]; then
    prefix="[$(date '+%Y-%m-%d %H:%M:%S')]${prefix}"
  fi

  local msg="$*"
  local IFS=""
  local line
  while read -r line; do
    line="${_LOGGING_CUSTOM_PREFIXES[*]}${line}"
    printf "%s\n" "${prefix}${line:+ ${line}}" >&2
  done <<< "${msg}"
}

function logging::info() {
  if (( _LOGGING_LEVEL <= _LOGGING_LEVEL_INFO )); then
    logging::_print_log "[   INFO]" "$@"
  fi
}

function logging::warning() {
  if (( _LOGGING_LEVEL <= _LOGGING_LEVEL_WARNING )); then
    logging::_print_log "[WARNING]" "$@"
  fi
}

function logging::error() {
  if (( _LOGGING_LEVEL <= _LOGGING_LEVEL_ERROR )); then
    logging::_print_log "[  ERROR]" "$@"
  fi
}

function logging::debug() {
  if (( _LOGGING_LEVEL <= _LOGGING_LEVEL_DEBUG )); then
    logging::_print_log "[  DEBUG]" "$@"
  fi
}

function logging::verbose() {
  if (( _LOGGING_LEVEL <= _LOGGING_LEVEL_VERBOSE )); then
    logging::_print_log "[VERBOSE]" "$@"
  fi
}

function logging::usage_out() {
  logging::usage
  exit
}

function logging::error_out() {
  logging::error "$@"
  exit 128
}

function logging::error_usage_out() {
  logging::error "$@"
  logging::usage >&2
  exit 129
}

function logging::set_option() {
  case "$1" in
    --loglevel=*)
      local level="${1#*=}"
      case "${level}" in
        debug) _LOGGING_LEVEL=${_LOGGING_LEVEL_DEBUG} ;;
        verbose) _LOGGING_LEVEL=${_LOGGING_LEVEL_VERBOSE} ;;
        info) _LOGGING_LEVEL=${_LOGGING_LEVEL_INFO} ;;
        warning) _LOGGING_LEVEL=${_LOGGING_LEVEL_WARNING} ;;
        error) _LOGGING_LEVEL=${_LOGGING_LEVEL_ERROR} ;;
        *) logging::error_usage_out "Invalid --loglevel: ${level}" ;;
      esac
      return 0
      ;;
    -q|--quiet)
      _LOGGING_LEVEL=${_LOGGING_LEVEL_ERROR}
      return 0
      ;;
    --timestamp)
      _LOGGING_TIMESTAMP="elapsed"
      return 0
      ;;
    --timestamp=*)
      _LOGGING_TIMESTAMP="${1#*=}"
      if [[ "${_LOGGING_TIMESTAMP}" != "datetime" && "${_LOGGING_TIMESTAMP}" != "elapsed" ]]; then
        logging::error_usage_out "Invalid --timestamp format: ${_LOGGING_TIMESTAMP}"
      fi
      return 0
      ;;
    -v|--verbose)
      _LOGGING_LEVEL=${_LOGGING_LEVEL_VERBOSE}
      return 0
      ;;
    -h|--help)
      logging::usage_out
      ;;
    *)
      return 1
      ;;
  esac
}

function logging::set_option_or_out() {
  if ! logging::set_option "$1"; then
    logging::error_usage_out "Unknown option: $1"
  fi
}
