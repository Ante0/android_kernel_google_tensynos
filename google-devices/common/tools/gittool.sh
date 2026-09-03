#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

source "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/logging.sh"

USAGE="$(cat <<EOF
usage: gittool [<options>] <command> [<args>]

OPTIONS:
  -C <path>             Run as if git was started in <path>.
  --no-fetch            Do not fetch remote branches.

FORMAT OPTIONS:
  --pretty=<format>, --format=<format>
                        Pretty-print commit logs in a given format.
                        Default: "tformat:%h (\"%s\")"
  --abbrev=<n>          Show a prefix of <n> digits for object names.
                        Default: 12

COMMANDS:
  fetch_if_needed <ref>
    Fetch <ref> if it is a remote reference (contains "/").

  find_commit [<format-options>] [<command-options>] <commit> [<head> [<limit>...]]
    Find if <commit> or an equivalent commit is in the range.
    Defaults <head> to HEAD. If <limit> is given, search in the range
    <limit>..<head>. <limit> is only for grep and patch-id checks, not
    for ancestry check.

    --skip-patch-id     Skip the slow patch-id equivalence check.
    --show-method       Print the equivalence method before the commit.
    --automerger        Simulate automerger behavior. Use only Merged-In to
                        search for Change-Id on upstream. Do not check
                        commit references.

  find_missing_commits [<format-options>] <upstream> [<head> [<limit>]]
    List commits in <head> missing from <upstream>.
    Defaults <head> to HEAD. If <limit> is given, search in the range
    <limit>..<head>.

    --skip-patch-id     Skip the patch-id equivalence check.
    --automerger        Simulate automerger behavior. Implies --skip-patch-id.
                        Passes --automerger to find_commit.

  merge [<format-options>] [<command-options>] <upstream>
    Merge <upstream>.

    --bug-id=<id>       Append "Bug: <id>" to all generated commit messages.
    --continue-with-conflicts
                        Commit conflicts with "[conflict]" subject prefix and continue.
    --dry-run           Show what would be merged without actually merging.
    --by-commit         Merge by commit. Equivalent commits already in HEAD are
                        merged with "-s ours" and marked as skipped.
                        Commits are also skipped if they have no effect on the
                        tree (e.g. the commit was merged to upstream with
                        -s ours), or if their subject contains "DO NOT MERGE ANYWHERE".
    --batch-skip        (With --by-commit only) Batch adjacent skipped commits
                        into a single commit.
    --cherry-pick       Use cherry-pick -x instead of merge for each commit,
                        then do a batch skip merge at the end. Implies
                        --by-commit, --batch-skip and --flatten.
    --automerger        Simulate automerger behavior. Implies --by-commit,
                        --first-parent, and --skip-patch-id. Do not check
                        commit references. Use only Merged-In to search for
                        Change-Id on upstream.
    --first-parent      Merge --first-parent commits only. This is the default
                        behavior. Implies --by-commit.
    --flatten           Flatten upstream history (merge origin commits and skip
                        merge commits). A commit can be skipped if it was
                        merged to upstream with -s ours. Implies --by-commit.
    --log=<n>           Maximum number of commits to list in the merge message.
                        Default: 100.
    --skip-patch-id     (With --by-commit only) Skip the patch-id equivalence
                        check.
    --ignore-keywords   (With --by-commit only) Do not skip commits based on
                        their subject keywords.
    --restrict-automerge
                        (With --by-commit only) Also skip commits whose subject
                        contains "DO NOT MERGE" or "RESTRICT AUTOMERGE".
    --manual-skip=<keyword>[::<reason>]
                        (With --by-commit only) Skip commits whose subject
                        contains <keyword> with the given <reason>.
    --multi-way-merge   (With --by-commit only) Allow multi-way merge (octopus merge)
                        when multiple independent commits are found.

EQUIVALENT COMMIT:
  A and B are equivalent if any of the following conditions are met:
    - They have the same patch-id.
    - They have the same Change-Id.
    - A's Change-Id is in one of B's Merged-In tags.
    - One references the other in its commit message using one of these patterns:
      - "[ Upstream commit <commit> ]"
      - "commit <commit> upstream."
      - "(cherry-picked from commit <commit>...)"
