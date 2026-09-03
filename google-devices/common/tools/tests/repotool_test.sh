#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

SCRIPT_DIR=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
readonly SCRIPT_DIR
readonly REPOTOOL="${SCRIPT_DIR}/../repotool.sh"

source "${SCRIPT_DIR}/test_base.sh"

# Create a temporary workspace
TEST_DIR=$(mktemp -d)
trap 'rm -rf "${TEST_DIR}"' EXIT

export PATH="${TEST_DIR}/bin:${PATH}"
mkdir -p "${TEST_DIR}/bin"

# Mock the `repo` command
cat << 'EOF' > "${TEST_DIR}/bin/repo"
#!/bin/bash
if [[ "$1" == "list" && "$2" == "-p" ]]; then
  shift 2
  if [[ "$1" == "-r" ]]; then
    shift
    filters=("$@")
    for p in "private/project1" "private/project2" "public/project3"; do
      for f in "${filters[@]}"; do
        if [[ "${p}" =~ ${f} ]]; then
          echo "${p}"
          break
        fi
      done
    done
  elif [[ -n "$1" ]]; then
    echo "$1"
  else
    for p in "private/project1" "private/project2" "public/project3"; do
      echo "${p}"
    done
  fi
elif [[ "$1" == "start" ]]; then
  shift
  if [[ "$1" == "--head" ]]; then
    shift
  fi
  topic="$1"
  shift
  proj="$1"
  if [[ "${topic}" == "fail_start_topic" ]]; then
    echo "Simulated repo start failure" >&2
    exit 1
  fi
  if [[ -n "${proj}" && "${proj}" != "." ]]; then
    cd "${proj}"
  fi
  git checkout -B "${topic}" -q
elif [[ "$1" == "abandon" ]]; then
  topic="$2"
  if [[ "${topic}" == "fail_topic" ]]; then
    echo "Simulated abandon failure" >&2
    exit 1
  fi
  git checkout main -q || true
  git branch -D "${topic}" -q || true
elif [[ "$1" == "download" ]]; then
  shift
  if [[ "$1" == "-b" ]]; then
    branch="$2"
    shift 2
  fi
  proj="$1"
  shift
  changes=("$@")
  if [[ "${proj}" != "." ]]; then
    cd "${proj}"
  fi
  git checkout "change_${changes[0]}" -q
  if [[ -n "${branch}" ]]; then
    git checkout -B "${branch}" -q
  fi
else
  echo "Unknown repo command: $@" >&2
  exit 1
fi
EOF
chmod +x "${TEST_DIR}/bin/repo"

# Mock the `gob-curl` command
cat << 'EOF' > "${TEST_DIR}/bin/gob-curl"
#!/bin/bash
if [[ "$*" == *"topic:test-topic"* ]]; then
  echo ")]}'"
  echo '[' \
       '{"project":"private/project1","_number":12345},' \
       '{"project":"public/project3","_number":67890}' \
       ']' | tr -d '\n'
  echo
elif [[ "$*" == *"topic:multi-change-topic"* ]]; then
  echo ")]}'"
  echo '[' \
       '{"project":"private/project1","_number":11111},' \
       '{"project":"private/project1","_number":22222}' \
       ']' | tr -d '\n'
  echo
elif [[ "$*" == *"topic:multi-host-topic"* ]]; then
  echo ")]}'"
  if [[ "$*" == *"host-a"* ]]; then
    echo '[{"project":"private/project1","_number":101}]'
  elif [[ "$*" == *"host-b"* ]]; then
    echo '[{"project":"private/project2","_number":202}]'
  else
    echo '[]'
  fi
elif [[ "$*" == *"topic:ancestry-topic"* ]]; then
  echo ")]}'"
  echo '[{"project":"private/project1","_number":2},{"project":"private/project1","_number":1}]'
elif [[ "$*" == *"topic:diverged-topic"* ]]; then
  echo ")]}'"
  echo '[{"project":"private/project1","_number":303},{"project":"private/project1","_number":404}]'
elif [[ "$*" == *"custom-query"* ]]; then
  echo ")]}'"
  echo '[{"project":"private/project1","_number":12345}]'
else
  echo ")]}'"
  echo '[]'
fi
EOF
chmod +x "${TEST_DIR}/bin/gob-curl"

