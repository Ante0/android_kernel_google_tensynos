#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

echo "=== BSTE Runtime Resilience Host Tests ==="

PACKAGE_TAR="$1"
if [ -z "${PACKAGE_TAR}" ]; then
  echo "ERROR: Missing package tarball argument" >&2
  exit 1
fi

# Create a local test sandbox workspace
SANDBOX_DIR="$(mktemp -d)"
echo "Created sandbox: ${SANDBOX_DIR}"

PKG_DIR="${SANDBOX_DIR}/pkg"
mkdir -p "${PKG_DIR}"

# Set custom TMPDIR for state isolation
export TMPDIR="${SANDBOX_DIR}/tmp"
mkdir -p "${TMPDIR}"

# Extract package
tar -xzf "${PACKAGE_TAR}" -C "${PKG_DIR}"

# Force use of bash instead of /bin/sh which might not be bash on some hosts
# TODO: Run the test with mksh instead of bash.
for f in "${PKG_DIR}/harness/"*.sh; do
  if [ -f "${f}" ]; then
    sed -i 's@#!/bin/sh@#!/bin/bash@' "${f}"
  fi
done

cd "${PKG_DIR}"
BSTE="./harness/bste_test.sh"

# shellcheck disable=SC2329
cleanup() {
  echo "Cleaning up sandbox..."
  rm -rf "${SANDBOX_DIR}"
}
trap 'cleanup' EXIT

CURRENT_DIR="${TMPDIR}/bste_run/current"
CURRENT_SUMMARY="${CURRENT_DIR}/summary.txt"
CURRENT_STATE="${CURRENT_DIR}/state.txt"

show_state() {
  echo "=== state.txt ==="
  cat "${CURRENT_STATE}"
}

show_summary() {
  echo "=== summary.txt ==="
  cat "${CURRENT_SUMMARY}"
}

show_state_and_summary() {
  show_state
  show_summary
}

wait_for_status() {
  local status="$1"
  local wait_count=15
  until grep -q "^STATUS=${status}" "${CURRENT_STATE}"; do
    if (( wait_count > 0 )); then
      wait_count="$(( wait_count - 1 ))"
      sleep 1
      continue
    fi
    echo ">>> Wait for status ${status} timeout"
    show_state
    return 1
  done
}

# ------------------------------------------------------------------------------
# Test: Execution Isolation (Subshell Scoping)
# ------------------------------------------------------------------------------
echo ">>> Testing Execution Isolation..."
GLOBAL_VAR="ORIGINAL" ""${BSTE}"" run --detach resilience_iso_suite

# Wait for completion
wait_for_status "FINISHED"

# Verify success
if grep -q "phase:resilience_iso_suite.test_case_2:TEST_CASE=OK" "${CURRENT_SUMMARY}" && \
   grep -q "result:resilience_iso_suite.test_case_2=PASSED" "${CURRENT_SUMMARY}"; then
  echo ">>> Execution Isolation SUCCESS"
else
  echo ">>> Execution Isolation FAILURE"
  show_summary
  exit 1
fi

# ------------------------------------------------------------------------------
# Test: Session Locking & Assertive Recovery
# ------------------------------------------------------------------------------
echo ">>> Testing Session Locking & Assertive Recovery..."

# Mock a dead session
DEAD_RUN="${TMPDIR}/bste_run/runs/dead_run"
mkdir -p "${DEAD_RUN}"
rm -f "${CURRENT_DIR}"
ln -s "runs/dead_run" "${CURRENT_DIR}"

cat << 'EOF' > "${DEAD_RUN}/state.txt"
RUN_ID=dead_run
PID=9999999
STATUS=RUNNING
PHASE=TEST_CASE
SUITE=mock_suite
CASE=mock_case
EOF
touch "${DEAD_RUN}/summary.txt"

# Running `status` should recover it
echo "Triggering recovery..."
"${BSTE}" status >/dev/null || true

# Check recovery status
if ! grep -q "STATUS=CRASHED" "${DEAD_RUN}/state.txt"; then
  echo ">>> Session Locking FAILURE: Session was not marked CRASHED"
  show_state
  exit 1
fi
if ! grep -q "result=CRASHED" "${DEAD_RUN}/summary.txt"; then
  echo ">>> Session Locking FAILURE: summary.txt missing crash status"
  show_summary
  exit 1
fi

# Now ensure new run succeeds
GLOBAL_VAR="ORIGINAL" "${BSTE}" run --detach resilience_iso_suite >/dev/null
NEW_RID=$(grep "^RUN_ID=" "${CURRENT_STATE}" | cut -d'=' -f2)
if [ "${NEW_RID}" = "dead_run" ]; then
  echo ">>> Session Locking FAILURE: Failed to start new run."
  exit 1
fi
echo ">>> Session Locking SUCCESS"

# Wait for current run to complete so it doesn't block next
wait_for_status "FINISHED"

