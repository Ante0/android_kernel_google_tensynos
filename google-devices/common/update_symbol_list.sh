# SPDX-License-Identifier: GPL-2.0
#!/bin/bash
#
# Copyright (C) Google LLC, 2022
# Author: Will McVicker (willmcvicker@google.com)

BUILD_ON_BUILD_BOT=`echo ${DIST_DIR} | grep \/buildbot\/`
if [ -z "$BUILD_ON_BUILD_BOT" ]; then
  GKI_REMOTE="aosp"
else
  GKI_REMOTE="android"
  git -C common/ack branch -D update_symbol_list-delete-after-push
fi
GKI_BRANCH="android14-6.1" # Need to push symbol list changes to the main ACK branch (not release branches)
PIXEL_SYMBOL_LIST="android/abi_gki_aarch64_pixel"
TARGET=
FOR_AOSP_PUSH_BRANCH="update_symbol_list-delete-after-push"
CONTINUE_AFTER_REBASE=0
CHANGE_ID=
BUG=

function usage {
  ret="$1"

  echo "$0 --config TARGET [-p|--prepare-aosp-abi BUG_NUMBER] [--change-id CHANGE_ID] [--continue]"
  echo
  echo "This script will update the pixel symbol list in common/ack."
  echo
  echo " The following arguments are supported:"
  echo "  --config <device-name>               Specifies which target to build."
  echo "  -p | --prepare-aosp-abi BUG_NUMBER   Update the AOSP ABI xml and symbol list."
  echo "                                       and create a commit with the provided BUG_NUMBER."
  echo "  -c | --continue                      Continue after the rebase failure."
  echo "  --change-id CHANGE_ID                Use this Change-Id when creating the commit."
  echo "  --commit BUG_NUMBER                  [DEPRECATED to keep same flag as 5.10] same as -p|--prepare-aosp-abi"
  exit ${ret}
}

# Add a trap to remove the temporary files in case of an error on early exit.
cleanup_trap() {
  rm -f ${COMMIT_TEXT}
  exit $1
}
trap 'cleanup_trap' EXIT

function exit_if_error {
  if [ $1 -ne 0 ]; then
    echo "ERROR: $2: retval=$1" >&2
    exit $1
  fi
}

while [[ $# -gt 0 ]]; do
  next="$1"
  case ${next} in
  --config)
    TARGET="$2"
    shift
    ;;
  -p|--prepare-aosp-abi|--commit)
    BUG="$2"
    if ! [[ "${BUG}" =~ ^[0-9]+$ ]]; then
      exit_if_error 1 "Bug numbers should be digits."
    fi
    shift
    ;;
  -c|--continue)
    CONTINUE_AFTER_REBASE=1
    ;;
  --change-id)
    CHANGE_ID="$2"
    if ! [[ "${CHANGE_ID}" =~ ^I[0-9a-f]{40}$ ]]; then
      exit_if_error 1 \
        "Invalid Change-Id. Make sure it starts with 'I' followed by 40 hex characters"
    fi
    shift
    ;;
  -h|--help)
    usage 0
    ;;
  *)
    echo "Invalid argument $1"
    usage 1
    ;;
  esac
  shift
done

function bazel_cquery() {
  tools/bazel cquery --config="${TARGET}" --kernel_package=ack "$@"
}

function bazel_run() {
  tools/bazel run --config="${TARGET}" --kernel_package=ack "$@"
}

KERNEL_TARGET="$(bazel_cquery \
  'kind("kernel_build rule", deps(//private/devices/google/common:kernel))' \
  2>/dev/null | head -n 1 | cut -d ' ' -f1)"
if [[ -z "${KERNEL_TARGET}" ]]; then
  echo "Could not detect kernel target" >&2
  exit 1
fi
echo "KERNEL_TARGET=${KERNEL_TARGET}"

KERNEL_DIR="$(bazel_cquery \
  'filter(kernel_aarch64_sources, deps(//private/devices/google/common:kernel))' \
  2>/dev/null | tail -n 1 | sed -n 's://\(.*\)\:kernel_aarch64_sources.*:\1:p')"
if [[ -z "${KERNEL_DIR}" ]]; then
  echo "Could not detect kernel dir"
  exit 1
fi
echo "KERNEL_DIR=${KERNEL_DIR}"


function verify_aosp_tree {
  pushd "${KERNEL_DIR}" >/dev/null
    if ! git diff --quiet HEAD; then
      exit_if_error 1 \
        "Found uncommitted changes in "${KERNEL_DIR}". Commit your changes before updating the ABI"
    fi

    if [ "${CONTINUE_AFTER_REBASE}" = "0" ]; then
      if git branch | grep "\<${FOR_AOSP_PUSH_BRANCH}\>" 2>&1 >/dev/null; then
        echo "The branch '${FOR_AOSP_PUSH_BRANCH}' already exists in "${KERNEL_DIR}". Please delete" >&2
        echo "this branch (git branch -D ${FOR_AOSP_PUSH_BRANCH}) before continuing." >&2
        exit 1
      fi

      AOSP_CUR_BRANCH_OR_SHA1=$(git branch --show-current)
      if [ -z "${AOSP_CUR_BRANCH_OR_SHA1}" ]; then
        AOSP_CUR_BRANCH_OR_SHA1=$(git log -1 --pretty="format:%H")
      fi
    else
      # Make sure they didn't switch branches when addressing the rebase conflict
      if [ "${FOR_AOSP_PUSH_BRANCH}" != "$(git branch --show-current)" ]; then
        exit_if_error 1 "For --continue, you need to be on the branch ${FOR_AOSP_PUSH_BRANCH}"
      fi
    fi
  popd >/dev/null
}

