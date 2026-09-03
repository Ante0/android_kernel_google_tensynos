#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

export GIT_PAGER=""

if [[ -t 1 ]] && [[ -z "${NO_COLOR}" ]]; then
  RED='\033[0;31m'
  GREEN='\033[0;32m'
  NC='\033[0m'
else
  RED=''
  GREEN=''
  NC=''
fi

FAILURES=0
TEST_FAILED=0

function pass() {
  if (( TEST_FAILED == 0 )); then
    echo -e "${GREEN}[PASS]${NC}"
  fi
  TEST_FAILED=0
}

function fail() {
  if (( TEST_FAILED == 0 )); then
    echo -e "${RED}[FAIL]${NC}" >&2
    TEST_FAILED=1
    FAILURES=$((FAILURES + 1))
  fi
  echo -e "${RED}Error: $1${NC}" >&2
}

function finish_tests() {
  local suite_name="${1:-}"
  if [[ -n "${suite_name}" ]]; then
    suite_name=" ${suite_name}"
  fi

  if (( FAILURES == 0 )); then
    echo -e "\n${GREEN}=== All${suite_name} tests passed! ===${NC}"
    exit 0
  else
    echo -e "\n${RED}=== ${FAILURES}${suite_name} failure(s) detected! ===${NC}"
    exit 1
  fi
}
