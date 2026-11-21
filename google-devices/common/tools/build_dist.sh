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

parameters=()
if [ "${BUILD_AOSP_KERNEL}" = "1" ]; then
  echo "WARNING: BUILD_AOSP_KERNEL is deprecated." \
    "Use --kernel_package=ack instead." >&2
  parameters=("--kernel_package=ack")
fi

if [ "${BUILD_STAGING_KERNEL}" = "1" ]; then
  echo "WARNING: BUILD_STAGING_KERNEL is deprecated." \
    "Use --kernel_package=staging instead." >&2
  parameters=("--kernel_package=staging")
fi

exec "${WORKSPACE_DIR}/tools/bazel" run \
  "${parameters[@]}" \
  --config="${DEVICE}" \
  "//private/devices/google/${DEVICE}:${DEVICE}/dist" \
  "$@"