function print_final_message {
  echo "========================================================"
  if ! git -C "${KERNEL_DIR}" diff --quiet ${GKI_REMOTE}/${GKI_BRANCH}..HEAD; then
    echo " A symbol list commit in ${KERNEL_DIR} was created for you."
    echo
    echo " Please verify your commit(s) before pushing. Here are the steps to perform:"
    echo
    echo "   cd ${KERNEL_DIR}"
    echo "   git log --oneline ${FOR_AOSP_PUSH_BRANCH}"
    echo "   git push ${GKI_REMOTE} ${FOR_AOSP_PUSH_BRANCH:-HEAD}:refs/for/${GKI_BRANCH}"
    echo
    if [ -n "${FOR_AOSP_PUSH_BRANCH}" ]; then
      echo " After pushing your changes to ${KERNEL_DIR}, you can delete the temporary"
      echo " branch: ${FOR_AOSP_PUSH_BRANCH} using the command:"
      echo
      echo "   cd ${KERNEL_DIR}"
      echo "   git branch -D ${FOR_AOSP_PUSH_BRANCH}"
      echo
    fi
  else
    echo " No changes were detected after rebasing to the tip of tree."
  fi

  # Rollback to the original branch/commit
  if [ -n "${AOSP_CUR_BRANCH_OR_SHA1}" ]; then
    git -C "${KERNEL_DIR}" checkout --quiet ${AOSP_CUR_BRANCH_OR_SHA1}
  fi
}

function commit_the_symbol_list {
  echo "Committing symbol list: ${KERNEL_DIR}"

  NEW_SYMS=$(git -C "${KERNEL_DIR}" diff ${PIXEL_SYMBOL_LIST} 2>/dev/null | sed -n 's/^+\s\+\(.*\)/\1\n/p')
  OLD_SYMS=$(git -C "${KERNEL_DIR}" diff ${PIXEL_SYMBOL_LIST} 2>/dev/null | sed -n 's/^-\s\+\(.*\)/\1\n/p')

  ADDING=$(for s in ${NEW_SYMS}; do [[ ! "${OLD_SYMS}" =~ ${s} ]] && echo "${s}"; done)
  REMOVING=$(for s in ${OLD_SYMS}; do [[ ! "${NEW_SYMS}" =~ ${s} ]] && echo "${s}"; done)

  # Create the symbol list commit
  COMMIT_TEXT=$(mktemp -t abi_sym_commit_text.XXXXX)
  echo "ANDROID: ABI: Update pixel symbol list" > ${COMMIT_TEXT}
  echo >> ${COMMIT_TEXT}
  if [ -n "${ADDING}" ]; then
    echo "Adding the following symbols:" >> ${COMMIT_TEXT}
    for s in ${ADDING}; do
      echo "  - $s" >> ${COMMIT_TEXT}
    done
    echo >> ${COMMIT_TEXT}
  fi
  if [ -n "${REMOVING}" ]; then
    echo "Removing the following symbols:" >> ${COMMIT_TEXT}
    for s in ${REMOVING}; do
      echo "  - $s" >> ${COMMIT_TEXT}
    done
    echo >> ${COMMIT_TEXT}
  fi

  echo "Bug: ${BUG}" >> ${COMMIT_TEXT}
  if [ -n "${CHANGE_ID}" ]; then
    echo "Change-Id: ${CHANGE_ID}" >> ${COMMIT_TEXT}
  fi
  git -C "${KERNEL_DIR}" commit --quiet -s -F ${COMMIT_TEXT} -- android/
  if [[ "$?" != 0 ]]; then
    rm -f ${COMMIT_TEXT}
    echo "No symbol list changes detected in ${KERNEL_DIR}."
    exit 0
  fi
  git -C "${KERNEL_DIR}" checkout --quiet -b ${FOR_AOSP_PUSH_BRANCH}
  echo "done..."
  rm -f ${COMMIT_TEXT}
}


# Verify the aosp tree is in a good state before compiling anything
verify_aosp_tree

if [ "${CONTINUE_AFTER_REBASE}" = "0" ]; then
  # Update the symbol list now
  bazel_run ${KERNEL_TARGET}_abi_update_symbol_list
  exit_if_error $? "Failed to update the ${TARGET} symbol list"

  # Sometimes protected exports are not up to date, which blocks abi update.
  bazel_run //${KERNEL_DIR}:kernel_aarch64_abi_update_protected_exports
  # Update abi, return 0: no update, 4: updated
  bazel_run //${KERNEL_DIR}:kernel_aarch64_abi_update
  ret="$?"
  if (( ret != 4 )); then
    exit_if_error ${ret} "Failed to update abi_gki_aarch64.stg"
  fi

  if [ -z "${BUG}" ]; then
    # Not committing the change
    echo
    echo "The symbol list in ${KERNEL_DIR} was updated. If you want to commit this to AOSP,"
    echo "then re-run this script with the --commit BUG_NUMBER command line argument."
    exit 0
  fi

  commit_the_symbol_list ${KERNEL_DIR}
fi

print_final_message
