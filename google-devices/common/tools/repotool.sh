#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

source "$(dirname "$(realpath "${BASH_SOURCE[0]}")")/logging.sh"
GITTOOL="$(dirname "$(realpath "${BASH_SOURCE[0]}")")/gittool.sh"
readonly GITTOOL

USAGE="$(cat <<EOF
usage: repotool [<options>] <command> [<args>]

OPTIONS:
    --exclude=<pattern> Exclude projects matching <pattern> (using grep -v).
                        Can be given multiple times.
    --filter=<pattern>  Filter projects matching <pattern> (using grep).
                        Can be given multiple times.
    --include=<pattern> Include projects matching <pattern> (using grep) from
                        the original project list. Can be given multiple times.
    -r, --regex=<regex> Get the project list based on regex or wildcard matching
                        of project paths. Can be given multiple times.

COMMANDS:
  list
    List the projects matching the filters.

  merge [<options>] <upstream>
    Merge a branch across multiple repo projects.

    --abort             Abort the in-progress merge and abandon the local topic branches.
                        Requires --topic.
    --bug-id=<id>       Append "Bug: <id>" to all generated commit messages.
    --continue-with-conflicts
                        Commit conflicts with "[conflict]" subject prefix and continue.
    --by-commit         Merge by commit using gittool.
    --cherry-pick       Merge by cherry-picking each commit using gittool.
                        Implies --by-commit and --flatten.
    --flatten           Flatten upstream history (with --by-commit only).
    --restrict-automerge
                        Respect "DO NOT MERGE" and "RESTRICT AUTOMERGE"
                        keywords. "DO NOT MERGE ANYWHERE" is always
                        respected. (With --by-commit only).
    --dry-run           Show what would be merged without actually merging.
    --drop-empty-merges Drop the merge if it results in no tree changes.
    --topic=<topic>     Use the specific topic branch name. Also can be used to
                        continue or abort an unsuccessful merge. Defaults to
                        "merge-<timestamp>".

  rebase [<options>] [<upstream>]
    Rebase across multiple repo projects. Forward all arguments to git rebase.

  download [<options>] --topic=<topic>
    Download a Gerrit topic across multiple repo projects and store as a local topic.

    --query=<query>       Use a custom Gerrit query. Please copy the string from the gerrit host URL
                          after /q/. e.g. http://android-review.googlesource.com/q/<copy-query-here>
                          Defaults to "topic:<topic>+status:open".
    -c, --cherry-pick     Cherry-pick instead of checkout.
    -x, --record-origin   Pass -x when cherry-picking.
    -r, --revert          Revert instead of checkout.
    -f, --ff-only         Force fast-forward merge.
EOF
)"
readonly USAGE

export GIT_PAGER=""

PROJECTS=()

function _curl() {
  echo "curl" "$@" >&2
  if command -v gob-curl >/dev/null 2>&1; then
    gob-curl "$@"
  else
    curl -n "$@"
  fi
}

function _url_encode() {
  python3 -c "import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))" "$1"
}

function _parse_gerrit_json() {
  python3 -c "import sys, json; \
    [print('%s %s' % (c['project'], c['_number'])) for c in json.load(sys.stdin)]" \
    2>/dev/null
}

# Status Definitions (Order determines the reporting order)
readonly STATUS_DEFS=(
  # Constant Name         Label
  STATUS_SKIP_NOSOURCE   "SKIPPED (No source branch)"
  STATUS_SKIP_NOTHING    "SKIPPED (Nothing to merge)"
  STATUS_SKIP_EMPTY      "SKIPPED (Empty merge)"
  STATUS_SKIP_NOMERGE    "SKIPPED (No merge in progress)"
  STATUS_SKIP_UPTODATE   "SKIPPED (Up to date)"
  STATUS_SKIP_NOBRANCH   "SKIPPED (Not on a branch)"
  STATUS_SKIP_NOREBASE   "SKIPPED (No rebase in progress)"
  STATUS_MERGED          "MERGED"
  STATUS_MERGED_WITH_CONFLICTS "MERGED (Conflict)"
  STATUS_REBASED         "REBASED"
  STATUS_DOWNLOADED      "DOWNLOADED"
  STATUS_ABORTED         "ABORTED"
  STATUS_CONFLICT        "FAILED (Conflict)"
  STATUS_DIRTY           "FAILED (Uncommitted changes)"
  STATUS_FAILURE         "FAILED (Unknown error)"
)