# ------------------------------------------------------------------------------
# Test: Atomic Scoreboard & Persistence (Hard Crash Attribution)
# ------------------------------------------------------------------------------
echo ">>> Testing Atomic Scoreboard & Persistence..."
"${BSTE}" run --detach resilience_crash_suite >/dev/null

# Wait until phase is TEST_CASE
MAX_WAIT=10
CRASH_PID=""
while (( MAX_WAIT > 0 )); do
  CUR_PHASE="$(grep "^PHASE=" "${CURRENT_STATE}" | cut -d'=' -f2)"
  if [ "${CUR_PHASE}" = "TEST_CASE" ]; then
    CRASH_PID="$(grep "^PID=" "${CURRENT_STATE}" | cut -d'=' -f2)"
    if [[ -n "${CRASH_PID}" ]]; then break; fi
  fi
  sleep 1
  MAX_WAIT="$(( MAX_WAIT - 1 ))"
done

if [ -z "${CRASH_PID}" ]; then
  echo ">>> Atomic Scoreboard FAILURE: Timeout waiting for TEST_CASE"
  exit 1
fi

echo "Hard killing runner (PID ${CRASH_PID})..."
kill -9 -"${CRASH_PID}"
sleep 1

# Invoke status to trigger recovery
"${BSTE}" status --raw > /dev/null || true

if grep -q "phase:resilience_crash_suite.sleepy_case:TEST_CASE=CRASHED" "${CURRENT_SUMMARY}" && \
   grep -q "result:resilience_crash_suite.sleepy_case=CRASHED" "${CURRENT_SUMMARY}" && \
   grep -q "STATUS=CRASHED" "${CURRENT_STATE}"; then
  echo ">>> Atomic Scoreboard SUCCESS"
else
  echo ">>> Atomic Scoreboard FAILURE: Crash not attributed to phase correctly."
  show_state_and_summary
  exit 1
fi

# ------------------------------------------------------------------------------
# Test: Process Group Management
# ------------------------------------------------------------------------------
echo ">>> Testing Process Group Management..."
"${BSTE}" run --detach resilience_kill_suite >/dev/null

# Wait for pid record
ORPHAN_PID=""
MAX_WAIT=10
while (( MAX_WAIT > 0 )); do
  CUR_RUN="$(readlink -f "${CURRENT_DIR}" 2>/dev/null)"
  PID_F="${CUR_RUN}/test_suites/resilience_kill_suite/orphan_test/out/orphan.pid"
  if [ -f "${PID_F}" ]; then
    ORPHAN_PID="$(cat "${PID_F}")"
    break
  fi
  sleep 1
  MAX_WAIT="$(( MAX_WAIT - 1 ))"
done

if [ -z "${ORPHAN_PID}" ]; then
  echo ">>> Process Group Management FAILURE: Orphan PID not created."
  exit 1
fi

echo "Orphan process detected with PID ${ORPHAN_PID}."
if ! kill -0 "${ORPHAN_PID}" 2>/dev/null; then
  echo ">>> Process Group Management FAILURE: Orphan already dead prematurely."
  exit 1
fi

echo "Invoking bste kill..."
"${BSTE}" kill >/dev/null

if kill -0 "${ORPHAN_PID}" 2>/dev/null; then
  echo ">>> Process Group Management FAILURE: Orphan PID ${ORPHAN_PID} survived kill!"
  kill -9 "${ORPHAN_PID}" 2>/dev/null
  exit 1
else
  # Verify kill status in summary.txt
  if grep -q "result:resilience_kill_suite.orphan_test=KILLED" "${CURRENT_SUMMARY}" && \
     grep -q "phase:resilience_kill_suite.orphan_test:TEST_CASE=KILLED" "${CURRENT_SUMMARY}" && \
     grep -q "result=KILLED" "${CURRENT_SUMMARY}" && \
     grep -q "STATUS=KILLED" "${CURRENT_STATE}"; then
    echo ">>> Process Group Management SUCCESS: Orphan cleaned up and status logged."
  else
    echo ">>> Process Group Management FAILURE: Kill status not logged correctly."
    show_state_and_summary
    exit 1
  fi
fi

# ------------------------------------------------------------------------------
# Test: Rich Test API & Structured Results (GoogleTest style)
# ------------------------------------------------------------------------------
echo ">>> Testing Rich Test API & Structured Results..."
"${BSTE}" run --detach resilience_api_suite >/dev/null

# Wait for completion
wait_for_status "FINISHED"

# Assertions for summary.txt
# Test case results
grep -q "result:resilience_api_suite.test_success=PASSED" "${CURRENT_SUMMARY}"
grep -q "result:resilience_api_suite.test_failure=FAILED" "${CURRENT_SUMMARY}"
grep -q "result:resilience_api_suite.test_skip=SKIPPED" "${CURRENT_SUMMARY}"
grep -q "result:resilience_api_suite.test_error=ERROR" "${CURRENT_SUMMARY}"
grep -q "result:resilience_api_suite.test_asserts=PASSED" "${CURRENT_SUMMARY}"

