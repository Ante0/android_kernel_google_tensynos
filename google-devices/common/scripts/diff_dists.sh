# SPDX-License-Identifier: GPL-2.0-only
#!/bin/bash

# Use with grep -w
IGNORE_FILES=(
  'repo.prop'
  'multiple.intoto.jsonl'
  'manifest.*\.xml'
  'applied.prop'
  'BUILD_INFO'
  'COPIED'
)

# Use with grep -w
DIFF_FILES=(
  '\.config'
  'kernel-uapi-headers\.tar\.gz'
  '.*modules\.load'
  '.*modules\.blocklist'
  '.*\.dtb'
  '.*\.dtbo'
  'dtb\.img'
  'dtbo\.img'
)

DIST1=""
DIST2=""
VERBOSE=""

function usage() {
  cat <<EOF
usage: $0 [options] <DIST1_DIR> <DIST2_DIR>

Compare outputs of 2 kernel builds.

Options:
  -v: Verbose.
  -h: Show this help.
EOF
}

function show_usage_and_exit() {
  usage >&2
  exit "$1"
}

function info() {
  echo "[INFO]" "$@" >&2
}

function error() {
  echo "[ERROR]" "$@" >&2
}

function verbose() {
  if [[ -n "${VERBOSE}" ]]; then
    echo "[VERBOSE]" "$@" >&2
  fi
}

function parse_args() {
  while (( $# > 0 )); do
    case "$1" in
      -v)
        VERBOSE=1
        ;;
      -h)
        usage
        exit 0
        ;;
      -*)
        error "Unknown option: $1"
        show_usage_and_exit 1
        ;;
      *)
        if [[ -z "${DIST1}" ]]; then
          DIST1="$1"
        elif [[ -z "${DIST2}" ]]; then
          DIST2="$1"
        else
          error "Too many args"
          show_usage_and_exit 1
        fi
        ;;
    esac
    shift
  done

  if [[ -z "${DIST1}" ]]; then
    error "Missing <DIST1_DIR>"
    show_usage_and_exit 1
  elif [[ ! -d "${DIST1}" ]]; then
    error "${DIST1} is not a directory"
    show_usage_and_exit 1
  fi

  if [[ -z "${DIST2}" ]]; then
    error "Missing <DIST2_DIR>"
    show_usage_and_exit 1
  elif [[ ! -d "${DIST2}" ]]; then
    error "${DIST2} is not a directory"
    show_usage_and_exit 1
  fi
}

function main() {
  parse_args "$@"

  ret=0

  tmp="$(mktemp -d)"

  printf "%s\n" "${IGNORE_FILES[@]}" > "${tmp}/ignore_files.txt"
  printf "%s\n" "${DIFF_FILES[@]}" > "${tmp}/diff_files.txt"

  ls -A -1 "${DIST1}" | grep -w -v -f "${tmp}/ignore_files.txt" | sort > "${tmp}/list1.txt"
  ls -A -1 "${DIST2}" | grep -w -v -f "${tmp}/ignore_files.txt" | sort > "${tmp}/list2.txt"

  if ! diff "${tmp}/list1.txt" "${tmp}/list2.txt"; then
    info "Differences in file list"
    ret=1
  else
    verbose "No difference in file list"
  fi

  for f in $(cat "${tmp}/list1.txt" | grep -w -f "${tmp}/diff_files.txt"); do
    if ! diff "${DIST1}/${f}" "${DIST2}/${f}"; then
      info "Differences in ${f}"
      ret=1
    else
      verbose "No difference in ${f}"
    fi
  done

  rm -rf "${tmp}"

  if (( ret == 0 )); then
    verbose "No difference between 2 dists"
  fi

  return "${ret}"
}

main "$@"
