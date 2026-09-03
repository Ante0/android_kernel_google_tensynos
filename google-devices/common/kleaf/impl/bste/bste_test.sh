#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

exec sh "${SCRIPT_DIR}/harness/bste_test.sh" "$@"