echo "Initializing test repository..."
mkdir -p "${TEST_DIR}/.repo"
for p in "private/project1" "private/project2" "public/project3"; do
  mkdir -p "${TEST_DIR}/${p}"
  cd "${TEST_DIR}/${p}"
  git init -q -b main
  git config user.email "test@example.com"
  git config user.name "Test User"
  echo "base" > file
  git add file
  git commit -m "base" -q

  local_base_sha=$(git rev-list --max-parents=0 HEAD)

  git checkout "${local_base_sha}" -q
  echo "change 12345" >> file
  git add file; git commit -m "Change 12345" -q; git branch change_12345 -q

  git checkout "${local_base_sha}" -q
  echo "change 1" >> file
  git add file; git commit -m "Change 1" -q; git branch change_1 -q
  echo "change 2" >> file
  git add file; git commit -m "Change 2" -q; git branch change_2 -q

  git checkout "${local_base_sha}" -q
  echo "change 303" >> file
  git add file; git commit -m "Change 303" -q; git branch change_303 -q
  git checkout "${local_base_sha}" -q
  echo "change 404" >> other_file
  git add other_file; git commit -m "Change 404" -q; git branch change_404 -q

  git checkout "${local_base_sha}" -q
  echo "change 67890" >> file
  git add file; git commit -m "Change 67890" -q; git branch change_67890 -q

  git checkout "${local_base_sha}" -q
  echo "change 11111" >> file
  git add file; git commit -m "Change 11111" -q; git branch change_11111 -q
  echo "change 22222" >> file
  git add file; git commit -m "Change 22222" -q; git branch change_22222 -q

  git checkout "${local_base_sha}" -q
  echo "change 101" >> file
  git add file; git commit -m "Change 101" -q; git branch change_101 -q
  git checkout "${local_base_sha}" -q
  echo "change 202" >> file
  git add file; git commit -m "Change 202" -q; git branch change_202 -q

  git checkout main -q
done

# Shared branches for merge/rebase tests
cd "${TEST_DIR}/private/project1"
git checkout -b upstream_branch -q
echo "p1" > file; git add file; git commit -m "Commit 1" -q; git checkout main -q
cd "${TEST_DIR}/public/project3"
git checkout -b upstream_branch -q
echo "p3" > file; git add file; git commit -m "Commit 3" -q; git checkout main -q

function reset_projects() {
  for p in "private/project1" "private/project2" "public/project3"; do
    cd "${TEST_DIR}/${p}"
    git merge --abort >/dev/null 2>&1 || true
    git rebase --abort >/dev/null 2>&1 || true
    git reset --hard HEAD -q
    git checkout main -q
    git reset --hard "$(git rev-list --max-parents=0 HEAD)" -q
    for b in $(git branch --format="%(refname:short)"); do
      case "${b}" in
        main|change_*|upstream_branch|conflict_upstream) ;;
        *) git branch -D "${b}" -q >/dev/null 2>&1 || true ;;
      esac
    done
  done
  cd "${TEST_DIR}"
}

reset_projects

echo -n "Testing list (Default)... "
OUT=$("${REPOTOOL}" list 2>&1 || true)
if ! grep -q "private/project1" <<<"${OUT}" || \
   ! grep -q "private/project2" <<<"${OUT}" || \
   ! grep -q "public/project3" <<<"${OUT}"; then
  fail "Default list should show all projects. Output: ${OUT}"
fi
pass

echo -n "Testing list (Multiple regexes)... "
OUT=$("${REPOTOOL}" -r "project1" --regex="project3" list 2>&1 || true)
if ! grep -q "private/project1" <<<"${OUT}" || \
   grep -q "private/project2" <<<"${OUT}" || \
   ! grep -q "public/project3" <<<"${OUT}"; then
  fail "Multiple regexes failed. Output: ${OUT}"
fi
pass

echo -n "Testing list (--exclude filter)... "
OUT=$("${REPOTOOL}" --exclude="project2" list 2>&1 || true)
if ! grep -q "private/project1" <<<"${OUT}" || \
   grep -q "project2" <<<"${OUT}"; then
  fail "--exclude list failed. Output: ${OUT}"
fi
pass

echo -n "Testing list (--filter filter)... "
OUT=$("${REPOTOOL}" --filter="project1" list 2>&1 || true)
if ! grep -q "private/project1" <<<"${OUT}" || \
   grep -q "private/project2" <<<"${OUT}"; then
  fail "--filter list failed. Output: ${OUT}"
fi
pass

echo -n "Testing list (--include filter)... "
OUT=$("${REPOTOOL}" --include="public" --exclude="private" list 2>&1 || true)
if grep -q "private" <<<"${OUT}" || \
   ! grep -q "public/project3" <<<"${OUT}"; then
  fail "--include list failed. Output: ${OUT}"
fi
pass