# Suite result
grep -q "result:resilience_api_suite=ERROR" "${CURRENT_SUMMARY}"

# Phase status (all OK except test_error which is ERROR)
grep -q "phase:resilience_api_suite.test_success:TEST_CASE=OK" "${CURRENT_SUMMARY}"
grep -q "phase:resilience_api_suite.test_failure:TEST_CASE=OK" "${CURRENT_SUMMARY}"
grep -q "phase:resilience_api_suite.test_skip:TEST_CASE=OK" "${CURRENT_SUMMARY}"
grep -q "phase:resilience_api_suite.test_error:TEST_CASE=ERROR" "${CURRENT_SUMMARY}"

# Verify state sharing via SUITE_OUT and TEST_OUT
TEST_SHARED_F="${CURRENT_DIR}/test_suites/resilience_api_suite/test_success/out/shared_state.txt"
if [ ! -f "${TEST_SHARED_F}" ] || \
   [ "$(cat "${TEST_SHARED_F}")" != "state_data" ]; then
  echo ">>> Rich Test API FAILURE: TEST_OUT state sharing failed"
  exit 1
fi
SUITE_SHARED_F="${CURRENT_DIR}/test_suites/resilience_api_suite/out/suite_shared.txt"
if [ ! -f "${SUITE_SHARED_F}" ] || \
   [ "$(cat "${SUITE_SHARED_F}")" != "suite_state" ]; then
  echo ">>> Rich Test API FAILURE: SUITE_OUT state sharing failed"
  exit 1
fi

# Verify session.log contents (GoogleTest format and messages)
SESSION_LOG="${CURRENT_DIR}/session.log"
echo "=== session.log ==="
cat "${SESSION_LOG}"

# Verify RUN and completion lines
grep -q "\[ RUN      \] resilience_api_suite.test_success (TEST_CASE)" "${SESSION_LOG}"
grep -q "\[       OK \] resilience_api_suite.test_success (TEST_CASE)" "${SESSION_LOG}"

grep -q "\[ RUN      \] resilience_api_suite.test_failure (TEST_CASE)" "${SESSION_LOG}"
grep -q "\[  FAILED  \] resilience_api_suite.test_failure (TEST_CASE)" "${SESSION_LOG}"

grep -q "\[ RUN      \] resilience_api_suite.test_skip (TEST_CASE)" "${SESSION_LOG}"
grep -q "\[  SKIPPED \] resilience_api_suite.test_skip (TEST_CASE)" "${SESSION_LOG}"

grep -q "\[ RUN      \] resilience_api_suite.test_error (TEST_CASE)" "${SESSION_LOG}"
grep -q "\[  ERROR   \] resilience_api_suite.test_error (TEST_CASE)" "${SESSION_LOG}"

# Verify helper messages and line numbers are printed BEFORE completion brackets
if grep -q "resilience_suites/api_test.sh:6:" "${SESSION_LOG}"; then
  echo "ERROR: Found unwanted line number print for pass" >&2
  exit 1
fi
if grep -q "custom pass message" "${SESSION_LOG}"; then
  echo "ERROR: Found unwanted pass message" >&2
  exit 1
fi

# test_failure calls assert_eq on line 10
grep -q "resilience_suites/api_test.sh:10: fail" "${SESSION_LOG}"
grep -q "Expected equality of these values:" "${SESSION_LOG}"
grep -q "  Expected: expected_val" "${SESSION_LOG}"
grep -q "  Actual:   actual_val" "${SESSION_LOG}"

# test_skip calls skip on line 14
grep -q "resilience_suites/api_test.sh:14: skip" "${SESSION_LOG}"
grep -q "skip message" "${SESSION_LOG}"

# test_error calls error on line 20
grep -q "resilience_suites/api_test.sh:20: error" "${SESSION_LOG}"
grep -q "explicit error message" "${SESSION_LOG}"

# Verify bste_test summary output
echo "=== bste_test summary ==="
"${BSTE}" summary

# Verify summary output format
RESULT_OUT=$("${BSTE}" summary)
echo "${RESULT_OUT}" | grep -q "\[ PASSED  \] resilience_api_suite.test_success"
echo "${RESULT_OUT}" | grep -q "\[ FAILED  \] resilience_api_suite.test_failure"
echo "${RESULT_OUT}" | grep -q "\[ SKIPPED \] resilience_api_suite.test_skip"
echo "${RESULT_OUT}" | grep -q "\[ ERROR   \] resilience_api_suite.test_error"
echo "${RESULT_OUT}" | grep -q "\[ ERROR   \] resilience_api_suite$"
echo "${RESULT_OUT}" | grep -q "Summary: 2 passed, 1 failed, 1 skipped, 1 errors"

echo ">>> Rich Test API SUCCESS"

# ------------------------------------------------------------------------------
# Final Conclusion
# ------------------------------------------------------------------------------
echo ">>> ALL RUNTIME TESTS PASSED SUCCESSFULLY <<<"
exit 0
