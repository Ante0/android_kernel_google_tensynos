#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/envsetup.sh"

DEVICE="$1"
shift

if [[ -z "${DEVICE}" ]]; then
  cat >&2 <<EOF
usage: $0 <device> [<options>]

Build the distribution package of a device.

EOF
  exit 1
fi

DEVICE_TARGET="//private/devices/google/${DEVICE}:${DEVICE}"

BAZEL_FLAGS=()
while (( $# > 0 )); do
  if [[ "$1" == "--" ]]; then
    break;
  fi
  BAZEL_FLAGS+=("$1")
  shift
done

echo "=== Base Kernel Info ==="
echo "Sources: $("${WORKSPACE_DIR}/tools/bazel" --quiet cquery \
  --config="${DEVICE}" \
  "filter(:kernel_.*_sources$, deps(${DEVICE_TARGET}/kernel_sources))" \
  "${BAZEL_FLAGS[@]}" | tail -n 1 | cut -d ' ' -f 1
)"
echo "Build: $("${WORKSPACE_DIR}/tools/bazel" --quiet cquery \
  --config="${DEVICE}" \
  "kind(\"(_kernel_build|kernel_filegroup) rule\", deps(${DEVICE_TARGET}/kernel))" \
  "${BAZEL_FLAGS[@]}" | tail -n 1 | cut -d ' ' -f 1
)"
echo "========================"

"${WORKSPACE_DIR}/tools/bazel" run \
  --config="${DEVICE}" \
  "${DEVICE_TARGET}/dist" \
  "${BAZEL_FLAGS[@]}" "$@"
