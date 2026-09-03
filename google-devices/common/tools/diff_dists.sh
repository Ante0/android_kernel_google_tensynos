#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/logging.sh"

USAGE="$(cat <<EOF
usage: $0 [<options>] <DIST1_DIR> <DIST2_DIR>

Compare outputs of 2 kernel builds.
EOF
)"

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
  '.*-uapi-headers\.tar\.gz'
  '.*modules\.load'
  '.*modules\.blocklist'
  '.*\.dtb'
  '.*\.dtbo'
  'dtb\.img'
  'dtbo\.img'
)

DIST1=""
DIST2=""

function parse_args() {
  while (( $# > 0 )); do
    case "$1" in
      -*)
        logging::set_option_or_out "$1"
        ;;
      *)
        if [[ -z "${DIST1}" ]]; then
          DIST1="$1"
        elif [[ -z "${DIST2}" ]]; then
          DIST2="$1"
        else
          logging::error_usage_out "Too many args"
        fi
        ;;
    esac
    shift
  done

  if [[ -z "${DIST1}" ]]; then
    logging::error_usage_out "Missing <DIST1_DIR>"
  elif [[ ! -d "${DIST1}" ]]; then
    logging::error_usage_out "${DIST1} is not a directory"
  fi

  if [[ -z "${DIST2}" ]]; then
    logging::error_usage_out "Missing <DIST2_DIR>"
  elif [[ ! -d "${DIST2}" ]]; then
    logging::error_usage_out "${DIST2} is not a directory"
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
    logging::info "Differences in file list"
    ret=1
  else
    logging::verbose "No difference in file list"
  fi

  for f in $(cat "${tmp}/list1.txt" | grep -w -f "${tmp}/diff_files.txt"); do
    if ! diff "${DIST1}/${f}" "${DIST2}/${f}"; then
      logging::info "Differences in ${f}"
      ret=1
    else
      logging::verbose "No difference in ${f}"
    fi
  done

  rm -rf "${tmp}"

  if (( ret == 0 )); then
    logging::verbose "No difference between 2 dists"
  fi

  return "${ret}"
}

main "$@"
