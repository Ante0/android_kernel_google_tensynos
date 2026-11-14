#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

source "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/envsetup.sh"

DEVICE="$1"
shift

if [[ -z "${DEVICE}" ]]; then
  cat >&2 <<EOF
usage: $0 <device> [<options>]

Build the compile_commands.json of a device.

EOF
  exit 1
fi

exec "${WORKSPACE_DIR}/tools/bazel" run \
  --config="${DEVICE}" \
  "//private/devices/google/${DEVICE}:${DEVICE}/kernel_compile_commands" \
  "$@"
