#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

SCRIPT_DIR=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
readonly SCRIPT_DIR
readonly GITTOOL="${SCRIPT_DIR}/../gittool.sh"

source "${SCRIPT_DIR}/test_base.sh"

function assert_find_commit_method() {
  local expected_commit="$1"
  local expected_method="$2"
  shift 2

  local match
  # Disable exit on error temporarily to ensure we can read the output
  # even if find_commit fails (though it shouldn't in these tests).
  set +e
  match=$("${GITTOOL}" find_commit --show-method --pretty=%H "$@" 2>/dev/null)
  local exit_code=$?
  set -e

  if (( exit_code != 0 )); then
    fail "find_commit failed with exit code ${exit_code}."
  fi

  local expected_out="Found equivalent commit by ${expected_method}
${expected_commit}"
  if [[ "${match}" != "${expected_out}" ]]; then
    fail "Expected output '${expected_out}', got '${match}'."
  fi
}

# Create a temporary workspace
TEST_DIR=$(mktemp -d)
REMOTE_DIR=$(mktemp -d)
trap 'rm -rf "${TEST_DIR}" "${REMOTE_DIR}"' EXIT

cd "${TEST_DIR}"
git init -q -b main
git config user.email "test@example.com"
git config user.name "Test User"

echo "Initializing test repository..."
# Set up a shared remote
git init -q -b main --bare "${REMOTE_DIR}"
git remote add origin "${REMOTE_DIR}"

# Create base commit
echo "base" > file
git add file
git commit -m "Initial base commit" -q
BASE_COMMIT=$(git rev-parse HEAD)

# --- Test Directory Execution (-C) ---
echo -n "Testing global options (-C)... "
mkdir -p sub_repo
pushd sub_repo >/dev/null
git init -q -b main
git config user.email "test@example.com"
git config user.name "Test User"
echo "sub" > file_sub
git add file_sub
git commit -m "Sub repo commit" -q
SUB_REPO_COMMIT=$(git rev-parse HEAD)
popd >/dev/null

# Run from outside
OUT=$("${GITTOOL}" -C sub_repo find_commit "${SUB_REPO_COMMIT}" 2>/dev/null)
if ! grep -q "${SUB_REPO_COMMIT:0:7}" <<<"${OUT}" && \
   ! grep -q "${SUB_REPO_COMMIT:0:12}" <<<"${OUT}"; then
  fail "Failed to run find_commit using -C. Output: ${OUT}"
fi
pass

# --- Test Global/Formatting Options Placement ---
echo -n "Testing global/format option placement... "
git checkout "${BASE_COMMIT}" -q
git checkout -b target_branch -q
echo "format" > file_format
git add file_format
git commit -m "Format msg" -q
TARGET_COMMIT=$(git rev-parse HEAD)

# Test before command
OUT_BEFORE=$("${GITTOOL}" --pretty=%H find_commit --show-method \
  "${TARGET_COMMIT}" target_branch 2>/dev/null)
