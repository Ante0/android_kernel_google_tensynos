#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

files=($@)

BINDINGS_DIR="Documentation/devicetree/bindings/"

for file in ${files[@]}; do
  if [[ "${file: -4}" == ".yaml" || "${file:0:${#BINDINGS_DIR}}" ==  "${BINDINGS_DIR}" ]]; then
     dt-doc-validate -u ${BINDINGS_DIR} ${file}
  fi
done