# Initialize status constants and labels
declare -a STATUS_LABELS=()
for ((i=0; i<${#STATUS_DEFS[@]}; i+=2)); do
  _name="${STATUS_DEFS[i]}"
  _label="${STATUS_DEFS[i+1]}"
  _id=$((i / 2))
  STATUS_LABELS[_id]="${_label}"
  declare -r "${_name}=${_id}"
done
unset _name _label _id

function _wait_and_report_results() {
  local -n _pids=$1
  local -n _projs=$2
  local -n _groups=$3

  local i
  for i in "${!_pids[@]}"; do
    local code=0
    wait "${_pids[i]}" || code=$?
    _groups[code]+="${_projs[i]}"$'\n'
  done

  logging::info ""
  logging::info "Results:"
  for i in "${!STATUS_LABELS[@]}"; do
    if [[ -n "${_groups[${i}]}" ]]; then
      local proj
      for proj in ${_groups[${i}]}; do
        logging::info "${STATUS_LABELS[i]}: ${proj}"
      done
    fi
  done
}

function download() {
  local topic=""
  local query=""
  local -a forward_args=()
  while (( $# > 0 )); do
    case "$1" in
      --topic=*)
        topic="${1#*=}"
        ;;
      --query=*)
        query="${1#*=}"
        ;;
      -c|--cherry-pick|-x|--record-origin|-r|--revert|-f|--ff-only)
        forward_args+=("$1")
        ;;
      -*)
        logging::set_option_or_out "$1"
        ;;
      *)
        logging::error_usage_out "Unexpected argument: $1"
        ;;
    esac
    shift
  done

  if [[ -z "${topic}" ]]; then
    logging::error_usage_out "--topic is required"
  fi

  local -A review_urls=()
  local proj
  local urls
  # Gather review URLs from projects
  for proj in "${PROJECTS[@]}"; do
    urls=$(git -C "${proj}" config --get-regexp "remote\..*\.review" | \
           awk '{print $2}' || true)
    for url in ${urls}; do
      review_urls["${url}"]=1
    done
  done

  if [[ ${#review_urls[@]} -eq 0 ]]; then
    logging::error_out "Could not find any Gerrit review URLs"
  fi

  local encoded_topic
  encoded_topic="$(_url_encode "${topic}")"

  if [[ -z "${query}" ]]; then
    query="topic:${encoded_topic}+status:open"
  fi

  local -A project_to_changes=()
  local review_url
  for review_url in "${!review_urls[@]}"; do
    logging::info "Querying ${review_url} for topic: ${topic}..."
    local json
    local base_url="${review_url%/}"
    json=$(_curl -s "${base_url}/changes/?q=${query}" \
          | sed '1d')

    local results
    results=$(echo "${json}" | _parse_gerrit_json 2>/dev/null || true)

    if [[ -n "${results}" ]]; then
      local project_name
      local change_number
      while read -r project_name change_number; do
        project_to_changes["${project_name}"]+="${change_number} "
      done <<< "${results}"
    fi
  done

  if [[ ${#project_to_changes[@]} -eq 0 ]]; then
    logging::info "No open changes found for topic ${topic}"
    return 0
  fi

  function _download_project() {
    local proj_path="$1"
    local -a changes
    read -r -a changes <<< "$2"
    (
      cd "${proj_path}"
      logging::push_prefix "[${proj_path}] "
      trap "logging::pop_prefix; trap - RETURN" RETURN

      if [[ -n "$(git status --porcelain)" ]]; then
        logging::error "${proj_path} has uncommitted changes"
        return "${STATUS_DIRTY}"
      fi

      local -a fetched_shas=()
      local change
      for change in "${changes[@]}"; do
        # Download the change (detaches HEAD)
        if ! repo download . "${change}" "${forward_args[@]}" >/dev/null 2>&1; then
          logging::error "Download failed for change: ${change}"
          return "${STATUS_FAILURE}"
        fi
        fetched_shas+=("$(git rev-parse HEAD)")
      done

      local tips
      tips=$(git merge-base --independent "${fetched_shas[@]}")
      if [[ $(echo "${tips}" | wc -w) -eq 1 ]]; then
        if ! repo start "${topic}" . >/dev/null 2>&1; then
          logging::error "Unable to start topic branch ${topic}"
          return "${STATUS_FAILURE}"
        fi
        git reset --hard "${tips}" -q
      else
        logging::error "Changes for ${proj_path} in topic ${topic} have diverged."
        logging::error "Tips: $(echo "${tips}" | tr '\n' ' ')"
        return "${STATUS_FAILURE}"
      fi

      logging::info "Successfully downloaded topic ${topic}"
      return "${STATUS_DOWNLOADED}"
    )
  }

  local -a pids=()
  local -a active_projects=()

  local -A allowed_paths=()
  local p
  for p in "${PROJECTS[@]}"; do
    allowed_paths["${p}"]=1
  done

  for project_name in "${!project_to_changes[@]}"; do
    if [[ -z "${project_name}" ]]; then
      continue
    fi
    local path
    path=$(repo list -p "${project_name}" 2>/dev/null || true)
    if [[ -z "${path}" ]] || [[ -z "${allowed_paths[${path}]}" ]]; then
      continue
    fi

    _download_project "${path}" "${project_to_changes[${project_name}]}" & pids+=("$!")
    active_projects+=("${path}")
  done

  if [[ ${#active_projects[@]} -eq 0 ]]; then
    logging::info "No changes found for the filtered projects."
    return 0
  fi

  local -a result_groups=()
  _wait_and_report_results pids active_projects result_groups

  if [[ -n "${result_groups[STATUS_FAILURE]}" ]] ||
     [[ -n "${result_groups[STATUS_DIRTY]}" ]]; then
    return 1
  fi
}

function rebase() {
  local -a forward_args=("$@")

  function _rebase_project() {
    local proj="$1"
    (
      cd "${proj}"

      logging::push_prefix "[${proj}] "
      trap "logging::pop_prefix; trap - RETURN" RETURN

      local is_special=
      local arg
      for arg in "${forward_args[@]}"; do
        case "${arg}" in
          --continue|--abort|--skip|--edit-todo)
            is_special=1
            break
            ;;
        esac
      done

      if [[ -z "${is_special}" ]] && [[ -n "$(git status --porcelain)" ]]; then
        logging::error "${proj} has uncommitted changes"
        return "${STATUS_DIRTY}"
      fi

      local output
      local exit_code=0
      output="$(git rebase "${forward_args[@]}" 2>&1)" || exit_code=$?

      if [[ -n "${output}" ]]; then
        logging::info "Rebase result:"
        logging::info "${output}"
      fi

      if (( exit_code != 0 )); then
        if [[ "${output}" =~ "CONFLICT" ]] || [[ "${output}" =~ "conflicts" ]]; then
          logging::error "Rebase conflict!"
          return "${STATUS_CONFLICT}"
        fi
        if [[ "${output}" == *"You are not currently on a branch"* ]]; then
          logging::warning "Not on a branch, skipped."
          return "${STATUS_SKIP_NOBRANCH}"
        fi
        if [[ "${output}" == *"no rebase in progress"* ]]; then
          logging::warning "No rebase in progress, skipped."
          return "${STATUS_SKIP_NOREBASE}"
        fi
        logging::error "Rebase failed."
        return "${STATUS_FAILURE}"
      fi

      if [[ "${output}" == *"is up to date"* ]]; then
        logging::info "Nothing to rebase."
        return "${STATUS_SKIP_UPTODATE}"
      fi

      logging::info "Rebase succeed."
      return "${STATUS_REBASED}"
    )
  }

  local -a pids=()
  local proj
  for proj in "${PROJECTS[@]}"; do
    _rebase_project "${proj}" & pids+=("$!")
  done

  local -a result_groups=()
  _wait_and_report_results pids PROJECTS result_groups

  if [[ -n "${result_groups[STATUS_CONFLICT]}" ]] ||
     [[ -n "${result_groups[STATUS_FAILURE]}" ]] ||
     [[ -n "${result_groups[STATUS_DIRTY]}" ]]; then
    return 1
  fi
}

function merge() {
  local abort
  local dry_run
  local drop_empty_merges
  local topic
  local -a forward_args=()
  local -a positional_args=()

  while (( $# > 0 )); do
    case "$1" in
      --abort)
        abort=1
        ;;
      --dry-run)
        forward_args+=("$1")
        dry_run=1
        ;;
      --drop-empty-merges)
        drop_empty_merges=1
        ;;
      --topic=*)
        topic="${1#*=}"
        ;;
      -*)
        if logging::set_option "$1"; then
          :
        else
          forward_args+=("$1")
        fi
        ;;
      *)
        positional_args+=("$1")
        ;;
    esac
    shift
  done

  if [[ -n "${abort}" && -z "${topic}" ]]; then
    logging::error_usage_out "--topic is required for --abort"
  fi

  if [[ -z "${topic}" ]]; then
    topic="merge-$(date +%s)"
  fi

  local upstream="${positional_args[0]:-}"

  if [[ -z "${upstream}" && -z "${abort}" ]]; then
    logging::error_usage_out "<upstream> is required"
  fi

  function _abort_project() {
    local proj="$1"
    (
      cd "${proj}"

      logging::push_prefix "[${proj}] "
      trap "logging::pop_prefix; trap - RETURN" RETURN

      if ! git rev-parse --verify -q "${topic}" >/dev/null; then
        return "${STATUS_SKIP_NOMERGE}"
      fi

      if [[ -n "${dry_run}" ]]; then
        return "${STATUS_ABORTED}"
      fi

      git merge --abort 2>/dev/null || true
      git cherry-pick --abort 2>/dev/null || true

      if ! repo abandon "${topic}" . >/dev/null 2>&1; then
        return "${STATUS_FAILURE}"
      fi

      git branch -D "${topic}-pre-merge-head" >/dev/null 2>&1 || true

      return "${STATUS_ABORTED}"
    )
  }

  function _merge_project() {
    local proj="$1"
    (
      cd "${proj}"

      if [[ -n "$(git status --porcelain)" ]]; then
        logging::error "${proj} has uncommitted changes"
        return "${STATUS_DIRTY}"
      fi

      logging::push_prefix "[${proj}] "
      trap "logging::pop_prefix; trap - RETURN" RETURN

      "${GITTOOL}" fetch_if_needed "${upstream}" >/dev/null 2>&1 || true

      if ! git rev-parse --verify -q "${upstream}^{commit}" >/dev/null 2>&1; then
        logging::warning "Cannot find ${upstream}, skipped."
        return "${STATUS_SKIP_NOSOURCE}"
      fi

      if git merge-base --is-ancestor "${upstream}" HEAD 2>/dev/null; then
        logging::info "Nothing to merge."
        return "${STATUS_SKIP_NOTHING}"
      fi

      local original_branch
      original_branch=$(git branch --show-current)
      if [[ -z "${original_branch}" ]]; then
        original_branch=$(git rev-parse HEAD)
      fi

      local pre_merge_head
      if [[ -z "${dry_run}" ]]; then
        # Create pre-merge head branch if it doesn't exist yet
        if ! git rev-parse --verify -q "${topic}-pre-merge-head" >/dev/null; then
          git branch "${topic}-pre-merge-head" HEAD
        fi
        pre_merge_head="${topic}-pre-merge-head"

        if ! repo start --head "${topic}" . >/dev/null 2>&1; then
          if ! git checkout "${topic}" -q; then
            logging::warning "Unable to create or switch to topic ${topic}."
            return "${STATUS_FAILURE}"
          fi
        fi
      else
        pre_merge_head="HEAD"
      fi

      local -a gittool_args=(--no-fetch --quiet merge --batch-skip "${forward_args[@]}")
      if [[ -n "${dry_run}" ]]; then
        gittool_args+=(--dry-run)
      fi
      gittool_args+=("${upstream}")

      local output
      local exit_code=0
      output="$("${GITTOOL}" "${gittool_args[@]}" 2>&1)" || exit_code=$?

      if [[ -n "${output}" ]]; then
        logging::info "Merge result:"
        logging::info "${output}"
      fi

      if (( exit_code == 1 )); then
        logging::error "Merge conflict!"
        return "${STATUS_CONFLICT}"
      fi

      if (( exit_code == 2 )); then
        logging::warning "Merged with conflicts."
        return "${STATUS_MERGED_WITH_CONFLICTS}"
      fi

      if (( exit_code != 0 )); then
        logging::error "Merge failed."
        return "${STATUS_FAILURE}"
      fi

      if [[ -z "${output}" ]]; then
        logging::info "Nothing to merge."
        return "${STATUS_SKIP_NOTHING}"
      fi

      if [[ -n "${drop_empty_merges}" && -z "${dry_run}" ]]; then
        if git diff --quiet "${pre_merge_head}"; then
          logging::info "Tree has no differences after merge. Dropping merge."
          git reset --hard "${pre_merge_head}" -q
          repo abandon "${topic}" . >/dev/null 2>&1 || true
          git checkout "${original_branch}" -q
          git branch -D "${topic}-pre-merge-head" >/dev/null 2>&1 || true
          return "${STATUS_SKIP_EMPTY}"
        fi
      fi

      logging::info "Merge succeed."
      return "${STATUS_MERGED}"
    )
  }

  local -a pids=()

  local proj
  if [[ -n "${abort}" ]]; then
    for proj in "${PROJECTS[@]}"; do
      _abort_project "${proj}" & pids+=("$!")
    done
  else
    for proj in "${PROJECTS[@]}"; do
      _merge_project "${proj}" & pids+=("$!")
    done
  fi

  local -a result_groups=()
  _wait_and_report_results pids PROJECTS result_groups

  logging::info ""
  if [[ -n "${abort}" ]]; then
    logging::info "Abandon topic:"
    logging::info "${topic}"
  else
    logging::info "Topic:"
    logging::info "${topic}"
    if [[ -n "${result_groups[STATUS_CONFLICT]}" ||
          -n "${result_groups[STATUS_FAILURE]}" ||
          -n "${result_groups[STATUS_DIRTY]}" ]]; then
      logging::info ""
      logging::info "To continue, resolve failures and re-run with --topic=${topic}"
    fi
  fi

  if [[ -n "${result_groups[STATUS_CONFLICT]}" ]] ||
     [[ -n "${result_groups[STATUS_FAILURE]}" ]] ||
     [[ -n "${result_groups[STATUS_DIRTY]}" ]]; then
    return 1
  fi
}

function list() {
  local proj
  for proj in "${PROJECTS[@]}"; do
    echo "${proj}"
  done
}

function main() {
  local -a regexes=()
  local -a filter_ops=()
  local cmd
  local -a cmd_args=()
  while (( $# > 0 )); do
    case "$1" in
      --exclude=*)
        filter_ops+=("exclude" "${1#*=}")
        ;;
      --filter=*)
        filter_ops+=("filter" "${1#*=}")
        ;;
      --include=*)
        filter_ops+=("include" "${1#*=}")
        ;;
      --regex=*)
        regexes+=("${1#*=}")
        ;;
      -r)
        regexes+=("$2")
        shift
        ;;
      -*)
        if [[ -z "${cmd}" ]]; then
          logging::set_option_or_out "$1"
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

  local repo_root="${PWD}"
  while [[ ! -d "${repo_root}/.repo" && "${repo_root}" != "/" ]]; do
    repo_root="$(dirname "${repo_root}")"
  done

  if [[ ! -d "${repo_root}/.repo" ]]; then
    logging::error_out "Not in a repo workspace."
  fi

  cd "${repo_root}"

  local projects_str=""
  if [[ ${#regexes[@]} -gt 0 ]]; then
    projects_str="$(repo list -p -r "${regexes[@]}")"
  else
    projects_str="$(repo list -p)"
  fi

  if (( ${#filter_ops[@]} > 0 )); then
    local original_projects="${projects_str}"
    local i
    for (( i=0; i<${#filter_ops[@]}; i+=2 )); do
      local op="${filter_ops[i]}"
      local pat="${filter_ops[i+1]}"
      if [[ "${op}" == "exclude" ]]; then
        projects_str="$(echo "${projects_str}" | grep -v -e "${pat}" || true)"
      elif [[ "${op}" == "filter" ]]; then
        projects_str="$(echo "${projects_str}" | grep -e "${pat}" || true)"
      elif [[ "${op}" == "include" ]]; then
        local added
        added="$(echo "${original_projects}" | grep -e "${pat}" || true)"
        projects_str="$(printf "%s\n%s" "${projects_str}" "${added}" | awk 'NF && !seen[$0]++')"
      fi
    done
  fi

  if [[ -n "${projects_str}" ]]; then
    readarray -t PROJECTS < <(echo "${projects_str}" | sort -u)
  else
    logging::error_out "No projects found matching the criteria."
  fi

  case "${cmd}" in
    list|merge|rebase|download)
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