echo -n "Testing list (No projects found)... "
set +e
OUT=$("${REPOTOOL}" --regex="non-existent" list 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || ! grep -q "No projects found" <<<"${OUT}"; then
  fail "repotool should fail when no projects are found. Output: ${OUT}"
fi
pass

echo -n "Testing merge (SUCCESS)... "
reset_projects
OUT=$("${REPOTOOL}" merge --topic=success_topic upstream_branch 2>&1 || true)
if ! grep -q "MERGED: private/project1" <<<"${OUT}" || \
   ! grep -q "MERGED: public/project3" <<<"${OUT}"; then
  fail "Both projects should be merged. Output: ${OUT}"
fi
pass

echo -n "Testing merge (Forward unrecognized options)... "
reset_projects
set +e
OUT=$("${REPOTOOL}" merge --topic=forward_topic --unknown-option upstream_branch 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || ! grep -q "Unknown option: --unknown-option" <<<"${OUT}"; then
  fail "Should forward unrecognized option to gittool. Output: ${OUT}"
fi
pass

echo -n "Testing merge (--regex filter)... "
reset_projects
OUT=$("${REPOTOOL}" merge --topic=regex_topic --regex="public/" \
  upstream_branch 2>&1 || true)
if ! grep -q "MERGED: public/project3" <<<"${OUT}" || \
   grep -q "private/project1" <<<"${OUT}"; then
  fail "Only project3 should be merged. Output: ${OUT}"
fi
pass

echo -n "Testing merge (-r filter)... "
reset_projects
cd "${TEST_DIR}/public/project3"
git merge upstream_branch -q
git reset --hard HEAD~1 -q
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" merge -r "public/" --topic=topic2 upstream_branch 2>&1 || \
  true)
if ! grep -q "MERGED: public/project3" <<<"${OUT}"; then
  fail "project3 should be merged. Output: ${OUT}"
fi
pass

echo -n "Testing merge (DIRTY)... "
reset_projects
cd "${TEST_DIR}/private/project1"
echo "dirty" > file
cd "${TEST_DIR}"
set +e
OUT=$("${REPOTOOL}" merge --topic=dirty_topic upstream_branch 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || \
   ! grep -q "FAILED (Uncommitted changes): private/project1" <<<"${OUT}"; then
  fail "Should fail on dirty project. Output: ${OUT}"
fi
pass

echo -n "Testing merge (FAILURE repo start)... "
reset_projects
cd "${TEST_DIR}/public/project3"
git merge upstream_branch -q
git reset --hard HEAD~1 -q
cd "${TEST_DIR}"
set +e
OUT=$("${REPOTOOL}" merge -r "public/" --topic=fail_start_topic \
  upstream_branch 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || ! grep -q "FAILED (Unknown error): public/project3" <<<"${OUT}"; then
  fail "Should fail to start branch. Output: ${OUT}"
fi
pass

echo -n "Testing merge (CONFLICT)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -B conflict_upstream -q
echo "up" > file
git add file
git commit -m "up" -q
git checkout main -q
echo "local" > file
git add file
git commit -m "local" -q
cd "${TEST_DIR}"
set +e
OUT=$("${REPOTOOL}" merge --topic=conflict_topic conflict_upstream 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || ! grep -q "FAILED (Conflict): private/project1" <<<"${OUT}"; then
  fail "Should report conflict. Output: ${OUT}"
fi
pass

echo -n "Testing merge (CONFLICT --continue-with-conflicts)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -B conflict_upstream -q
echo "up" > file
git add file
git commit -m "up" -q
git checkout main -q
echo "local" > file
git add file
git commit -m "local" -q
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" merge --topic=conflict_cwc_topic \
      --continue-with-conflicts conflict_upstream 2>&1 || true)
if ! grep -q "MERGED (Conflict): private/project1" <<<"${OUT}"; then
  fail "Should report MERGED (Conflict) if --continue-with-conflicts is used. Output: ${OUT}"
fi
cd "${TEST_DIR}/private/project1"
COMMIT_MSG=$(git log -1 --pretty=%s)
if [[ "${COMMIT_MSG}" != "[conflict] "* ]]; then
  fail "Expected commit message to start with '[conflict] ', got '${COMMIT_MSG}'"
fi
cd "${TEST_DIR}"
pass

echo -n "Testing merge (ABORT_SUCCESS)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -b my-topic -q
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" merge --abort --topic=my-topic 2>&1 || true)
if ! grep -q "ABORTED: private/project1" <<<"${OUT}"; then
  fail "Should be aborted. Output: ${OUT}"
fi
pass

echo -n "Testing merge (ABORT_CHERRY_PICK)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -b cp-topic -q
# Produce a cherry-pick conflict
echo "base" > conflict_file
git add conflict_file
git commit -m "base" -q
git checkout -b cp-branch HEAD^1 -q
echo "conflict" > conflict_file
git add conflict_file
git commit -m "conflict" -q
git checkout cp-topic -q
set +e
git cherry-pick cp-branch >/dev/null 2>&1
set -e
if ! git rev-parse --verify -q CHERRY_PICK_HEAD >/dev/null; then
  fail "Cherry-pick should be in progress."
fi
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" merge --abort --topic=cp-topic 2>&1 || true)
if ! grep -q "ABORTED: private/project1" <<<"${OUT}"; then
  fail "Should be aborted. Output: ${OUT}"
fi
cd "${TEST_DIR}/private/project1"
if git rev-parse --verify -q CHERRY_PICK_HEAD >/dev/null; then
  fail "Cherry-pick should have been aborted."
fi
cd "${TEST_DIR}"
pass

echo -n "Testing merge (--by-commit)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -B by_commit_upstream -q
echo "bc" > file
git add file
git commit -m "bc" -q
git checkout main -q
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" merge --topic=bc_topic --by-commit --bug-id=123 \
  by_commit_upstream 2>&1 || true)
if ! grep -q "MERGED: private/project1" <<<"${OUT}"; then
  fail "Should be merged. Output: ${OUT}"
fi
cd "${TEST_DIR}/private/project1"
if ! git log -1 --format=%B | grep -q "Bug: 123"; then
  fail "Missing bug ID in commit. Output: $(git log -1 --format=%B)"
fi
cd "${TEST_DIR}"
pass

echo -n "Testing merge (--drop-empty-merges)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -B empty_merge_upstream -q
echo "empty merge content" > empty_merge_file
git add empty_merge_file
git commit -m "add empty merge file" -q
git checkout main -q
echo "empty merge content" > empty_merge_file
git add empty_merge_file
git commit -m "add empty merge file on main" -q
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" merge --topic=empty_topic --drop-empty-merges empty_merge_upstream 2>&1 || true)
if ! grep -q "SKIPPED (Empty merge): private/project1" <<<"${OUT}"; then
  fail "Should be skipped as empty merge. Output: ${OUT}"
fi
cd "${TEST_DIR}/private/project1"
current_branch=$(git branch --show-current)
if [[ "${current_branch}" != "main" ]]; then
  fail "Should be back on main branch, but on ${current_branch}"
fi
cd "${TEST_DIR}"
pass

echo -n "Testing merge (--drop-empty-merges continuing)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -B empty_merge_upstream2 -q
echo "content2" > file2
git add file2
git commit -m "content2" -q
git checkout main -q
echo "content2" > file2
git add file2
git commit -m "content2 on main" -q

# Simulate previous merge started
git checkout -b continue_topic -q
git branch continue_topic-pre-merge-head HEAD
git checkout main -q

cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" merge --topic=continue_topic --drop-empty-merges \
  empty_merge_upstream2 2>&1 || true)
if ! grep -q "SKIPPED (Empty merge): private/project1" <<<"${OUT}"; then
  fail "Should be skipped as empty merge on continue. Output: ${OUT}"
fi
cd "${TEST_DIR}/private/project1"
current_branch=$(git branch --show-current)
if [[ "${current_branch}" != "main" ]]; then
  fail "Should be back on main branch, but on ${current_branch}"
fi
cd "${TEST_DIR}"
pass

echo -n "Testing rebase (SUCCESS)... "
reset_projects
OUT=$("${REPOTOOL}" rebase upstream_branch 2>&1 || true)
if ! grep -q "REBASED: private/project1" <<<"${OUT}" || \
   ! grep -q "REBASED: public/project3" <<<"${OUT}"; then
  fail "Both projects should be rebased. Output: ${OUT}"
fi
pass

echo -n "Testing rebase (NOTHING)... "
reset_projects
"${REPOTOOL}" rebase upstream_branch >/dev/null 2>&1 || true
OUT=$("${REPOTOOL}" rebase upstream_branch 2>&1 || true)
if ! grep -q "SKIPPED (Up to date): private/project1" <<<"${OUT}"; then
  fail "project1 should be skipped (up to date). Output: ${OUT}"
fi
pass

echo -n "Testing rebase (DIRTY)... "
reset_projects
cd "${TEST_DIR}/private/project1"
echo "dirty" > file
cd "${TEST_DIR}"
set +e
OUT=$("${REPOTOOL}" rebase upstream_branch 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || \
   ! grep -q "FAILED (Uncommitted changes): private/project1" <<<"${OUT}"; then
  fail "Should fail on dirty project. Output: ${OUT}"
fi
pass

echo -n "Testing rebase (CONFLICT)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout -B conflict_upstream -q
echo "up" > file
git add file
git commit -m "up" -q
git checkout main -q
echo "local" > file
git add file
git commit -m "local" -q
cd "${TEST_DIR}"
set +e
OUT=$("${REPOTOOL}" rebase conflict_upstream 2>&1)
EXIT_CODE=$?
set -e
if (( EXIT_CODE == 0 )) || ! grep -q "FAILED (Conflict): private/project1" <<<"${OUT}"; then
  fail "Should report conflict. Output: ${OUT}"
fi
pass

echo -n "Testing rebase (--abort)... "
reset_projects
cd "${TEST_DIR}/private/project1"
project_base_sha=$(git rev-list --max-parents=0 HEAD)
git checkout main -q
git reset --hard "${project_base_sha}" -q
echo "diverge" > file
git add file
git commit -m "diverge" -q
git rebase conflict_upstream >/dev/null 2>&1 || true
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" rebase --abort 2>&1 || true)
if ! grep -q "REBASED: private/project1" <<<"${OUT}"; then
  fail "Should be aborted. Output: ${OUT}"
fi
pass

echo -n "Testing rebase (NOBRANCH)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git checkout --detach HEAD -q
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" rebase 2>&1 || true)
if ! grep -q "SKIPPED (Not on a branch): private/project1" <<<"${OUT}"; then
  fail "Should be skipped (no branch). Output: ${OUT}"
fi
pass

for p in "private/project1" "private/project2" "public/project3"; do
  cd "${TEST_DIR}/${p}"
  git config remote.custom.review "https://gerrit-host/"
done

echo -n "Testing download (SUCCESS)... "
reset_projects
OUT=$("${REPOTOOL}" download --topic=test-topic 2>&1 || true)
if ! grep -q "DOWNLOADED: private/project1" <<<"${OUT}"; then
  fail "project1 should be downloaded. Output: ${OUT}"
fi
pass

echo -n "Testing download (--regex filter)... "
reset_projects
OUT=$("${REPOTOOL}" --regex="public/" download --topic=test-topic 2>&1 || true)
if ! grep -q "DOWNLOADED: public/project3" <<<"${OUT}" || \
   grep -q "DOWNLOADED: private/project1" <<<"${OUT}"; then
  fail "Regex filter failed. Output: ${OUT}"
fi
pass

echo -n "Testing download (multi-change topic)... "
reset_projects
OUT=$("${REPOTOOL}" download --topic=multi-change-topic 2>&1 || true)
if ! grep -q "DOWNLOADED: private/project1" <<<"${OUT}"; then
  fail "Multi-change download failed. Output: ${OUT}"
fi
pass

echo -n "Testing download (multi-host topic)... "
reset_projects
cd "${TEST_DIR}/private/project1"
git config remote.a.review "https://host-a/"
cd "${TEST_DIR}"
cd "${TEST_DIR}/private/project2"
git config remote.b.review "https://host-b/"
cd "${TEST_DIR}"
OUT=$("${REPOTOOL}" download --topic=multi-host-topic 2>&1 || true)
if ! grep -q "DOWNLOADED: private/project1" <<<"${OUT}" || \
   ! grep -q "DOWNLOADED: private/project2" <<<"${OUT}"; then
  fail "Multi-host download failed. Output: ${OUT}"
fi
pass

echo -n "Testing download (reordered ancestry topic)... "
reset_projects
OUT=$("${REPOTOOL}" download --topic=ancestry-topic 2>&1 || true)
if ! grep -q "DOWNLOADED: private/project1" <<<"${OUT}"; then
  fail "Ancestry topic download failed. Output: ${OUT}"
fi
cd "${TEST_DIR}/private/project1"
if [[ "$(git log -1 --format=%s)" != "Change 2" ]]; then
  fail "Project should be at Change 2. Output: $(git log -1 --format=%s)"
fi
cd "${TEST_DIR}"
pass

echo -n "Testing download (diverged changes)... "
reset_projects
OUT=$("${REPOTOOL}" download --topic=diverged-topic 2>&1 || true)
if ! grep -q "have diverged" <<<"${OUT}" || ! grep -q "Tips:" <<<"${OUT}"; then
  fail "Should report divergence. Output: ${OUT}"
fi
pass

echo -n "Testing download (with query)... "
reset_projects
OUT=$("${REPOTOOL}" download --topic=query-topic --query="custom-query" 2>&1 || true)
if ! grep -q "DOWNLOADED: private/project1" <<<"${OUT}"; then
  fail "download with query failed. Output: ${OUT}"
fi
pass

finish_tests
