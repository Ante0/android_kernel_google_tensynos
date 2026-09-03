#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

TARGET_BIN="$1"

if [ ! -f "${TARGET_BIN}" ]; then
  echo "ERROR: Target binary not found: ${TARGET_BIN}" >&2
  exit 1
fi

# Run readelf to verify architecture
MACHINE=$(llvm-readelf -h "${TARGET_BIN}" | grep "Machine:" | awk '{print $2}')

echo "Machine type detected: ${MACHINE}"

if [ "${MACHINE}" = "AArch64" ]; then
  echo "OK: Confirmed AArch64 architecture."
else
  echo "FAIL: Expected AArch64, found ${MACHINE}" >&2
  exit 1
fi

# Ensure binary is statically linked (no program interpreter requested)
if llvm-readelf -l "${TARGET_BIN}" | grep -q "program interpreter"; then
  echo "FAIL: Binary is dynamically linked (interpreter found)" >&2
  exit 1
fi

echo "OK: Confirmed binary is statically linked."
exit 0
