# SPDX-License-Identifier: GPL-2.0-only

function envsetup() {
  WORKSPACE_DIR=

  local dir="${PWD}"
  while [[ "${dir}" != / ]]; do
    if [[ -e "${dir}/WORKSPACE" || -e "${dir}/WORKSPACE.bazel" || -e "${dir}/MODULE.bazel" ]]; then
      WORKSPACE_DIR="${dir}"
      break
    fi
    dir="$(dirname "${dir}")"
  done

  if [[ -z "${WORKSPACE_DIR}" ]]; then
    echo "Not in a bazel workspace." >&2
    exit 1
  fi

  readonly WORKSPACE_DIR
}

envsetup
