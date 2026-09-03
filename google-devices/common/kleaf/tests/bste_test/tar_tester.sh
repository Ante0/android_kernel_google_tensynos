#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

TAR_FILE="$1"
shift

echo "Checking contents of ${TAR_FILE}..."
LISTING=$(tar -tf "${TAR_FILE}" | sed 's/^\.\///' | sort)
echo "Found files:"
echo "${LISTING}"

MISSING=0
for EXPECTED in "$@"; do
  if ! echo "${LISTING}" | grep -q "^${EXPECTED}$"; then
    echo "ERROR: Missing expected file in tarball: ${EXPECTED}" >&2
    MISSING=1
  else
    echo "OK: Found ${EXPECTED}"
  fi
done

exit "${MISSING}"