if [[ "${OUT_BEFORE}" != "Found equivalent commit by ancestry
${TARGET_COMMIT}" ]]; then
  fail "Failed to parse formatting option before command. Got: ${OUT_BEFORE}"
fi

# Test after command
OUT_AFTER=$("${GITTOOL}" find_commit "${TARGET_COMMIT}" target_branch \
  --show-method --pretty=%H 2>/dev/null)
if [[ "${OUT_AFTER}" != "Found equivalent commit by ancestry
${TARGET_COMMIT}" ]]; then
  fail "Failed to parse formatting option after command. Got: ${OUT_AFTER}"
fi
pass

# --- Test fetch_if_needed ---
echo -n "Testing fetch_if_needed... "
# Create a commit and push to remote
git checkout -b fetch_src_branch -q
echo "fetch content" > fetch_file
git add fetch_file
git commit -m "fetch commit" -q
git push origin fetch_src_branch:fetch_branch -q

# Ensure local remote ref does not exist yet
git update-ref -d refs/remotes/origin/fetch_branch 2>/dev/null || true

# 1. No slash (should NOT fetch)
"${GITTOOL}" fetch_if_needed "some_local_ref" > /dev/null 2>&1
if git rev-parse --verify refs/remotes/origin/fetch_branch >/dev/null 2>&1; then
  fail "Should not have fetched without a slash."
fi

# 2. With slash (should fetch)
"${GITTOOL}" fetch_if_needed "origin/fetch_branch" > /dev/null 2>&1
if ! git rev-parse --verify refs/remotes/origin/fetch_branch >/dev/null 2>&1; then
  fail "Should have fetched remote reference."
fi

# 3. Empty reference (should fail)
set +e
"${GITTOOL}" fetch_if_needed "" > /dev/null 2>&1
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )); then
  fail "fetch_if_needed should have failed with empty reference."
fi

# 4. With slash but --no-fetch (should NOT fetch)
git update-ref -d refs/remotes/origin/fetch_branch 2>/dev/null || true
"${GITTOOL}" --no-fetch fetch_if_needed "origin/fetch_branch" > /dev/null 2>&1
if git rev-parse --verify refs/remotes/origin/fetch_branch >/dev/null 2>&1; then
  fail "Should not have fetched with --no-fetch."
fi

pass

# --- Test find_commit: Ancestry ---
echo -n "Testing find_commit (Ancestry)... "
# Commit Graph:
# * (main) Initial base commit
assert_find_commit_method "${BASE_COMMIT}" "ancestry" "${BASE_COMMIT}"
pass

# --- Test find_commit: Patch-ID (Equivalent Commits) ---
echo -n "Testing find_commit (Patch-ID)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b patch_id_side_branch -q
echo "change" >> file
git add file
git commit -m "Change on side branch" -q
PATCH_ID_SIDE_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b patch_id_head_branch -q
echo "change" >> file
git add file
git commit -m "Equivalent change on head branch" -q
PATCH_ID_HEAD_COMMIT=$(git rev-parse HEAD)

# Commit Graph:
# * (patch_id_head_branch) Equivalent change on head branch
# | * (patch_id_side_branch) Change on side branch
# |/
# * Initial base commit
assert_find_commit_method "${PATCH_ID_HEAD_COMMIT}" "patch-id" \
  "${PATCH_ID_SIDE_COMMIT}" patch_id_head_branch
pass

# --- Test find_commit: Change-Id ---
echo -n "Testing find_commit (Change-Id)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b change_id_side_branch -q
echo "change 3" >> file
git add file
git commit -F - <<EOF -q
Commit with Change-Id

Change-Id: I1234567890abcdef1234567890abcdef12345678
EOF
CHANGE_ID_SIDE_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b change_id_head_branch -q
echo "different content" >> file
git add file
git commit -F - <<EOF -q
Different content but same Change-Id

Change-Id: I1234567890abcdef1234567890abcdef12345678
EOF
CHANGE_ID_HEAD_COMMIT=$(git rev-parse HEAD)

# Commit Graph:
# * (change_id_head_branch) Different content but same Change-Id
# | * (change_id_side_branch) Commit with Change-Id
# |/
# * Initial base commit
assert_find_commit_method "${CHANGE_ID_HEAD_COMMIT}" "reference commit or Change-Id" \
  "${CHANGE_ID_SIDE_COMMIT}" change_id_head_branch
pass

# --- Test find_commit: Merged-In ---
echo -n "Testing find_commit (Merged-In)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b merged_in_side_branch -q
echo "change merged-in" >> file
git add file
git commit -F - <<EOF -q
Source commit with Merged-In

Merged-In: Iabcdef1234567890abcdef1234567890abcdef12
EOF
MERGED_IN_SIDE_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b merged_in_head_branch -q
echo "some other change" >> file
git add file
git commit -F - <<EOF -q
Target commit with Change-Id

Change-Id: Iabcdef1234567890abcdef1234567890abcdef12
EOF
MERGED_IN_HEAD_COMMIT=$(git rev-parse HEAD)

# Commit Graph:
# * (merged_in_head_branch) Target commit with Change-Id
# | * (merged_in_side_branch) Source commit with Merged-In
# |/
# * Initial base commit
assert_find_commit_method "${MERGED_IN_HEAD_COMMIT}" "reference commit or Change-Id" \
  "${MERGED_IN_SIDE_COMMIT}" merged_in_head_branch
pass

# --- Test find_commit: Upstream Reference (via Ancestry) ---
echo -n "Testing find_commit (Upstream Reference via Ancestry)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b upstream_ref_ancestry_upstream_branch -q
echo "upstream change" >> file
git add file
git commit -m "Original Upstream Commit" -q
UPSTREAM_REF_ANCESTRY_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b upstream_ref_ancestry_side_branch -q
echo "backport src" >> file
git add file
git commit -F - <<EOF -q
Backport source commit

[ Upstream commit ${UPSTREAM_REF_ANCESTRY_COMMIT} ]
EOF
UPSTREAM_REF_ANCESTRY_BACKPORT_COMMIT=$(git rev-parse HEAD)

# Commit Graph:
# * (upstream_ref_ancestry_upstream_branch) Original Upstream Commit
# | * (upstream_ref_ancestry_side_branch) Backport source commit
# |/
# * Initial base commit
assert_find_commit_method "${UPSTREAM_REF_ANCESTRY_COMMIT}" "upstream reference" \
  "${UPSTREAM_REF_ANCESTRY_BACKPORT_COMMIT}" upstream_ref_ancestry_upstream_branch
pass

# --- Test find_commit: Upstream Reference (via Grep) ---
echo -n "Testing find_commit (Upstream Reference via Grep)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b upstream_ref_grep_side_branch -q
echo "upstream change grep" >> file
git add file
git commit -m "Upstream Commit to Grep" -q
UP_COMMIT_TO_GREP=$(git rev-parse HEAD)

# Pattern 1: commit <hash> upstream.
git checkout "${BASE_COMMIT}" -q
git checkout -b upstream_ref_grep_head_branch_1 -q
echo "grep content 1" > file_grep1
git add file_grep1
git commit -F - <<EOF -q
Backport commit 1

commit ${UP_COMMIT_TO_GREP} upstream.
EOF
UPSTREAM_REF_GREP_HEAD_COMMIT_1=$(git rev-parse HEAD)
# Commit Graph:
# * (upstream_ref_grep_head_branch_1) Backport commit 1 (refs UP_COMMIT_TO_GREP)
# | * (upstream_ref_grep_side_branch) Upstream Commit to Grep
# |/
# * Initial base commit
assert_find_commit_method "${UPSTREAM_REF_GREP_HEAD_COMMIT_1}" "reference commit or Change-Id" \
  "${UP_COMMIT_TO_GREP}" upstream_ref_grep_head_branch_1

# Pattern 2: (cherry picked from commit <hash>...)
git checkout "${BASE_COMMIT}" -q
git checkout -b upstream_ref_grep_head_branch_2 -q
echo "grep content 2" > file_grep2
git add file_grep2
git commit -F - <<EOF -q
Backport commit 2

(cherry picked from commit ${UP_COMMIT_TO_GREP})
EOF
UPSTREAM_REF_GREP_HEAD_COMMIT_2=$(git rev-parse HEAD)
# Commit Graph:
# * (upstream_ref_grep_head_branch_2) Backport commit 2 (refs UP_COMMIT_TO_GREP)
# | * (upstream_ref_grep_side_branch) Upstream Commit to Grep
# |/
# * Initial base commit
assert_find_commit_method "${UPSTREAM_REF_GREP_HEAD_COMMIT_2}" "reference commit or Change-Id" \
  "${UP_COMMIT_TO_GREP}" upstream_ref_grep_head_branch_2

# Pattern 2 (variant): cherry-picked (with hyphen)
git checkout "${BASE_COMMIT}" -q
git checkout -b upstream_ref_grep_head_branch_hyphen -q
echo "grep content 2 hyphen" > file_grep2_hyphen
git add file_grep2_hyphen
git commit -F - <<EOF -q
Backport commit with hyphen

(cherry-picked from commit ${UP_COMMIT_TO_GREP})
EOF
UPSTREAM_REF_GREP_HEAD_COMMIT_HYPHEN=$(git rev-parse HEAD)
# Commit Graph:
# * (upstream_ref_grep_head_branch_hyphen) Backport commit with hyphen (refs UP_COMMIT_TO_GREP)
# | * (upstream_ref_grep_side_branch) Upstream Commit to Grep
# |/
# * Initial base commit
assert_find_commit_method "${UPSTREAM_REF_GREP_HEAD_COMMIT_HYPHEN}" \
  "reference commit or Change-Id" \
  "${UP_COMMIT_TO_GREP}" upstream_ref_grep_head_branch_hyphen

# Pattern 3: [ Upstream commit <hash> ]
git checkout "${BASE_COMMIT}" -q
git checkout -b upstream_ref_grep_head_branch_3 -q
echo "grep content 3" > file_grep3
git add file_grep3
git commit -F - <<EOF -q
Backport commit 3

[ Upstream commit ${UP_COMMIT_TO_GREP} ]
EOF
UPSTREAM_REF_GREP_HEAD_COMMIT_3=$(git rev-parse HEAD)
# Commit Graph:
# * (upstream_ref_grep_head_branch_3) Backport commit 3 (refs UP_COMMIT_TO_GREP)
# | * (upstream_ref_grep_side_branch) Upstream Commit to Grep
# |/
# * Initial base commit
assert_find_commit_method "${UPSTREAM_REF_GREP_HEAD_COMMIT_3}" "reference commit or Change-Id" \
  "${UP_COMMIT_TO_GREP}" upstream_ref_grep_head_branch_3
pass

# --- Test find_commit: --skip-patch-id ---
echo -n "Testing find_commit (--skip-patch-id)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b skip_patch_id_side_branch -q
echo "skip change" >> file
git add file
git commit -m "Skip source" -q
SKIP_PATCH_ID_SIDE_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b skip_patch_id_head_branch -q
echo "skip change" >> file
git add file
git commit -m "Skip destination" -q
SKIP_PATCH_ID_HEAD_COMMIT=$(git rev-parse HEAD)

# Commit Graph:
# * (skip_patch_id_head_branch) Skip destination
# | * (skip_patch_id_side_branch) Skip source
# |/
# * Initial base commit
if [[ "$("${GITTOOL}" find_commit --show-method --pretty=%H \
      "${SKIP_PATCH_ID_SIDE_COMMIT}" skip_patch_id_head_branch)" \
      != "Found equivalent commit by patch-id
${SKIP_PATCH_ID_HEAD_COMMIT}" ]]; then
  fail "Initial match failed (sanity check)."
fi

set +e
MATCH_SKIP=$("${GITTOOL}" find_commit --skip-patch-id --pretty=%H \
  "${SKIP_PATCH_ID_SIDE_COMMIT}" skip_patch_id_head_branch)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || [[ -n "${MATCH_SKIP}" ]]; then
  fail "--skip-patch-id should not have found a match \
(Exit Code: ${EXIT_CODE}, Match: ${MATCH_SKIP})."
fi
pass

# --- Test find_commit: Commit Not Found (Invalid Format) ---
echo -n "Testing find_commit (Invalid Format)... "
NON_EXISTENT_FORMAT="invalid_commit_string"
set +e
MATCH_INVALID=$("${GITTOOL}" find_commit --pretty=%H "${NON_EXISTENT_FORMAT}" 2>/dev/null)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || [[ -n "${MATCH_INVALID}" ]]; then
  fail "find_commit should have failed for invalid format \
(Exit Code: ${EXIT_CODE}, Match: ${MATCH_INVALID})."
fi
pass

# --- Test find_commit: Commit Not Found (Valid Hash, Not in Repo) ---
echo -n "Testing find_commit (Valid Hash, Not in Repo)... "
NON_EXISTENT_HASH="deadbeefdeadbeefdeadbeefdeadbeefdeadbeef"
set +e
MATCH_NOT_FOUND=$("${GITTOOL}" find_commit --pretty=%H "${NON_EXISTENT_HASH}" 2>/dev/null)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || [[ -n "${MATCH_NOT_FOUND}" ]]; then
  fail "find_commit should not have found a match for non-existent valid hash \
(Exit Code: ${EXIT_CODE}, Match: ${MATCH_NOT_FOUND})."
fi
pass

# --- Test find_commit: Grep Method (Valid Hash, Not in Repo) ---
echo -n "Testing find_commit (Grep Method, Not in Repo)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b grep_not_in_repo_head_branch -q
GREP_HASH="11223344556677889900aabbccddeeff11223344"
echo "grep content not in repo" > file_grep_not_in_repo
git add file_grep_not_in_repo
git commit -F - <<EOF -q
Backport commit not in repo

[ Upstream commit ${GREP_HASH} ]
EOF
GREP_NOT_IN_REPO_HEAD_COMMIT=$(git rev-parse HEAD)

assert_find_commit_method "${GREP_NOT_IN_REPO_HEAD_COMMIT}" "reference commit or Change-Id" \
  "${GREP_HASH}" grep_not_in_repo_head_branch
pass

# --- Test find_commit: --automerger ---
echo -n "Testing find_commit (--automerger)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b find_automerger_side_branch -q
echo "change" >> file
git add file
git commit -F - <<EOF -q
Commit with Merged-In for find_commit

Merged-In: I9999999999999999999999999999999999999999
EOF
FIND_AUTOMERGER_SIDE_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b find_automerger_head_branch -q
echo "other change" >> file
git add file
git commit -F - <<EOF -q
Commit with Change-Id for find_commit

Change-Id: I9999999999999999999999999999999999999999
EOF
FIND_AUTOMERGER_HEAD_COMMIT=$(git rev-parse HEAD)

# Commit Graph:
# * (find_automerger_head_branch) Commit with Change-Id for find_commit
# | * (find_automerger_side_branch) Commit with Merged-In for find_commit
# |/
# * Initial base commit

# Should find match by Merged-In -> Change-Id
assert_find_commit_method "${FIND_AUTOMERGER_HEAD_COMMIT}" "reference commit or Change-Id" \
  --automerger "${FIND_AUTOMERGER_SIDE_COMMIT}" find_automerger_head_branch

# Should NOT find match if we only have Change-Id on side branch (not Merged-In)
git checkout find_automerger_side_branch -q
echo "yet another change" >> file
git add file
git commit -F - <<EOF -q
Commit with Change-Id only for find_commit

Change-Id: I8888888888888888888888888888888888888888
EOF
FIND_AUTOMERGER_SIDE_COMMIT_2=$(git rev-parse HEAD)

git checkout find_automerger_head_branch -q
echo "more change" >> file
git add file
git commit -F - <<EOF -q
Commit with matching Change-Id for find_commit

Change-Id: I8888888888888888888888888888888888888888
EOF

set +e
MATCH=$("${GITTOOL}" find_commit --automerger \
  "${FIND_AUTOMERGER_SIDE_COMMIT_2}" find_automerger_head_branch 2>/dev/null)
EXIT_CODE=$?
set -e

if (( EXIT_CODE == 0 )) || [[ -n "${MATCH}" ]]; then
  fail "find_commit --automerger should not have found a match based on Change-Id only."
fi

pass

# --- Test find_commit: Limit ---
echo -n "Testing find_commit (Limit)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b limit_upstream_branch -q
echo "up change" > file_up
git add file_up
git commit -m "Upstream" -q
LIMIT_UPSTREAM_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b limit_head_branch -q

echo "backport content" > file_backport_limit
git add file_backport_limit
git commit -F - <<EOF -q
Backport commit 1

commit ${LIMIT_UPSTREAM_COMMIT} upstream.
EOF
LIMIT_BACKPORT_COMMIT=$(git rev-parse HEAD)

echo "commit 1" > file_c1
git add file_c1
git commit -m "Commit 1" -q
LIMIT_HEAD_COMMIT_1=$(git rev-parse HEAD)

echo "commit 2" > file_c2
git add file_c2
git commit -m "Commit 2" -q

# Commit Graph:
# * (limit_head_branch) Commit 2
# * Commit 1
# * Backport commit 1 (refs LIMIT_UPSTREAM_COMMIT)
# | * (limit_upstream_branch) Upstream
# |/
# * Initial base commit
MATCH_NO_LIMIT=$("${GITTOOL}" find_commit --show-method --pretty=%H \
  "${LIMIT_UPSTREAM_COMMIT}" limit_head_branch 2>/dev/null)
if [[ "${MATCH_NO_LIMIT}" != "Found equivalent commit by reference commit or Change-Id
${LIMIT_BACKPORT_COMMIT}" ]]; then
  fail "Search without limit should have found backport. Got: ${MATCH_NO_LIMIT}"
fi

set +e
MATCH_LIMIT=$("${GITTOOL}" find_commit --pretty=%H "${LIMIT_UPSTREAM_COMMIT}" limit_head_branch \
  "${LIMIT_HEAD_COMMIT_1}" 2>/dev/null)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || [[ -n "${MATCH_LIMIT}" ]]; then
  fail "Search with limit should NOT have found backport."
fi
pass

# --- Test find_missing_commits ---
git checkout "${BASE_COMMIT}" -q
git checkout -b missing_upstream_branch -q
git checkout -b missing_head_branch -q

# 1. Truly missing commits
echo -n "Testing find_missing_commits (Truly missing commits)... "
git checkout missing_head_branch -q
echo "missing 1" > file_missing1
git add file_missing1
git commit -m "Missing commit 1" -q
MISSING_COMMIT_1=$(git rev-parse HEAD)

echo "missing 2" > file_missing2
git add file_missing2
git commit -m "Missing commit 2" -q
MISSING_COMMIT_2=$(git rev-parse HEAD)

# Commit Graph:
# * (missing_head_branch) Missing commit 2
# * Missing commit 1
# * (missing_upstream_branch) Initial base commit
OUT=$("${GITTOOL}" find_missing_commits --pretty=%H \
  missing_upstream_branch missing_head_branch 2>/dev/null)
if ! grep -q "${MISSING_COMMIT_1}" <<<"${OUT}"; then
  fail "MISSING_COMMIT_1 should be present"
fi
if ! grep -q "${MISSING_COMMIT_2}" <<<"${OUT}"; then
  fail "MISSING_COMMIT_2 should be present"
fi
pass

# 2. Equivalent via Patch-ID
echo -n "Testing find_missing_commits (Equivalent via Patch-ID)... "
git checkout missing_head_branch -q
echo "patch-id content" > file_patchid
git add file_patchid
git commit -m "Head commit for patch-id" -q
HEAD_COMMIT_PATCH_ID=$(git rev-parse HEAD)

git checkout missing_upstream_branch -q
echo "patch-id content" > file_patchid
git add file_patchid
git commit -m "Upstream commit for patch-id" -q

# Commit Graph:
# * (missing_upstream_branch) Upstream commit for patch-id
# | * (missing_head_branch) Head commit for patch-id
# | * Missing commit 2
# | * Missing commit 1
# |/
# * Initial base commit
OUT=$("${GITTOOL}" find_missing_commits --pretty=%H \
  missing_upstream_branch missing_head_branch 2>/dev/null)
if grep -q "${HEAD_COMMIT_PATCH_ID}" <<<"${OUT}"; then
  fail "HEAD_COMMIT_PATCH_ID should have been filtered out (Patch-ID)"
fi
pass

# 3. Equivalent via Change-Id
echo -n "Testing find_missing_commits (Equivalent via Change-Id)... "
git checkout missing_head_branch -q
echo "change-id head" > file_changeid_head
git add file_changeid_head
git commit -F - <<EOF -q
Head commit with Change-Id

Change-Id: I1234567890123456789012345678901234567890
EOF
HEAD_COMMIT_CHANGE_ID=$(git rev-parse HEAD)

git checkout missing_upstream_branch -q
echo "change-id upstream" > file_changeid_up
git add file_changeid_up
git commit -F - <<EOF -q
Upstream commit with Change-Id

Change-Id: I1234567890123456789012345678901234567890
EOF

# Commit Graph:
# * (missing_upstream_branch) Upstream commit with Change-Id
# * Upstream commit for patch-id
# | * (missing_head_branch) Head commit with Change-Id
# | * Head commit for patch-id
# | * Missing commit 2
# | * Missing commit 1
# |/
# * Initial base commit
OUT=$("${GITTOOL}" find_missing_commits --pretty=%H \
  missing_upstream_branch missing_head_branch 2>/dev/null)
if grep -q "${HEAD_COMMIT_CHANGE_ID}" <<<"${OUT}"; then
  fail "HEAD_COMMIT_CHANGE_ID should have been filtered out (Change-Id)"
fi
pass

# 4. Equivalent via Upstream reference
echo -n "Testing find_missing_commits (Equivalent via Upstream reference)... "
git checkout missing_upstream_branch -q
echo "original upstream" > file_original
git add file_original
git commit -m "Original Upstream Commit" -q
MISSING_UPSTREAM_ORIGINAL_COMMIT=$(git rev-parse HEAD)

git checkout missing_head_branch -q
echo "backport content" > file_backport
git add file_backport
git commit -F - <<EOF -q
Backport to head

[ Upstream commit ${MISSING_UPSTREAM_ORIGINAL_COMMIT} ]
EOF
HEAD_COMMIT_BACKPORT=$(git rev-parse HEAD)

# Commit Graph:
# * (missing_upstream_branch) Original Upstream Commit
# * Upstream commit with Change-Id
# * Upstream commit for patch-id
# | * (missing_head_branch) Backport to head (refs Original Upstream Commit)
# | * Head commit with Change-Id
# | * Head commit for patch-id
# | * Missing commit 2
# | * Missing commit 1
# |/
# * Initial base commit
OUT=$("${GITTOOL}" find_missing_commits --pretty=%H \
  missing_upstream_branch missing_head_branch 2>/dev/null)
if grep -q "${HEAD_COMMIT_BACKPORT}" <<<"${OUT}"; then
  fail "HEAD_COMMIT_BACKPORT should have been filtered out (Upstream reference)"
fi
pass

# Verify no extra commits (should only be 2)
echo -n "Testing find_missing_commits (Total missing count)... "
# Commit Graph:
# * (missing_upstream_branch) Original Upstream Commit
# * Upstream commit with Change-Id
# * Upstream commit for patch-id
# | * (missing_head_branch) Backport to head (refs Original Upstream Commit)
# | * Head commit with Change-Id
# | * Head commit for patch-id
# | * Missing commit 2
# | * Missing commit 1
# |/
# * Initial base commit
COUNT=$(echo "${OUT}" | grep -c "^[0-9a-f]")
if (( COUNT != 2 )); then
  fail "Expected 2 missing commits, got ${COUNT}. Output:
  ${OUT}"
fi
pass

# 5. find_missing_commits with --automerger
echo -n "Testing find_missing_commits (--automerger)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b missing_automerger_upstream_branch -q
echo "automerger up" > file_au_up
git add file_au_up
git commit -F - <<EOF -q
Upstream commit with Change-Id for find_missing

Change-Id: I7777777777777777777777777777777777777777
EOF

git checkout "${BASE_COMMIT}" -q
git checkout -b missing_automerger_head_branch -q
echo "automerger head" > file_au_head
git add file_au_head
git commit -F - <<EOF -q
Head commit with Merged-In for find_missing

Merged-In: I7777777777777777777777777777777777777777
EOF
HEAD_COMMIT_AUTOMERGER=$(git rev-parse HEAD)

# Commit Graph:
# * (missing_automerger_head_branch) Head commit with Merged-In for find_missing
# | * (missing_automerger_upstream_branch) Upstream commit with Change-Id for find_missing
# |/
# * Initial base commit

# find_missing_commits checks if commits in HEAD are missing from upstream.
# In automerger mode, it should extract Merged-In from HEAD commit and search
# for Change-Id in upstream.
# Here, HEAD commit has Merged-In I777..., and upstream has Change-Id I777...
# So it SHOULD find a match and consider it NOT missing.
OUT=$("${GITTOOL}" find_missing_commits --automerger --pretty=%H \
  missing_automerger_upstream_branch missing_automerger_head_branch 2>/dev/null)

if grep -q "${HEAD_COMMIT_AUTOMERGER}" <<<"${OUT}"; then
  fail "HEAD_COMMIT_AUTOMERGER should have been filtered out by --automerger"
fi

# Now test that it DOES NOT filter out if only Change-Id matches (in HEAD)
git checkout missing_automerger_upstream_branch -q
echo "automerger up 2" > file_au_up_2
git add file_au_up_2
git commit -F - <<EOF -q
Upstream commit with Change-Id only for find_missing

Change-Id: I6666666666666666666666666666666666666666
EOF

git checkout missing_automerger_head_branch -q
echo "automerger head 2" > file_au_head_2
git add file_au_head_2
git commit -F - <<EOF -q
Head commit with matching Change-Id for find_missing

Change-Id: I6666666666666666666666666666666666666666
EOF
HEAD_COMMIT_AUTOMERGER_2=$(git rev-parse HEAD)

# Commit Graph:
# * (missing_automerger_head_branch) Head commit with matching Change-Id for find_missing
# * Head commit with Merged-In for find_missing
# | * (missing_automerger_upstream_branch) Upstream commit with Change-Id only for find_missing
# | * Upstream commit with Change-Id for find_missing
# |/
# * Initial base commit

# In automerger mode, find_missing_commits extracts Merged-In from HEAD commit.
# Here, HEAD commit 2 has only Change-Id, so extracted Merged-In is empty.
# It should NOT find a match in upstream, and thus HEAD commit 2 should be considered MISSING.
OUT=$("${GITTOOL}" find_missing_commits --automerger --pretty=%H \
  missing_automerger_upstream_branch missing_automerger_head_branch 2>/dev/null)

if ! grep -q "${HEAD_COMMIT_AUTOMERGER_2}" <<<"${OUT}"; then
  fail "HEAD_COMMIT_AUTOMERGER_2 should NOT have been filtered out by --automerger" \
       "(Change-Id only in HEAD)"
fi

pass

# Run find_missing_commits with --skip-patch-id
echo -n "Testing find_missing_commits (--skip-patch-id)... "
# Commit Graph:
# * (missing_upstream_branch) Original Upstream Commit
# * Upstream commit with Change-Id
# * Upstream commit for patch-id
# | * (missing_head_branch) Backport to head (refs Original Upstream Commit)
# | * Head commit with Change-Id
# | * Head commit for patch-id
# | * Missing commit 2
# | * Missing commit 1
# |/
# * Initial base commit
OUT_SKIP=$("${GITTOOL}" find_missing_commits --skip-patch-id --pretty=%H \
  missing_upstream_branch missing_head_branch 2>/dev/null)

# Verify equivalent commit via patch-id IS now present
if ! grep -q "${HEAD_COMMIT_PATCH_ID}" <<<"${OUT_SKIP}"; then
  fail "HEAD_COMMIT_PATCH_ID should be present with --skip-patch-id"
fi
pass

# --- Test merge conflict ---
echo -n "Testing merge conflict... "
git checkout main -q
git checkout -b conflict_base_branch -q
echo "base" > conflict_file
git add conflict_file
git commit -m "conflict base" -q

git checkout -b conflict_upstream_branch -q
echo "upstream" > conflict_file
git add conflict_file
git commit -m "conflict upstream" -q

git checkout conflict_base_branch -q
git checkout -b conflict_head_branch -q
echo "local" > conflict_file
git add conflict_file
git commit -m "conflict local" -q

# Commit Graph:
# * (conflict_head_branch) conflict local
# | * (conflict_upstream_branch) conflict upstream
# |/
# * (conflict_base_branch) conflict base
set +e
OUT=$("${GITTOOL}" merge conflict_upstream_branch 2>&1)
EXIT_CODE=$?
set -e

if (( EXIT_CODE == 0 )); then
  fail "merge should have failed with conflict. Output: ${OUT}"
fi
if ! grep -q "Merge failed. Resolve conflicts" <<<"${OUT}"; then
  fail "Expected conflict error message. Output: ${OUT}"
fi
git merge --abort >/dev/null 2>&1 || true
pass

# --- Test merge conflict with --continue-with-conflicts ---
echo -n "Testing merge conflict with --continue-with-conflicts... "
git checkout main -q
git checkout -b conflict_base_branch_cwc -q
echo "base" > conflict_file_cwc
git add conflict_file_cwc
git commit -m "conflict base" -q

git checkout -b conflict_upstream_branch_cwc -q
echo "upstream" > conflict_file_cwc
git add conflict_file_cwc
git commit -m "conflict upstream" -q

git checkout conflict_base_branch_cwc -q
git checkout -b conflict_head_branch_cwc -q
echo "local" > conflict_file_cwc
git add conflict_file_cwc
git commit -m "conflict local" -q

set +e
"${GITTOOL}" merge --continue-with-conflicts conflict_upstream_branch_cwc >/dev/null 2>&1
EXIT_CODE=$?
set -e
if (( EXIT_CODE != 2 )); then
  fail "Expected exit code 2, got ${EXIT_CODE}"
fi

# Verify commit message starts with [conflict]
COMMIT_MSG=$(git log -1 --pretty=%s)
if [[ "${COMMIT_MSG}" != "[conflict] "* ]]; then
  fail "Expected commit message to start with '[conflict] ', got '${COMMIT_MSG}'"
fi

# Verify conflict markers are in the file
if ! grep -q "<<<<<<< HEAD" conflict_file_cwc; then
  fail "Expected conflict markers in conflict_file_cwc"
fi
pass


# --- Test merge failure (unrelated histories) ---
echo -n "Testing merge failure (unrelated histories)... "
git checkout main -q
git checkout -b unrelated_head_branch -q
echo "head" > head_file
git add head_file
git commit -m "Head commit" -q

git checkout --orphan unrelated_upstream_branch -q
git rm -rf . -q
echo "unrelated" > unrelated_file
git add unrelated_file
git commit -m "unrelated commit" -q

git checkout unrelated_head_branch -q
# Commit Graph:
# * (unrelated_head_branch) Head commit
#
# * (unrelated_upstream_branch) unrelated commit
set +e
# There is a bug in older git versions that --right-only doesn't work correctly without
# --cherry-mark, which causes multi-way merge error instead of unrelated histories error.
# As a workaround, use --by-commit for this test case.
OUT=$("${GITTOOL}" merge --by-commit unrelated_upstream_branch 2>&1)
EXIT_CODE=$?
set -e

if (( EXIT_CODE != 128 )); then
  fail "merge should have failed with exit code 128. Got: ${EXIT_CODE}. Output: ${OUT}"
fi
if ! grep -q "refusing to merge unrelated histories" <<<"${OUT}"; then
  fail "Expected unrelated histories error message. Output: ${OUT}"
fi
pass

# --- Test merge (no --by-commit) ---
echo -n "Testing merge (no --by-commit)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b merge_all_head_branch -q

git checkout "${BASE_COMMIT}" -q
git checkout -b merge_all_upstream_branch -q
echo "commit 1" > file_all_1
git add file_all_1
git commit -m "All 1" -q
echo "commit 2" > file_all_2
git add file_all_2
git commit -m "All 2" -q

git checkout merge_all_head_branch -q
# Commit Graph:
# * (merge_all_upstream_branch) All 2
# * All 1
# * (merge_all_head_branch) Initial base commit
"${GITTOOL}" merge --pretty=%s merge_all_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%s | grep -q "Merge 2 commit(s) from merge_all_upstream_branch"; then
  fail "Merge subject should use the number of commits."
fi
pass

# --- Test merge (author preservation) ---
echo -n "Testing merge (author preservation)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b author_upstream_branch -q
git config user.name "Original Author"
git config user.email "author@example.com"
echo "author content" > author_file
git add author_file
git commit -m "Author Commit" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b author_head_branch -q
git config user.name "Test User"
git config user.email "test@example.com"

# 1. Single commit merge without --by-commit
# Commit Graph:
# * (author_upstream_branch) Author Commit
# * (author_head_branch) Initial base commit
"${GITTOOL}" merge author_upstream_branch > /dev/null 2>&1
if [[ "$(git log -1 --format='%an <%ae>')" != "Original Author <author@example.com>" ]]; then
  fail "Merge (standard) did not preserve author. Got: $(git log -1 --format='%an <%ae>')"
fi

# 2. Single commit merge with --by-commit
git checkout "${BASE_COMMIT}" -q
git branch -D author_head_branch -q
git checkout -b author_head_branch -q
# Commit Graph:
# * (author_upstream_branch) Author Commit
# * (author_head_branch) Initial base commit
"${GITTOOL}" merge --by-commit author_upstream_branch > /dev/null 2>&1
if [[ "$(git log -1 --format='%an <%ae>')" != "Original Author <author@example.com>" ]]; then
  fail "Merge (--by-commit) did not preserve author. Got: $(git log -1 --format='%an <%ae>')"
fi

# 3. Skip commit should NOT preserve author
git checkout "${BASE_COMMIT}" -q
git checkout -b author_skip_upstream_branch -q
git config user.name "Original Author"
git config user.email "author@example.com"
echo "skip content" > skip_file
git add skip_file
git commit -m "Skip Commit" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b author_skip_head_branch -q
git config user.name "Test User"
git config user.email "test@example.com"
# Create equivalent commit in head
echo "skip content" > skip_file
git add skip_file
git commit -m "Equivalent of Skip" -q

# Commit Graph:
# * (author_skip_head_branch) Equivalent of Skip
# | * (author_skip_upstream_branch) Skip Commit
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit author_skip_upstream_branch > /dev/null 2>&1
if [[ "$(git log -1 --format='%an <%ae>')" != "Test User <test@example.com>" ]]; then
  fail "Skip commit should NOT have preserved author. Got: $(git log -1 --format='%an <%ae>')"
fi

# 4. Multi-commit merge should NOT preserve author of any single commit
git checkout "${BASE_COMMIT}" -q
git checkout -b author_multi_upstream_branch -q
git config user.name "Original Author"
git config user.email "author@example.com"
echo "multi 1" > multi1
git add multi1
git commit -m "Multi 1" -q
echo "multi 2" > multi2
git add multi2
git commit -m "Multi 2" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b author_multi_head_branch -q
git config user.name "Test User"
git config user.email "test@example.com"

# Commit Graph:
# * (author_multi_head_branch) Initial base commit
# | * (author_multi_upstream_branch) Multi 2
# | * Multi 1
# |/
# * Initial base commit
"${GITTOOL}" merge author_multi_upstream_branch > /dev/null 2>&1
if [[ "$(git log -1 --format='%an <%ae>')" == "Original Author <author@example.com>" ]]; then
  fail "Merge (multi-commit) should NOT have preserved author."
fi
pass

# --- Test merge (--by-commit) ---
echo -n "Testing merge (--by-commit)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b by_commit_upstream_branch -q
echo "u1" > file_u1
git add file_u1
git commit -m "Upstream 1" -q

echo "u2" > file_u2
git add file_u2
git commit -m "Upstream 2" -q

echo "u3" > file_u3
git add file_u3
git commit -m "Upstream 3" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b by_commit_head_branch -q

# Create an equivalent of U2 in by_commit_head_branch
echo "u2" > file_u2
git add file_u2
git commit -m "Equivalent of U2" -q

# Add a "DO NOT MERGE ANYWHERE" commit to upstream
git checkout by_commit_upstream_branch -q
echo "u4" > file_u4
git add file_u4
git commit -m "DO NOT MERGE ANYWHERE: restricted" -q

echo "u5" > file_u5
git add file_u5
git commit -m "Upstream 5" -q

# Add a Change-Id commit to upstream
echo "u6" > file_u6
git add file_u6
git commit -F - <<EOF -q
Upstream 6 (Change-Id)

Change-Id: I6666666666666666666666666666666666666666
EOF

git checkout by_commit_head_branch -q
# Create an equivalent of U6 in by_commit_head_branch by Change-Id
echo "h2" > file_h2
git add file_h2
git commit -F - <<EOF -q
Equivalent of U6 by Change-Id

Change-Id: I6666666666666666666666666666666666666666
EOF

# Now merge (--by-commit)
# Commit Graph:
# * (by_commit_head_branch) Equivalent of U6 by Change-Id
# * Equivalent of U2
# | * (by_commit_upstream_branch) Upstream 6 (Change-Id)
# | * Upstream 5
# | * DO NOT MERGE ANYWHERE: restricted
# | * Upstream 3
# | * Upstream 2
# | * Upstream 1
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit --pretty=%s by_commit_upstream_branch > /dev/null 2>&1

# Check results
# Should have 6 merge commits after H2
# 1. Merge "Upstream 1"
# 2. Skip "Upstream 2" (Equivalent patch-id)
# 3. Merge "Upstream 3"
# 4. Skip "DO NOT MERGE ANYWHERE: restricted" (Keyword)
# 5. Merge "Upstream 5"
# 6. Skip "Upstream 6 (Change-Id)" (Equivalent Change-Id)

# M6 should be skip of U6
if ! git log -1 --format=%s | grep -q "Skip 1 commit(s) from by_commit_upstream_branch"; then
  fail "U6 was not skipped correctly (Change-Id) - subject mismatch"
fi
if ! git log -1 --format=%b | grep -q "Upstream 6 (Change-Id)"; then
  fail "U6 was not skipped correctly (Change-Id) - body mismatch"
fi
if ! git log -1 --format=%b | grep -q "Skip reason: Found equivalent commit"; then
  fail "U6 skip reason not found in body"
fi

# M5 should be merge of U5
if ! git log -1 --format=%s HEAD~1 | grep -q "MERGE: Upstream 5"; then
  fail "U5 was not merged correctly"
fi

# M4 should be skip of U4
if ! git log -1 --format=%s HEAD~2 | grep -q "Skip 1 commit(s) from by_commit_upstream_branch"; then
  fail "U4 was not skipped correctly - subject mismatch"
fi
if ! git log -1 --format=%b HEAD~2 | grep -q "DO NOT MERGE ANYWHERE: restricted"; then
  fail "U4 was not skipped correctly - body mismatch"
fi

# Check that the skip reason is in the body
if ! git log -1 --format=%b HEAD~2 | grep -q "Skip reason: DO NOT MERGE ANYWHERE"; then
  fail "U4 skip reason not found in body"
fi

# M2 should be skip of U2
if ! git log -1 --format=%s HEAD~4 | grep -q "Skip 1 commit(s) from by_commit_upstream_branch"; then
  fail "U2 was not skipped correctly - subject mismatch"
fi
if ! git log -1 --format=%b HEAD~4 | grep -q "Upstream 2"; then
  fail "U2 was not skipped correctly - body mismatch"
fi
if ! git log -1 --format=%b HEAD~4 | \
    grep -q "Skip reason: Found equivalent commit by batched patch-id result"; then
  fail "U2 skip reason not found in body"
fi
pass

# --- Test merge (--by-commit) (Dry Run) ---
echo -n "Testing merge (--by-commit) (Dry Run)... "
# U7 was created on by_commit_upstream_branch in previous test, but we already merged up to U6.
# Let's ensure we have a NEW commit on by_commit_upstream_branch.
git checkout by_commit_upstream_branch -q
echo "u7-dry-run" > file_u7_dry
git add file_u7_dry
git commit -m "Commit for dry-run only" -q

git checkout by_commit_head_branch -q
PRE_MERGE_HEAD=$(git rev-parse HEAD)

# Run with dry-run
# Output from logging functions goes to stderr
# Commit Graph:
# * (by_commit_head_branch) (already merged up to U6)
# | * (by_commit_upstream_branch) Commit for dry-run only
# | * ...
# |/
# * Initial base commit
OUT=$("${GITTOOL}" merge --by-commit --pretty=%s --dry-run by_commit_upstream_branch 2>&1)

if ! grep -q "MERGE: Commit for dry-run only" <<<"${OUT}"; then
  fail "U7_DRY should have been marked as merge in dry-run output. Output:
${OUT}"
fi

if [[ "$(git rev-parse HEAD)" != "${PRE_MERGE_HEAD}" ]]; then
    fail "Dry-run should not have changed HEAD"
fi
pass

# --- Test merge (--automerger) ---
echo -n "Testing merge (--automerger)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b automerger_upstream_branch -q

# Commit 1: Normal commit
echo "au1" > file_au1
git add file_au1
git commit -m "Automerger Upstream 1" -q

# Commit 2: Commit with Change-Id
echo "au2" > file_au2
git add file_au2
git commit -F - <<EOF -q
Automerger Upstream 2 (Change-Id)

Change-Id: I2222222222222222222222222222222222222222
EOF

# Commit 3: Commit with Merged-In
echo "au3" > file_au3
git add file_au3
git commit -F - <<EOF -q
Automerger Upstream 3 (Merged-In)

Merged-In: I3333333333333333333333333333333333333333
EOF

git checkout "${BASE_COMMIT}" -q
git checkout -b automerger_head_branch -q

# Create equivalent of Commit 2 by Change-Id in head
echo "ah2" > file_ah2
git add file_ah2
git commit -F - <<EOF -q
Equivalent of AU2 by Change-Id

Change-Id: I2222222222222222222222222222222222222222
EOF

# Create equivalent of Commit 3 by Change-Id in head (so it matches Merged-In of AU3)
echo "ah3" > file_ah3
git add file_ah3
git commit -F - <<EOF -q
Equivalent of AU3 by Change-Id

Change-Id: I3333333333333333333333333333333333333333
EOF

# Commit Graph:
# * (automerger_head_branch) Equivalent of AU3 by Change-Id
# * Equivalent of AU2 by Change-Id
# | * (automerger_upstream_branch) Automerger Upstream 3 (Merged-In)
# | * Automerger Upstream 2 (Change-Id)
# | * Automerger Upstream 1
# |/
# * Initial base commit
git checkout automerger_head_branch -q
"${GITTOOL}" merge --automerger --pretty=%s automerger_upstream_branch > /dev/null 2>&1

# Verify results
if ! git log -1 --format=%s | grep -q "Skip 1 commit(s) from automerger_upstream_branch"; then
  fail "AU3 was not skipped correctly (Merged-In) - subject mismatch"
fi
if ! git log -1 --format=%b | grep -q "Automerger Upstream 3 (Merged-In)"; then
  fail "AU3 was not skipped correctly (Merged-In) - body mismatch"
fi

if ! git log -1 --format=%s HEAD~1 | grep -q "MERGE: Automerger Upstream 2 (Change-Id)"; then
  fail "AU2 should have been merged (Change-Id ignored)"
fi

if ! git log -1 --format=%s HEAD~2 | grep -q "MERGE: Automerger Upstream 1"; then
  fail "AU1 was not merged correctly"
fi

pass

# --- Test merge (--by-commit) (Merge a Merge) ---
echo -n "Testing merge (--by-commit) (Merge a Merge)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b complex_upstream_branch -q

# Side branch with two commits
git checkout -b complex_side_branch -q
echo "f1" > file_f1
git add file_f1
git commit -m "Feature 1" -q
echo "f2" > file_f2
git add file_f2
git commit -m "Feature 2" -q

# Merge side branch into upstream
git checkout complex_upstream_branch -q
git merge complex_side_branch --no-ff -m "Merge feature branch" -q

# Add one more commit
echo "u1" > file_u1
git add file_u1
git commit -m "Upstream 1" -q

# Prepare destination branch with F1 equivalent
git checkout "${BASE_COMMIT}" -q
git checkout -b complex_head_branch_normal -q
echo "f1" > file_f1
git add file_f1
git commit -m "Feature 1 equivalent" -q

# 1. Test First-Parent Mode (Default)
# Commit Graph:
# * (complex_head_branch_normal) Feature 1 equivalent
# | * (complex_upstream_branch) Upstream 1
# | * Merge feature branch
# | |\
# | | * (complex_side_branch) Feature 2
# | | * Feature 1
# | |/
# |/
# * Initial base commit
"${GITTOOL}" merge --first-parent --pretty=%s complex_upstream_branch > /dev/null 2>&1

if ! git log --format=%s | grep -q "MERGE: Merge feature branch"; then
    fail "First-parent mode: Merge feature branch should have been merged."
fi
if ! git log --format=%s | grep -q "MERGE: Upstream 1"; then
    fail "First-parent mode: Upstream 1 should have been merged."
fi

# 2. Test Flatten Mode
git checkout "${BASE_COMMIT}" -q
git checkout -b complex_head_branch_flatten -q
echo "f1" > file_f1
git add file_f1
git commit -m "Feature 1 equivalent" -q

# Commit Graph:
# * (complex_head_branch_flatten) Feature 1 equivalent
# | * (complex_upstream_branch) Upstream 1
# | * Merge feature branch
# | |\
# | | * (complex_side_branch) Feature 2
# | | * Feature 1
# | |/
# |/
# * Initial base commit
"${GITTOOL}" merge --flatten --pretty=%s complex_upstream_branch > /dev/null 2>&1

# Should see "Feature 1", "Feature 2", "Upstream 1".
# - Skip "Feature 1" (Equivalent patch-id)
# - Merge "Feature 2"
# - Skip "Merge feature branch" (Omitted in flatten mode)
# - Merge "Upstream 1"
if ! git log --format=%s | grep -q "Skip 1 commit(s) from complex_upstream_branch"; then
    fail "Flatten mode: Feature 1 should have been skipped - subject mismatch"
fi
if ! git log --format=%b | grep -q "Feature 1"; then
    fail "Flatten mode: Feature 1 should have been skipped - body mismatch"
fi
if ! git log --format=%b | \
    grep -q "Skip reason: Found equivalent commit by batched patch-id result"; then
    fail "Flatten mode: Feature 1 skip reason should be \
Found equivalent commit by batched patch-id result."
fi
if ! git log --format=%s | grep -q "MERGE: Feature 2"; then
    fail "Flatten mode: Feature 2 should have been merged."
fi
if git log --format=%s | grep -q "SKIP: Merge feature branch"; then
    fail "Flatten mode: Merge feature branch should have been omitted."
fi
if ! git log --format=%s | grep -q "MERGE: Upstream 1"; then
    fail "Flatten mode: Upstream 1 should have been merged."
fi
pass

# --- Test merge (Named Upstream: Branch, Tag, Remote) ---
echo -n "Testing merge (Named Upstream: Branch, Tag, Remote)... "

# 1. Local Branch
git checkout "${BASE_COMMIT}" -q
git checkout -b named_upstream_branch -q
echo "branch content 1" > file_branch1
git add file_branch1
git commit -m "branch commit 1" -q
echo "branch content 2" > file_branch2
git add file_branch2
git commit -m "branch commit 2" -q
NAMED_UPSTREAM_COMMIT=$(git rev-parse HEAD)

git checkout "${BASE_COMMIT}" -q
git checkout -b named_head_branch -q
"${GITTOOL}" merge named_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%s | grep -q "Merge 2 commit(s) from named_upstream_branch"; then
  fail "Merge subject should include local branch name."
fi

# 2. Tag
git checkout "${BASE_COMMIT}" -q
git checkout -b named_upstream_tag_branch -q
echo "tag content 1" > file_tag1
git add file_tag1
git commit -m "tag commit 1" -q
echo "tag content 2" > file_tag2
git add file_tag2
git commit -m "tag commit 2" -q
git tag TEST_TAG

git checkout "${BASE_COMMIT}" -q
git checkout -b named_head_tag_branch -q
"${GITTOOL}" merge TEST_TAG > /dev/null 2>&1

if ! git log -1 --format=%s | grep -q "Merge 2 commit(s) from TEST_TAG"; then
  fail "Merge subject should include tag name."
fi

# 3. Remote Branch
git checkout "${BASE_COMMIT}" -q
git checkout -b named_remote_src_branch -q
echo "remote content 1" > file_remote1
git add file_remote1
git commit -m "remote commit 1" -q
echo "remote content 2" > file_remote2
git add file_remote2
git commit -m "remote commit 2" -q
git push origin named_remote_src_branch:named_remote_branch -q

git checkout "${BASE_COMMIT}" -q
git checkout -b named_head_remote_branch -q

git fetch origin named_remote_branch -q

"${GITTOOL}" merge origin/named_remote_branch > /dev/null 2>&1

if ! git log -1 --format=%s | grep -q "Merge 2 commit(s) from named_remote_branch"; then
  fail "Merge subject should include stripped remote branch name."
fi

# 4. Raw Hash (No 'from')
git checkout "${BASE_COMMIT}" -q
git checkout -b named_head_hash_branch -q
"${GITTOOL}" merge "${NAMED_UPSTREAM_COMMIT}" > /dev/null 2>&1

if ! git log -1 --format=%s | grep -q "Merge 2 commit(s)"; then
  fail "Merge subject should be 'Merge 2 commit(s)'."
fi
if git log -1 --format=%s | grep -q "from"; then
  fail "Merge subject should NOT include 'from' for a raw hash."
fi
pass

# --- Test merge (--by-commit) (Batch Skip) ---
echo -n "Testing merge (--by-commit) (Batch Skip)... "

git checkout "${BASE_COMMIT}" -q
git checkout -b batch_skip_upstream_branch -q

echo "c1" > file_c1
git add file_c1
git commit -m "Commit 1" -q

echo "c2" > file_c2
git add file_c2
git commit -m "Commit 2" -q

echo "c3" > file_c3
git add file_c3
git commit -m "Commit 3" -q

echo "c4" > file_c4
git add file_c4
git commit -m "Commit 4" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b batch_skip_head_branch -q

# Create equivalents of C1, C2, and C4 in head
echo "c1" > file_c1
git add file_c1
git commit -m "Equivalent of C1" -q

echo "c2" > file_c2
git add file_c2
git commit -m "Equivalent of C2" -q

# Notice C3 is missing from head

echo "c4" > file_c4
git add file_c4
git commit -m "Equivalent of C4" -q

# Commit Graph:
# * (batch_skip_head_branch) Equivalent of C4
# * Equivalent of C2
# * Equivalent of C1
# | * (batch_skip_upstream_branch) Commit 4
# | * Commit 3
# | * Commit 2
# | * Commit 1
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit --batch-skip --pretty=%s batch_skip_upstream_branch > /dev/null 2>&1

# Check results:
# C1 and C2 should be skipped and batched into a single skip commit.
# C3 should be merged.
# C4 should be skipped (a single skip commit because it's at the end).

# Let's verify the commit history on batch_skip_head_branch
# HEAD    -> skip C4
# HEAD~1  -> merge C3
# HEAD~2  -> skip C1 & C2
# HEAD~3  ... our base equivalents

if ! git log -1 --format=%s HEAD~2 | \
     grep -q "Skip 2 commit(s) from batch_skip_upstream_branch"; then
  fail "C1 and C2 were not batched correctly."
fi

if ! git log -1 --format=%s HEAD~1 | grep -q "MERGE: Commit 3"; then
  fail "C3 was not merged correctly."
fi

if ! git log -1 --format=%s HEAD | grep -q "Skip 1 commit(s) from batch_skip_upstream_branch"; then
  fail "C4 was not skipped correctly as a single batch skip - subject mismatch"
fi
if ! git log -1 --format=%b HEAD | grep -q "Commit 4"; then
  fail "C4 was not skipped correctly as a single batch skip - body mismatch"
fi

pass

# --- Test merge (--by-commit) (--bug-id) ---
echo -n "Testing merge (--by-commit) (--bug-id)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b bug_id_upstream_branch -q
echo "b1" > file_b1
git add file_b1
git commit -m "Bugid Commit 1" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b bug_id_head_branch -q

# Commit Graph:
# * (bug_id_upstream_branch) Bugid Commit 1
# * (bug_id_head_branch) Initial base commit
"${GITTOOL}" merge --by-commit --bug-id=1234567 bug_id_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%b HEAD | grep -q "Bug: 1234567"; then
  fail "Bug ID was not appended to the commit message."
fi
pass

# --- Test merge (--by-commit) (--ignore-keywords) ---
echo -n "Testing merge (--by-commit) (--ignore-keywords)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b ignore_kw_upstream_branch -q
echo "ik1" > file_ik1
git add file_ik1
git commit -m "DO NOT MERGE ANYWHERE: but actually merge" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b ignore_kw_head_branch -q

# Commit Graph:
# * (ignore_kw_upstream_branch) DO NOT MERGE ANYWHERE: but actually merge
# * (ignore_kw_head_branch) Initial base commit
"${GITTOOL}" merge --by-commit --ignore-keywords --pretty=%s \
  ignore_kw_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%s HEAD | \
     grep -q "MERGE: DO NOT MERGE ANYWHERE: but actually merge"; then
  fail "Commit was skipped despite --ignore-keywords"
fi
pass

# --- Test merge (--by-commit) (--skip-patch-id) ---
echo -n "Testing merge (--by-commit) (--skip-patch-id)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b merge_skip_patch_id_upstream_branch -q
echo "patch A content" > file_patch_a
git add file_patch_a
git commit -m "Patch A on upstream" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b merge_skip_patch_id_head_branch -q
echo "patch A content" > file_patch_a
git add file_patch_a
git commit -m "Patch A on head" -q

# Commit Graph:
# * (merge_skip_patch_id_head_branch) Patch A on head
# | * (merge_skip_patch_id_upstream_branch) Patch A on upstream
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit --skip-patch-id --pretty=%s \
  merge_skip_patch_id_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%s HEAD | grep -q "MERGE: Patch A on upstream"; then
  fail "Commit was skipped by patch-id despite --skip-patch-id"
fi
pass

# --- Test merge (--by-commit) (No Common Ancestor) ---
echo -n "Testing merge (--by-commit) (No Common Ancestor)... "
git checkout --orphan disjoint_upstream_branch -q
git rm -rf . -q
echo "disjoint" > file_disjoint
git add file_disjoint
git commit -m "Disjoint commit" -q

git checkout --orphan disjoint_head_branch -q
git rm -rf . -q
echo "head" > file_head
git add file_head
git commit -m "Head commit" -q

# Commit Graph:
# * (disjoint_head_branch) Head commit
#
# * (disjoint_upstream_branch) Disjoint commit
set +e
"${GITTOOL}" merge --by-commit disjoint_upstream_branch > /dev/null 2>&1
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )); then
  fail "merge (--by-commit) should fail on unrelated histories without --allow-unrelated-histories"
fi
pass

# --- Test merge (--by-commit) (Equivalent patch in merged branch) ---
echo -n "Testing merge (--by-commit) (Equivalent patch in merged branch)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b eq_merged_upstream_branch -q
echo "patch A content" > file_patch_a
git add file_patch_a
git commit -m "Patch A" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b eq_merged_side_branch -q
echo "patch A content" > file_patch_a
git add file_patch_a
git commit -m "Patch B" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b eq_merged_head_branch -q
git merge eq_merged_side_branch --no-ff -m "Merge Patch B" -q

# Commit Graph:
# *   (eq_merged_head_branch) Merge Patch B
# |\
# | * (eq_merged_side_branch) Patch B (Same patch-id as Patch A)
# |/
# | * (eq_merged_upstream_branch) Patch A
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit --pretty=%s eq_merged_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%s HEAD | grep -q "Skip 1 commit(s) from eq_merged_upstream_branch"; then
  fail "Patch A was not skipped when its equivalent Patch B is merged into HEAD - subject mismatch"
fi
if ! git log -1 --format=%b HEAD | grep -q "Patch A"; then
  fail "Patch A was not skipped when its equivalent Patch B is merged into HEAD - body mismatch"
fi
pass

# --- Test merge (--by-commit) (Nothing to Merge) ---
echo -n "Testing merge (--by-commit) (Nothing to Merge)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b nothing_upstream_branch -q
echo "nothing" > file_nothing
git add file_nothing
git commit -m "Nothing commit" -q

git checkout -b nothing_head_branch -q
# nothing_head_branch is now identical to nothing_upstream_branch (or ahead of it)
# Commit Graph:
# * (HEAD -> nothing_head_branch, nothing_upstream_branch) Nothing commit
# * Initial base commit
OUT=$("${GITTOOL}" merge --by-commit nothing_upstream_branch 2>&1)
if ! grep -q "Nothing to merge" <<<"${OUT}"; then
  fail "Should have output 'Nothing to merge'. Output: ${OUT}"
fi
pass

# --- Test merge (--by-commit) (Multi-way merge) ---
echo -n "Testing merge (--by-commit) (Multi-way merge)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b multi_way_upstream_branch -q

git checkout -b multi_way_side_branch_1 -q
echo "mw1" > file_mw1
git add file_mw1
git commit -m "Multi-way 1" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b multi_way_side_branch_2 -q
echo "mw2" > file_mw2
git add file_mw2
git commit -m "Multi-way 2" -q

git checkout multi_way_side_branch_1 -q
echo "mw_x1" > file_mw_x1
git add file_mw_x1
git commit -m "Multi-way X1" -q

git merge multi_way_side_branch_2 --no-ff -m "Merge MW2" -q
git branch -f multi_way_upstream_branch -q

git checkout "${BASE_COMMIT}" -q
git checkout -b multi_way_head_branch -q
echo "mw1" > file_mw1
git add file_mw1
git commit -m "Multi-way 1 eq" -q
echo "mw2" > file_mw2
git add file_mw2
git commit -m "Multi-way 2 eq" -q

# Commit Graph:
# * (multi_way_head_branch) Multi-way 2 eq
# * Multi-way 1 eq
# | * (multi_way_upstream_branch, multi_way_side_branch_1) Merge MW2
# | |\
# | | * (multi_way_side_branch_2) Multi-way 2
# | |/
# |/|
# | * Multi-way X1
# | * Multi-way 1
# |/
# * Initial base commit

# Without --multi-way-merge, should fail
set +e
OUT=$("${GITTOOL}" merge --by-commit --flatten --batch-skip multi_way_upstream_branch 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )); then
  fail "merge should have failed with Multiple independent commits found."
fi
if ! grep -q "Multiple independent commits found" <<<"${OUT}"; then
  fail "Expected error message 'Multiple independent commits found', got: ${OUT}"
fi

# With --multi-way-merge, should succeed
"${GITTOOL}" merge --by-commit --flatten --batch-skip \
  --multi-way-merge multi_way_upstream_branch > /dev/null 2>&1

# Verify history
if ! git log -1 --format=%s HEAD~2 | grep -q "Skip 2 commit(s) from multi_way_upstream_branch"; then
  fail "Expected skip of 2 commits at HEAD~2."
fi
if ! git log -1 --format=%s HEAD~1 | grep -q "MERGE: Multi-way X1"; then
  fail "Expected merge of Multi-way X1 at HEAD~1."
fi
pass

# --- Test merge (--by-commit) (Empty reported_commits) ---
echo -n "Testing merge (--by-commit) (Empty reported_commits)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b empty_rep_side_branch -q
echo "side" > file_side
git add file_side
git commit -m "side" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b empty_rep_upstream_branch -q
echo "main" > file_main
git add file_main
git commit -m "main" -q
git merge empty_rep_side_branch --no-ff -m "Merge side" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b empty_rep_head_branch -q
# Merge the parents of the upstream merge so HEAD has the commits, but not the merge itself
git merge empty_rep_side_branch^0 empty_rep_upstream_branch^1 --no-ff -m "head merge" \
  > /dev/null 2>&1

# Commit Graph:
# *   (empty_rep_head_branch) head merge
# |\  * (empty_rep_upstream_branch) Merge side
# | |/|
# |/|/
# * | main
# | * side
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit --flatten empty_rep_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%s HEAD | \
     grep -q "Skip merge commits from empty_rep_upstream_branch"; then
  fail "Expected 'Skip merge commits from empty_rep_upstream_branch', " \
       "got: $(git log -1 --format=%s HEAD)"
fi
pass

# --- Test merge (--by-commit) (--log) ---
echo -n "Testing merge (--by-commit) (--log)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b log_limit_upstream_branch -q

for i in {1..5}; do
  echo "commit $i" > file_log_"$i"
  git add file_log_"$i"
  git commit -m "DO NOT MERGE ANYWHERE: Log commit $i" -q
done

git checkout "${BASE_COMMIT}" -q
git checkout -b log_limit_head_branch -q

# Commit Graph:
# * (log_limit_head_branch) Initial base commit
# | * (log_limit_upstream_branch) DO NOT MERGE ANYWHERE: Log commit 5
# | * DO NOT MERGE ANYWHERE: Log commit 4
# | * DO NOT MERGE ANYWHERE: Log commit 3
# | * DO NOT MERGE ANYWHERE: Log commit 2
# | * DO NOT MERGE ANYWHERE: Log commit 1
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit --batch-skip --log=3 log_limit_upstream_branch > /dev/null 2>&1

if ! git log -1 --format=%b HEAD | grep -q "\.\.\. and 2 more commits \.\.\."; then
  fail "Commit message body should contain '... and 2 more commits ...' when using --log=3.
Body is: $(git log -1 --format=%b HEAD)"
fi
pass

# --- Test merge (--by-commit) (Manual Skip) ---
echo -n "Testing merge (--by-commit) (Manual Skip)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b manual_skip_head_branch -q

git checkout "${BASE_COMMIT}" -q
git checkout -b manual_skip_upstream_branch -q
echo "manual skip" > manual_skip_file
git add manual_skip_file
git commit -m "MANUAL-SKIP: reason for skipping" -q

git checkout manual_skip_head_branch -q
# Commit Graph:
# * (manual_skip_upstream_branch) MANUAL-SKIP: reason for skipping
# * (manual_skip_head_branch) Initial base commit
OUT=$("${GITTOOL}" merge --by-commit \
  --manual-skip="MANUAL-SKIP::reason for skipping" manual_skip_upstream_branch 2>&1 || true)

if ! grep -q "Skip 1 commit(s) from manual_skip_upstream_branch" \
     <<<"${OUT}"; then
  fail "Commit should be skipped. Output: ${OUT}"
fi
if ! git log -1 --format=%B | grep -q "Skip reason: Manual - reason for skipping"; then
  fail "Skip reason should be 'Manual - reason for skipping'. Output: $(git log -1 --format=%B)"
fi

# Manual skip should work even with --ignore-keywords
git reset --hard "${BASE_COMMIT}" -q
OUT=$("${GITTOOL}" merge --by-commit --ignore-keywords \
  --manual-skip="MANUAL-SKIP::reason for skipping" manual_skip_upstream_branch 2>&1 || true)
if ! grep -q "Skip 1 commit(s) from manual_skip_upstream_branch" \
     <<<"${OUT}"; then
  fail "Manual skip should still work with --ignore-keywords. Output: ${OUT}"
fi
pass

# --- Test merge (--by-commit) (Empty Commits) ---
echo -n "Testing merge (--by-commit) (Empty Commits)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b empty_commit_upstream_branch -q

# 1. Empty non-merge commit
git commit --allow-empty -m "Empty Commit 1" -q

# 2. Empty merge commit
git checkout -b empty_commit_side_branch -q
echo "side change" > empty_side_file
git add empty_side_file
git commit -m "Side branch commit" -q

git checkout empty_commit_upstream_branch -q
echo "main change" > empty_main_file
git add empty_main_file
git commit -m "Main branch commit" -q

git merge empty_commit_side_branch -s ours -m "Empty Merge Commit" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b empty_commit_head_branch -q

# Commit Graph:
# * (empty_commit_upstream_branch) Empty Merge Commit (ours)
# |\
# * | Main branch commit
# | * (empty_commit_side_branch) Side branch commit
# |/
# * Empty Commit 1
# * (empty_commit_head_branch) Initial base commit
"${GITTOOL}" merge --by-commit empty_commit_upstream_branch > /dev/null 2>&1

# Check that Empty Commit 1 and Empty Merge Commit are skipped.
skip_count=$(git log --format=%s | \
             grep -c "Skip 1 commit(s) from empty_commit_upstream_branch" || true)
if (( skip_count != 2 )); then
  fail "Expected 2 skip commits, got ${skip_count}."
fi
if ! git log --format=%b HEAD | grep -q "Empty Commit 1"; then
  fail "Empty Commit 1 was not skipped."
fi
if ! git log --format=%b HEAD | \
    grep -q "Skip reason: commit has no effect on the tree (empty or redundant)"; then
  fail "Empty Commit 1 skip reason was not 'commit has no effect on the tree (empty or redundant)'."
fi
if ! git log --format=%b HEAD | grep -q "Empty Merge Commit"; then
  fail "Empty Merge Commit was not skipped."
fi
if ! git log --format=%b HEAD | \
    grep -q "Skip reason: commit has no effect on the tree (empty or redundant)"; then
  fail "Empty Merge Commit skip reason was not \
'commit has no effect on the tree (empty or redundant)'."
fi
pass

# --- Test merge (--by-commit) (Change-Id equivalence with diamond merge) ---
echo -n "Testing merge (--by-commit) (Change-Id equivalence with diamond merge)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b cid_merge_upstream_branch -q
echo "a content" > cid_merge_file_side
git add cid_merge_file_side
git commit -F - <<EOF -q
Commit A

Change-Id: I1111222233334444555566667777888899990000
EOF

git checkout "${BASE_COMMIT}" -q
git checkout -b cid_merge_head_branch -q
echo "a2 content" > cid_merge_file_head
git add cid_merge_file_head
git commit -F - <<EOF -q
Commit A2

Change-Id: I1111222233334444555566667777888899990000
EOF

# Merge A and A2
git checkout cid_merge_upstream_branch -q
git merge cid_merge_head_branch --no-ff -m "Merge Commit M" -q

# Add Commit B to head
git checkout cid_merge_head_branch -q
echo "b content" > b_file
git add b_file
git commit -m "Commit B" -q

# Commit Graph:
# * (cid_merge_head_branch) Commit B
# | * (cid_merge_upstream_branch) Merge Commit M
# |/|
# * | Commit A2 (Change-Id: I1111...0000)
# | * Commit A (Change-Id: I1111...0000)
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit --flatten --pretty=%s cid_merge_upstream_branch > /dev/null 2>&1

# Verify results
# In flatten mode, A should be skipped (matches A2 in HEAD)
if ! git log -1 HEAD~1 --format=%s | grep -q "Skip 1 commit(s) from cid_merge_upstream_branch"; then
    fail "Commit A was not skipped correctly (Change-Id match with A2) - subject mismatch"
fi
if ! git log -1 HEAD~1 --format=%b | grep -q "Commit A"; then
    fail "Commit A was not skipped correctly (Change-Id match with A2) - body mismatch"
fi
if ! git log -1 HEAD~1 --format=%b | grep -q "Skip reason: Found equivalent commit"; then
    fail "Commit A skip reason not found in body."
fi
pass

# --- Test merge (--cherry-pick) ---
echo -n "Testing merge (--cherry-pick)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b cp_upstream_branch -q
echo "cp1" > file_cp1
git add file_cp1
git commit -m "Cherry-pick 1" -q
echo "cp2" > file_cp2
git add file_cp2
git commit -m "Cherry-pick 2" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b cp_head_branch -q

# Commit Graph:
# * (cp_head_branch) Initial base commit
# | * (cp_upstream_branch) Cherry-pick 2
# | * Cherry-pick 1
# |/
# * Initial base commit

# Run with --cherry-pick (implies --by-commit and --batch-skip)
"${GITTOOL}" merge --cherry-pick --pretty=%s \
  --bug-id=999 cp_upstream_branch > /dev/null 2>&1

# Verify history:
# *   (HEAD -> cp_head_branch) Skip 2 commits from cp_upstream_branch
# |\
# | * (cp_upstream_branch) Cherry-pick 2
# | * Cherry-pick 1
# * | Cherry-pick 2 (from cp_upstream_branch, with -x)
# * | Cherry-pick 1 (from cp_upstream_branch, with -x)
# |/
# * Initial base commit

if ! git log -1 --format=%s | grep -q "Skip 2 commit(s) from cp_upstream_branch"; then
  fail "Final Skip commit missing or incorrect."
fi

if ! git log -1 --format=%b | grep -q "Skip reason: cherry-picked"; then
  fail "Skip reason 'cherry-picked' missing from final merge message."
fi

if ! git log -1 --format=%b | grep -q "Bug: 999"; then
  fail "Bug ID missing from final Skip commit."
fi

if ! git log -1 --format=%B HEAD~1 | grep -q "(cherry picked from commit"; then
  fail "Intermediate commit 2 was not cherry-picked with -x."
fi

if git log -1 --format=%B HEAD~1 | grep -q "Bug: 999"; then
  fail "Bug ID should NOT be in the cherry-picked commit 2."
fi

if ! git log -1 --format=%B HEAD~2 | grep -q "(cherry picked from commit"; then
  fail "Intermediate commit 1 was not cherry-picked with -x."
fi

if git log -1 --format=%B HEAD~2 | grep -q "Bug: 999"; then
  fail "Bug ID should NOT be in the cherry-picked commit 1."
fi

# Verify files exist
if [[ ! -f file_cp1 || ! -f file_cp2 ]]; then
  fail "Files from cherry-picked commits are missing."
fi
pass

# --- Test merge (--cherry-pick) conflict ---
echo -n "Testing merge (--cherry-pick) conflict... "
git checkout "${BASE_COMMIT}" -q
git checkout -b cp_conflict_upstream_branch -q
echo "conflict upstream" > conflict_file
git add conflict_file
git commit -m "Cherry-pick conflict upstream" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b cp_conflict_head_branch -q
echo "conflict local" > conflict_file
git add conflict_file
git commit -m "Cherry-pick conflict local" -q

# Commit Graph:
# * (cp_conflict_head_branch) Cherry-pick conflict local
# | * (cp_conflict_upstream_branch) Cherry-pick conflict upstream
# |/
# * Initial base commit

# Run with --cherry-pick
set +e
OUT=$("${GITTOOL}" merge --cherry-pick cp_conflict_upstream_branch 2>&1)
EXIT_CODE=$?
set -e

if (( EXIT_CODE == 0 )); then
  fail "merge --cherry-pick should have failed with conflict."
fi

# Verify that cherry-pick was NOT aborted (CHERRY_PICK_HEAD should exist)
if ! git rev-parse --verify -q CHERRY_PICK_HEAD >/dev/null; then
  fail "Cherry-pick was aborted, but should have been left for the user."
fi

git cherry-pick --abort >/dev/null 2>&1 || true
pass

# --- Test merge (--cherry-pick) conflict with --continue-with-conflicts ---
echo -n "Testing merge (--cherry-pick) conflict with --continue-with-conflicts... "
git checkout "${BASE_COMMIT}" -q
git checkout -b cp_conflict_upstream_branch_cwc -q
echo "conflict upstream" > conflict_file_cp_cwc
git add conflict_file_cp_cwc
git commit -m "Cherry-pick conflict upstream" -q

git checkout "${BASE_COMMIT}" -q
git checkout -b cp_conflict_head_branch_cwc -q
echo "conflict local" > conflict_file_cp_cwc
git add conflict_file_cp_cwc
git commit -m "Cherry-pick conflict local" -q

set +e
"${GITTOOL}" merge --cherry-pick --continue-with-conflicts \
  cp_conflict_upstream_branch_cwc >/dev/null 2>&1
EXIT_CODE=$?
set -e
if (( EXIT_CODE != 2 )); then
  fail "Expected exit code 2, got ${EXIT_CODE}"
fi

# Verify cherry-picked commit message starts with [conflict]
CP_COMMIT_MSG=$(git log -1 --pretty=%s HEAD~1)
if [[ "${CP_COMMIT_MSG}" != "[conflict] "* ]]; then
  fail "Expected cherry-picked commit message to start with '[conflict] ', got '${CP_COMMIT_MSG}'"
fi

# Verify conflict markers in file of HEAD~1
if ! git show HEAD~1:conflict_file_cp_cwc | grep -q "<<<<<<< HEAD"; then
  fail "Expected conflict markers in conflict_file_cp_cwc at HEAD~1"
fi
pass


# --- Test merge (--cherry-pick) flattening ---
echo -n "Testing merge (--cherry-pick) flattening... "
git checkout "${BASE_COMMIT}" -q
git checkout -b cp_flatten_upstream_branch -q

# Create a side branch and merge it
git checkout -b cp_side_branch -q
echo "side" > file_side
git add file_side
git commit -m "Side commit" -q

git checkout cp_flatten_upstream_branch -q
echo "main" > file_main
git add file_main
git commit -m "Main commit" -q

git merge cp_side_branch --no-ff -m "Merge side into main" -q

# Commit Graph (cp_flatten_upstream_branch):
# *   Merge side into main
# |\
# | * Side commit
# * | Main commit
# |/
# * Initial base commit

git checkout "${BASE_COMMIT}" -q
git checkout -b cp_flatten_head_branch -q

# Commit Graph (Initial):
# * (cp_flatten_head_branch) Initial base commit

# Run with --cherry-pick (should imply --flatten)
"${GITTOOL}" merge --cherry-pick --pretty=%s cp_flatten_upstream_branch > /dev/null 2>&1

# Verify history:
# *   (HEAD -> cp_flatten_head_branch) Skip 2 commits from cp_flatten_upstream_branch
# |\
# | * Merge side into main
# | |\
# | | * Side commit
# | | * Main commit
# | |/
# * | Cherry-pick 2 (from Side commit, with -x)
# * | Cherry-pick 1 (from Main commit, with -x)
# |/
# * Initial base commit

# Note: The merge commit itself is skipped with "flatten mode" reason, but
# gittool explicitly excludes "flatten mode" skips from the final merge message.
# So we expect 2 commits (the cherry-picked ones).

if ! git log -1 --format=%s | grep -q "Skip 2 commit(s) from cp_flatten_upstream_branch"; then
  fail "Final Skip commit missing or incorrect."
fi

# Verify files exist from both parents of the merge
if [[ ! -f file_main || ! -f file_side ]]; then
  fail "Files from flattened commits are missing."
fi

# Check that Main and Side commits were cherry-picked (should have 2 intermediate
# commits before Skip)
if ! git log -n 5 --format=%B | grep -q "(cherry picked from commit"; then
  fail "Commits were not cherry-picked."
fi
pass

# --- Test merge (--restrict-automerge) ---
echo -n "Testing merge (--restrict-automerge)... "
git checkout "${BASE_COMMIT}" -q
git checkout -b restrict_upstream_branch -q

echo "kw1" > file_kw1
git add file_kw1
git commit -m "DO NOT MERGE ANYWHERE: always skip" -q

echo "kw2" > file_kw2
git add file_kw2
git commit -m "DO NOT MERGE: conditional skip" -q

echo "kw3" > file_kw3
git add file_kw3
git commit -m "RESTRICT AUTOMERGE: conditional skip 2" -q

echo "kw4" > file_kw4
git add file_kw4
git commit -m "Normal commit" -q

# 1. Without --restrict-automerge (Default)
git checkout "${BASE_COMMIT}" -q
git checkout -b restrict_head_default -q
# Commit Graph:
# * (restrict_upstream_branch) Normal commit
# * RESTRICT AUTOMERGE: conditional skip 2
# * DO NOT MERGE: conditional skip
# * DO NOT MERGE ANYWHERE: always skip
# |/
# * Initial base commit
"${GITTOOL}" merge --by-commit restrict_upstream_branch > /dev/null 2>&1

# Verify results:
# "DO NOT MERGE ANYWHERE" should be skipped.
# "DO NOT MERGE" and "RESTRICT AUTOMERGE" should be MERGED.
if ! git log --format=%s | grep -q "Skip 1 commit(s) from restrict_upstream_branch"; then
  fail "DO NOT MERGE ANYWHERE should have been skipped by default - subject mismatch"
fi
if ! git log --format=%b | grep -q "DO NOT MERGE ANYWHERE: always skip"; then
  fail "DO NOT MERGE ANYWHERE should have been skipped by default - body mismatch."
fi
if ! git log --format=%s | grep -q "MERGE: DO NOT MERGE: conditional skip"; then
  fail "DO NOT MERGE should have been merged by default."
fi
if ! git log --format=%s | grep -q "MERGE: RESTRICT AUTOMERGE: conditional skip 2"; then
  fail "RESTRICT AUTOMERGE should have been merged by default."
fi

# 2. With --restrict-automerge
git checkout "${BASE_COMMIT}" -q
git checkout -b restrict_head_enabled -q
"${GITTOOL}" merge --by-commit --restrict-automerge restrict_upstream_branch > /dev/null 2>&1

# Verify results:
# ALL three keywords should be skipped.
skip_count=$(git log --format=%s | grep -c "Skip 1 commit(s) from restrict_upstream_branch" || true)
if (( skip_count != 3 )); then
  fail "Expected 3 skip commits, got ${skip_count}."
fi
if ! git log --format=%b | grep -q "DO NOT MERGE ANYWHERE: always skip"; then
  fail "DO NOT MERGE ANYWHERE should have been skipped."
fi
if ! git log --format=%b | grep -q "DO NOT MERGE: conditional skip"; then
  fail "DO NOT MERGE should have been skipped with --restrict-automerge."
fi
if ! git log --format=%b | grep -q "RESTRICT AUTOMERGE: conditional skip 2"; then
  fail "RESTRICT AUTOMERGE should have been skipped with --restrict-automerge."
fi
pass

finish_tests
