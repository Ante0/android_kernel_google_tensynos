# SPDX-License-Identifier: GPL-2.0-only

# Usage: setup_cog_output_dir <output_dir>
#
# Sets up the output directory for Cog. If the output directory is in a Cog
# workspace, a symlink will be created from the Cog workspace directory to a
# directory in the user's cache directory.
#
# Note that the parent directory of <output_dir> must already exist;
# otherwise, creating the symlink will fail. This is intentional, as it avoids
# the following problematic scenario:
#
#   setup_cog_output_dir output_root/output_dir
#   setup_cog_output_dir output_root
#
# If the first call created output_root, the second call would detect output_root
# as an existing directory and remove it, along with the symlink created by the
# first call.
function setup_cog_output_dir() {
  # Resolve the real path of the output directory. If the output directory has
  # already been linked outside of a Cog workspace, do not link it again to
  # avoid creating nested symlinks.
  local -r output_dir="$(readlink -m "$1")"

  # Match the Cog workspace format: /google/cog/cloud/<user>/<workspace>.
  if [[ ! "${output_dir}" =~ ^/google/cog/cloud/[^/]+/([^/]+) ]]; then
    mkdir -p "${output_dir}"
    return 0
  fi

  local -r cog_workspace_root="${BASH_REMATCH[0]}"
  local -r cog_workspace_name="${BASH_REMATCH[1]}"

  if [[ -d "${output_dir}" ]]; then
    echo "Detected existing output directory in the Cog workspace: ${output_dir}. Removing it..."
    rm -rf "${output_dir}"
  fi

  local -r link_destination_dir="${HOME}/.cache/cog/${cog_workspace_name}"
  local -r link_destination="${output_dir/"${cog_workspace_root}"/"${link_destination_dir}"}"

  echo "Creating symlink: ${output_dir} -> ${link_destination}"
  mkdir -p "${link_destination}"
  ln -snf "${link_destination}" "${output_dir}"
}