EOF
)"

export GIT_PAGER=""

# Global defaults for output formatting
GIT_PRETTY="tformat:%h (\"%s\")"
GIT_ABBREV=12

# Sets the global format options.
#
# Globals:
#   GIT_PRETTY
#   GIT_ABBREV
# Arguments:
#   1: The option string.
# Returns:
#   0 if the option was parsed, 1 otherwise.
function set_format_option() {
  case "$1" in
    --pretty=*|--format=*)
      GIT_PRETTY="${1#*=}"
      return 0
      ;;
    --abbrev=*)
      GIT_ABBREV="${1#*=}"
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

# Formats a commit using the global format options.
#
# Globals:
#   GIT_PRETTY
#   GIT_ABBREV
# Arguments:
#   ...: Arguments to pass to git show.
function format_commit() {
  git show -s --pretty="${GIT_PRETTY}" --abbrev="${GIT_ABBREV}" "$@"
}

# Fetches <ref> if it is a remote reference (contains "/").
#
# Globals:
#   GIT_NO_FETCH
# Arguments:
#   1: The reference to fetch.
function fetch_if_needed() {
  local ref="$1"
  if [[ -z "${ref}" ]]; then
    logging::error_usage_out "<ref> is required"
  fi

  if [[ -n "${GIT_NO_FETCH}" ]]; then
    return 0
  fi

  if [[ "${ref}" == */* ]]; then
    local remote="${ref%%/*}"
    local branch="${ref#*/}"
    if git remote | grep -q "^${remote}$"; then
      logging::info "Fetching ${ref}..."
      if git fetch -q "${remote}" "${branch}"; then
        logging::info "  -> $(format_commit "${ref}")"
      else
        logging::error_out "Failed to fetch ${ref}"
      fi
    fi
  fi
}

# Adds a prefix to each line of the given text.
#
# Arguments:
#   1: The prefix to add.
#   2: The text to prefix.
function _add_prefix() {
  local prefix="$1"
  local text="$2"
  if [[ -n "${text}" ]]; then
    local indented="${text//$'\n'/$'\n'${prefix}}"
    echo "${prefix}${indented}"
  fi
}

# Checks if the given Git object is valid.
#
# Arguments:
#   1: The object name/hash.
# Returns:
#   0 if valid, non-zero otherwise.
function _is_valid_object() {
  git rev-parse -q --verify "${1}^{commit}" >/dev/null
}

# Asserts that all given Git objects are valid, exiting if any are not.
#
# Arguments:
#   ...: The object names/hashes to check.
function _assert_valid_objects() {
  while (( $# > 0 )); do
    if ! _is_valid_object "$1"; then
      logging::error_out "$1 is not a valid object"
    fi
    shift
  done
}

# Finds if <commit> or an equivalent commit is in the given range.
# Equivalence is determined by ancestry, upstream references, Change-Id, or patch-id.
#
# Arguments:
#   ...: Options (--skip-patch-id, --show-method), <commit>, <head> (defaults to HEAD),
#        and optional <limit>... to restrict grep and patch-id checks.
# Returns:
#   0 if an equivalent commit is found, 1 otherwise.
function find_commit() {
  local skip_patch_id
  local show_method
  local -a positional_args=()

  while (( $# > 0 )); do
    case "$1" in
      --skip-patch-id)
        skip_patch_id=1
        ;;
      --show-method)
        show_method=1
        ;;
      --automerger)
        automerger=1
        ;;
      -*)
        logging::error_usage_out "Unknown option: $1"
        ;;
      *)
        positional_args+=("$1")
        ;;
    esac
    shift
  done

  local commit="${positional_args[0]:-}"
  local head="${positional_args[1]:-HEAD}"
  local limits=("${positional_args[@]:2}")

  logging::debug "find_commit("
  logging::debug "  commit=${commit},"
  logging::debug "  head=${head},"
  logging::debug "  limits=[${#limits[@]}](${limits[*]})"
  logging::debug "  skip_patch_id=${skip_patch_id},"
  logging::debug ")"

  if [[ -z "${commit}" ]]; then
    logging::error_usage_out "<commit> is required"
  fi

  fetch_if_needed "${head}"
  _assert_valid_objects "${head}"

  local commit_is_valid=
  if _is_valid_object "${commit}"; then
    commit_is_valid=1
  elif ! [[ "${commit}" =~ ^[0-9a-fA-F]{7,40}$ ]]; then
    logging::error_out "Invalid commit format: ${commit}"
  fi

  function _show_match() {
    local match_commit="$1"
    local method="$2"
    local formatted
    formatted="$(format_commit "${match_commit}")"
    logging::verbose "MATCH: ${formatted} (${method})"
    if [[ -n "${show_method}" ]]; then
      echo "Found equivalent commit by ${method}"
    fi
    echo "${formatted}"
  }

  logging::verbose "Finding commit ${commit}..."

  # 1. Finding by ancestry (fast)
  if [[ -n "${commit_is_valid}" ]]; then
    logging::verbose "Finding by ancestry..."
    if git merge-base --is-ancestor "${commit}" "${head}" 2>/dev/null; then
      _show_match "${commit}" "ancestry"
      return 0
    fi
  fi

  # Extract change IDs and upstream references
  local commit_body
  if [[ -n "${commit_is_valid}" ]]; then
    commit_body="$(git show -s --pretty=%B "${commit}" 2>/dev/null)"
  fi

  local -a upstream_commits=()
  local -a change_ids=()
  if [[ -n "${commit_body}" ]]; then
    if [[ -z "${automerger}" ]]; then
      readarray -t upstream_commits < <(
        sed -n -e 's/^\[ Upstream commit \([0-9a-f]\{7,40\}\) \]$/\1/p' \
               -e 's/^commit \([0-9a-f]\{7,40\}\) upstream\.$/\1/p' \
               -e 's/^(cherry[ -]picked from commit \([0-9a-f]\{7,40\}\).*/\1/p' <<<"${commit_body}"
      )
      readarray -t change_ids < <(
        sed -n -e 's/^Change-Id: \(I[0-9a-f]\{40\}\)$/\1/p' \
               -e 's/^Merged-In: \(I[0-9a-f]\{40\}\)$/\1/p' <<<"${commit_body}"
      )
    else
      # Automerger mode: Use only Merged-In to search for Change-Id on upstream.
      readarray -t change_ids < <(
        sed -n -e 's/^Merged-In: \(I[0-9a-f]\{40\}\)$/\1/p' <<<"${commit_body}"
      )
    fi
  fi

  if (( ${#upstream_commits[@]} > 0 )); then
    readarray -t upstream_commits < <(printf "%s\n" "${upstream_commits[@]}" | sort -u)
  fi
  if (( ${#change_ids[@]} > 0 )); then
    readarray -t change_ids < <(printf "%s\n" "${change_ids[@]}" | sort -u)
  fi

  # 2. Upstream commit reference check (fast)
  if (( ${#upstream_commits[@]} > 0 )); then
    logging::verbose "Finding by upstream commit reference..."
    local id
    for id in "${upstream_commits[@]}"; do
      if git merge-base --is-ancestor "${id}" "${head}" 2>/dev/null; then
        _show_match "${id}" "upstream reference"
        return 0
      fi
    done
  fi

  # 3. Grep checks (slow)
  local -a log_args=("${head}")
  local -i grep_count=0
  if [[ -z "${automerger}" ]]; then
    for id in "${commit}" "${upstream_commits[@]}"; do
      log_args+=(--grep "^\[ Upstream commit ${id} \]$")
      log_args+=(--grep "^commit ${id} upstream\.$")
      log_args+=(--grep "^(cherry[ -]picked from commit ${id}")
      grep_count+=3
    done
  fi
  for id in "${change_ids[@]}"; do
    log_args+=(--grep "^Change-Id: ${id}$")
    grep_count+=1
  done

  if (( grep_count > 0 )); then
    logging::verbose "Finding by commit message reference or Change-Id..."
    if (( ${#limits[@]} > 0 )); then
      log_args+=("${limits[@]/#/^}")
    fi
    # Exclude common ancestors of head and commit, assuming that the same change will not be
    # submitted to the same branch twice.
    if [[ -n "${commit_is_valid}" ]]; then
      log_args+=("^${commit}")
    fi
    local match
    match="$(git log -1 --pretty=%H "${log_args[@]}")"
    if [[ -n "${match}" ]]; then
      _show_match "${match}" "reference commit or Change-Id"
      return 0
    fi
  fi

  # 4. Patch-ID (slow)
  if [[ -z "${skip_patch_id}" ]] && [[ -n "${commit_is_valid}" ]]; then
    logging::verbose "Finding by patch-id..."
    local -a parents=()
    readarray -t parents < <(git rev-parse "${commit}^@" 2>/dev/null)
    local rev_list_args=("${commit}...${head}")
    if (( ${#parents[@]} > 0 )); then
      rev_list_args+=("${parents[@]/#/^}")
    fi
    if (( ${#limits[@]} > 0 )); then
      rev_list_args+=("${limits[@]/#/^}")
    fi
    local match
    match="$(git rev-list --cherry "${rev_list_args[@]}" 2>/dev/null | sed -n 's/^=//p' | head -1)"
    if [[ -n "${match}" ]]; then
      _show_match "${match}" "patch-id"
      return 0
    fi
  fi

  logging::verbose "Not found"
  return 1
}

# Lists commits in <head> that are missing from <upstream>.
#
# Arguments:
#   ...: Options (--skip-patch-id), <upstream>, <head> (defaults to HEAD),
#        and optional <limit>.
function find_missing_commits() {
  local skip_patch_id
  local -a positional_args=()

  while (( $# > 0 )); do
    case "$1" in
      --skip-patch-id)
        skip_patch_id=1
        ;;
      --automerger)
        skip_patch_id=1
        automerger=1
        ;;
      -*)
        logging::error_usage_out "Unknown option: $1"
        ;;
      *)
        positional_args+=("$1")
        ;;
    esac
    shift
  done

  local upstream="${positional_args[0]:-}"
  local head="${positional_args[1]:-HEAD}"
  local limit="${positional_args[2]:-}"

  if [[ -z "${upstream}" ]]; then
    logging::error_usage_out "Missing upstream branch"
  fi

  fetch_if_needed "${head}"
  fetch_if_needed "${upstream}"
  _assert_valid_objects "${head}" "${upstream}"

  local rev_list_args=(
    "--reverse" "--left-right" "--right-only" "--no-merges" "${upstream}...${head}"
  )
  if [[ -z "${skip_patch_id}" ]]; then
    rev_list_args+=("--cherry-mark")
  fi

  if [[ -n "${limit}" ]]; then
    rev_list_args+=("^${limit}")
  fi

  local -a commit_statuses=()
  readarray -t commit_statuses < <(git rev-list "${rev_list_args[@]}")

  local -i missing=0
  local -i count=0
  local -i total="${#commit_statuses[@]}"

  # Use upstream_rev for find_commit to avoid fetching from remote
  local upstream_rev
  upstream_rev="$(git rev-parse "${upstream}")"

  local line
  for line in "${commit_statuses[@]}"; do
    local status="${line:0:1}"
    local commit="${line:1}"
    local formatted_commit
    formatted_commit="$(format_commit "${commit}")"

    logging::verbose "[$(( ++count ))/${total}] ${formatted_commit}"

    logging::push_prefix "  "
    if [[ "${status}" == "=" ]]; then
      logging::verbose "MATCH: (batched patch-id result)"
    else
      local -a find_commit_args=(--skip-patch-id)
      if [[ -n "${automerger}" ]]; then
        find_commit_args+=(--automerger)
      fi
      if ! find_commit "${find_commit_args[@]}" "${commit}" "${upstream_rev}" >/dev/null; then
        logging::verbose "MISSING"
        missing="$(( missing + 1 ))"
        echo "${formatted_commit}"
      fi
    fi
    logging::pop_prefix
  done

  logging::info "Found ${missing} missing commits"
}

# Merges <upstream> into HEAD.
# Supports normal merging, or commit-by-commit merging (--by-commit) which
# skips commits that are equivalent in HEAD or contain specific keywords.
#
# Arguments:
#   ...: Options (--dry-run, --first-parent, --batch-skip, --ignore-keywords,
#        --skip-patch-id, --by-commit, --bug-id=), and <upstream>.
# Returns:
#   0 on success, 1 on failure.
function merge() {
  local dry_run
  local continue_with_conflicts
  local had_conflicts
  local first_parent=1
  local batch_skip
  local ignore_keywords
  local restrict_automerge
  local skip_patch_id
  local by_commit
  local cherry_pick
  local multi_way_merge
  local -A manual_skips=()
  local -a bug_ids=()
  local log_limit=100
  local -a positional_args=()

  while (( $# > 0 )); do
    case "$1" in
      --dry-run)
        dry_run=1
        ;;
      --continue-with-conflicts)
        continue_with_conflicts=1
        ;;
      --automerger)
        by_commit=1
        first_parent=1
        skip_patch_id=1
        automerger=1
        ;;
      --first-parent)
        first_parent=1
        by_commit=1
        ;;
      --flatten)
        first_parent=
        by_commit=1
        ;;
      --batch-skip)
        batch_skip=1
        ;;
      --ignore-keywords)
        ignore_keywords=1
        ;;
      --restrict-automerge)
        restrict_automerge=1
        ;;
      --skip-patch-id)
        skip_patch_id=1
        ;;
      --by-commit)
        by_commit=1
        ;;
      --cherry-pick)
        cherry_pick=1
        by_commit=1
        batch_skip=1
        first_parent=
        ;;
      --multi-way-merge)
        multi_way_merge=1
        ;;
      --manual-skip=*)
        local val="${1#*=}"
        manual_skips["${val%%::*}"]="${val#*::}"
        ;;
      --bug-id=*)
        bug_ids+=("${1#*=}")
        ;;
      --log=*)
        log_limit="${1#*=}"
        ;;
      -*)
        logging::error_usage_out "Unknown option: $1"
        ;;
      *)
        positional_args+=("$1")
        ;;
    esac
    shift
  done

  local upstream="${positional_args[0]:-}"

  if [[ -z "${upstream}" ]]; then
    logging::error_usage_out "Missing upstream branch"
  fi

  fetch_if_needed "${upstream}"
  _assert_valid_objects "${upstream}"

  local upstream_ref
  upstream_ref="$(git rev-parse --symbolic-full-name "${upstream}" 2>/dev/null)"
  local upstream_label=""
  if [[ "${upstream_ref}" == refs/heads/* ]]; then
    upstream_label="${upstream_ref#refs/heads/}"
  elif [[ "${upstream_ref}" == refs/remotes/* ]]; then
    upstream_label="${upstream_ref#refs/remotes/*/}"
  elif [[ "${upstream_ref}" == refs/tags/* ]]; then
    upstream_label="${upstream_ref#refs/tags/}"
  fi

  local pending_merge_mode=""
  local -a pending_merge_commits=()
  local -A pending_merge_info=()

  function _flush_pending_merges() {
    if (( ${#pending_merge_commits[@]} == 0 )); then
      return 0
    fi

    local mode="${pending_merge_mode}"
    case "${mode}" in
      skip|merge) ;;
      *) logging::error_out "Internal error: unknown merge mode ${mode}" ;;
    esac

    local -a independent_commits=()
    local -a reported_commits=()
    local i
    for (( i=${#pending_merge_commits[@]}-1; i>=0; i-- )); do
      local c="${pending_merge_commits[i]}"
      local r="${pending_merge_info[${c}]:-}"
      independent_commits+=("${c}")
      # Avoid exceeding ARG_MAX, just in case.
      if (( ${#independent_commits[@]} >= 1000 )); then
        readarray -t independent_commits < <(
          git merge-base --independent "${independent_commits[@]}"
        )
      fi
      if [[ "${r}" == "Skip reason: flatten mode" ]]; then
        continue
      fi
      reported_commits+=("${c}")
    done
    readarray -t independent_commits < <(
      git merge-base --independent "${independent_commits[@]}"
    )

    if (( ${#independent_commits[@]} > 1 )) && [[ -z "${multi_way_merge}" ]]; then
      logging::error_out \
        "Multiple independent commits found. Use --multi-way-merge to allow multi-way merge."
    fi

    local -a merge_args=(--no-ff --signoff)
    if [[ "${mode}" == "skip" ]]; then
      merge_args+=(-s ours)
    fi

    # Commit subject
    local subject
    if [[ "${mode}" == "merge" ]] && (( ${#reported_commits[@]} == 1 )); then
      subject="$(git show -s --pretty=%s "${reported_commits[0]}")"
      subject="MERGE: ${subject#MERGE: }"
    else
      case "${mode}" in
        skip)
          subject="Skip"
          ;;
        merge)
          subject="Merge"
          ;;
      esac
      if (( ${#reported_commits[@]} > 0 )); then
        subject+=" ${#reported_commits[@]} commit(s)"
      else
        subject+=" merge commits"
      fi
      if [[ -n "${upstream_label}" ]]; then
        subject+=" from ${upstream_label}"
      fi
    fi
    subject+=$'\n'

    # Commit body
    local body=""
    if [[ "${mode}" == "merge" ]] && (( ${#reported_commits[@]} == 1 )); then
      body="$(git show -s --pretty=%b "${reported_commits[0]}")"$'\n'
      body+="(merged from commit ${reported_commits[0]})"  # No $'\n', to connect with the footer
    else
      for (( i=0; i<${#reported_commits[@]}; i++ )); do
        local c="${reported_commits[i]}"
        local r="${pending_merge_info[${c}]:-}"
        if (( i >= log_limit )); then
          body+="... and $(( ${#reported_commits[@]} - log_limit )) more commits ..."$'\n'
          break
        fi
        body+="$(format_commit "${c}")"$'\n'
        if [[ -n "${r}" ]]; then
          body+="$(_add_prefix "  " "${r}")"$'\n'
        fi
      done
    fi

    # Commit footer
    local footer=""
    for id in "${bug_ids[@]}"; do
      footer+="Bug: ${id}"$'\n'
    done

    # Construct commit message
    local merge_message="${subject}"
    if [[ -n "${body}" ]]; then
      merge_message+=$'\n'"${body}"
    fi
    if [[ -n "${footer}" ]]; then
      merge_message+=$'\n'"${footer}"
    fi
    merge_args+=(--message="${merge_message}")

    if [[ -n "${dry_run}" ]]; then
      echo "[DRY RUN] ${merge_message}"
      pending_merge_commits=()
      pending_merge_info=()
      pending_merge_mode=""
      return 0
    fi

    local -a merge_env=()
    if [[ "${mode}" == "merge" ]] && (( ${#reported_commits[@]} == 1 )); then
      local author_name
      local author_email
      author_name="$(git show -s --pretty=%an "${reported_commits[0]}")"
      author_email="$(git show -s --pretty=%ae "${reported_commits[0]}")"
      merge_env=(GIT_AUTHOR_NAME="${author_name}" GIT_AUTHOR_EMAIL="${author_email}")
    fi

    local output
    local exit_code=0
    output="$(env "${merge_env[@]}" \
              git merge "${merge_args[@]}" "${independent_commits[@]}" 2>&1)" || exit_code=$?

    if (( exit_code == 1 )); then
      if [[ -n "${continue_with_conflicts}" ]] &&
         git rev-parse --verify -q MERGE_HEAD >/dev/null; then
        logging::warning "Merge conflicted. Committing conflicts."
        git add -u
        sed -i '1s/^/[conflict] /' "${GIT_DIR}/MERGE_MSG"
        if env "${merge_env[@]}" git commit --allow-empty --no-verify -s --no-edit; then
          exit_code=0
          had_conflicts=1
        else
          logging::error "Failed to commit merge conflicts."
          return 1
        fi
      else
        logging::error "Merge failed. Resolve conflicts and re-run this command to continue."
        return 1
      fi
    fi

    if (( exit_code != 0 )); then
      logging::error "${output}"
      return "${exit_code}"
    fi

    format_commit HEAD

    pending_merge_commits=()
    pending_merge_info=()
    pending_merge_mode=""
    return 0
  }

  if git merge-base --is-ancestor "${upstream}" HEAD; then
    logging::info "Nothing to merge"
    return 0
  fi

  local -a rev_list_args=("HEAD...${upstream}" "--right-only" "--reverse")

  if [[ -z "${by_commit}" ]]; then
    pending_merge_mode="merge"
    readarray -t pending_merge_commits < <(git rev-list "${rev_list_args[@]}")
    _flush_pending_merges || return $?
    if [[ -n "${had_conflicts}" ]]; then
      return 2
    fi
    return 0
  fi

  local -a rev_list_status_args=("${rev_list_args[@]}" "--left-right")
  if [[ -z "${skip_patch_id}" ]]; then
    rev_list_status_args+=("--cherry-mark")
  fi
  local -a commit_statuses=()
  readarray -t commit_statuses < <(git rev-list "${rev_list_status_args[@]}")

  # Using --first-parent together with --cherry-mark restricted the patch-id equivalence checking to
  # only the first-parent histories of HEAD. This meant that equivalent commits existing inside
  # merged branches of HEAD were completely invisible to the --cherry-mark step. To avoid the
  # problem, use an associative array to check if a commit belongs to the first-parent history.
  if [[ -n "${first_parent}" ]]; then
    rev_list_args+=("--first-parent")
  fi

  local c
  local -A first_parents=()
  if [[ -n "${first_parent}" ]]; then
    for c in $(git rev-list "${rev_list_args[@]}"); do
      first_parents["${c}"]=1
    done
  fi

  local -A simplified_history=()
  for c in $(git rev-list "${rev_list_args[@]}" "--" "${GIT_WORK_TREE}"); do
    simplified_history["${c}"]=1
  done

  local skip_keywords=(
    "DO NOT MERGE ANYWHERE"
  )
  if [[ -n "${restrict_automerge}" ]]; then
    skip_keywords+=(
      "DO NOT MERGE"
      "RESTRICT AUTOMERGE"
    )
  fi

  local -i skipped=0
  local -i merged=0
  local -i count=0
  local -i total="${#commit_statuses[@]}"

  local line
  for line in "${commit_statuses[@]}"; do
    local status="${line:0:1}"
    local commit="${line:1}"
    local formatted_commit
    formatted_commit="$(format_commit "${commit}")"

    logging::verbose "[$(( ++count ))/${total}] ${formatted_commit}"

    if [[ -n "${first_parent}" ]] && [[ -z "${first_parents[${commit}]}" ]]; then
      logging::verbose "Ignore non-first-parent commit"
      continue
    fi

    local skip=
    if [[ -z "${first_parent}" ]]; then
      if git rev-parse -q --verify "${commit}^2" >/dev/null; then
        skip="flatten mode"
      fi
    fi

    if [[ -z "${skip}" ]] && [[ -z "${simplified_history[${commit}]}" ]]; then
      skip="commit has no effect on the tree (empty or redundant)"
    fi

    local subject
    subject="$(git show -s --pretty=%s "${commit}")"
    if [[ -z "${skip}" ]] && [[ -z "${ignore_keywords}" ]]; then
      local keyword
      for keyword in "${skip_keywords[@]}"; do
        if grep -q "${keyword}" <<<"${subject}"; then
          skip="${keyword}"
          break
        fi
      done
    fi
    if [[ -z "${skip}" ]]; then
      local keyword
      for keyword in "${!manual_skips[@]}"; do
        if grep -q "${keyword}" <<<"${subject}"; then
          skip="Manual - ${manual_skips[${keyword}]}"
          break
        fi
      done
    fi

    if [[ -z "${skip}" ]] && [[ "${status}" == "="  ]]; then
      skip="Found equivalent commit by batched patch-id result"
    fi

    if [[ -z "${skip}" ]]; then
      local match_out
      logging::push_prefix "  "
      local -a find_commit_args=(--show-method --skip-patch-id)
      if [[ -n "${automerger}" ]]; then
        find_commit_args+=(--automerger)
      fi
      if match_out="$(find_commit "${find_commit_args[@]}" "${commit}" HEAD)"; then
        skip="${match_out}"
      fi
      logging::pop_prefix
    fi

    if [[ -z "${skip}" ]] && [[ -n "${cherry_pick}" ]]; then
      if [[ -n "${dry_run}" ]]; then
        skip="cherry-picked (dry-run)"
      else
        if git cherry-pick -x -s "${commit}" >/dev/null 2>&1; then
          skip="cherry-picked"
        elif [[ -n "${continue_with_conflicts}" ]]; then
          logging::warning "Cherry-pick conflicted for ${formatted_commit}. Committing conflicts."
          git add -u
          sed -i '1s/^/[conflict] /' "${GIT_DIR}/MERGE_MSG"
          git commit --allow-empty --no-verify -s --no-edit
          had_conflicts=1
          skip="cherry-picked"
        else
          logging::error "Cherry-pick failed for ${formatted_commit}." \
                         "Resolve conflicts and re-run this command to continue."
          return 1
        fi
      fi
    fi

    if [[ -n "${skip}" ]]; then
      skipped="$(( skipped + 1 ))"
      local skip_reason="Skip reason: ${skip}"
      if [[ "${pending_merge_mode}" != "skip" ]]; then
        _flush_pending_merges || return $?
        pending_merge_mode="skip"
      fi
      pending_merge_commits+=("${commit}")
      pending_merge_info["${commit}"]="${skip_reason}"
      logging::verbose "Skipping ${formatted_commit}"
      logging::verbose "$(_add_prefix "  " "${skip_reason}")"
      if [[ -z "${batch_skip}" ]]; then
        _flush_pending_merges || return $?
      fi
    else
      merged="$(( merged + 1 ))"
      if [[ "${pending_merge_mode}" != "merge" ]]; then
        _flush_pending_merges || return $?
        pending_merge_mode="merge"
      fi
      logging::verbose "Merging ${formatted_commit}"
      pending_merge_commits+=("${commit}")
      _flush_pending_merges || return $?
    fi
  done

  _flush_pending_merges || return $?

  logging::info "Skipped: ${skipped}, Merged: ${merged}"

  if [[ -n "${had_conflicts}" ]]; then
    return 2
  fi
}

# Main entry point for the gittool script.
#
# Arguments:
#   ...: Global options (-C, --no-fetch, format options), command, and command arguments.
function main() {
  local cmd
  local -a cmd_args=()

  while (( $# > 0 )); do
    case "$1" in
      -C)
        if ! cd "$2" 2>/dev/null; then
          logging::error_usage_out "Cannot cd to $2"
        fi
        shift
        ;;
      --no-fetch)
        GIT_NO_FETCH=1
        ;;
      --pretty=*|--format=*|--abbrev=*)
        set_format_option "$1"
        ;;
      -*)
        if logging::set_option "$1"; then
          :
        elif [[ -z "${cmd}" ]]; then
          logging::error_usage_out "Unknown option: $1"
        else
          cmd_args+=("$1")
        fi
        ;;
      *)
        if [[ -z "${cmd}" ]]; then
          cmd="$1"
        else
          cmd_args+=("$1")
        fi
        ;;
    esac
    shift
  done

  if [[ -z "${cmd}" ]]; then
    logging::usage_out
  fi

  GIT_DIR="$(git rev-parse --git-dir)"
  GIT_WORK_TREE="$(git rev-parse --show-toplevel)"

  case "${cmd}" in
    fetch_if_needed|find_commit|find_missing_commits|merge)
      "${cmd}" "${cmd_args[@]}"
      ;;
    *)
      logging::error_usage_out "Unknown command: ${cmd}"
      ;;
  esac
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  main "$@"
fi
