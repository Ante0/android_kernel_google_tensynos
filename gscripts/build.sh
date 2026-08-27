#!/usr/bin/env bash
# shellcheck shell=bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

usage() {
cat <<EOF
Usage:
    $0 <gs201|zuma|zumapro|clean>
EOF
}

###############################################################################
# Arguments
###############################################################################

[[ $# -eq 1 ]] || {
    usage
    exit 1
}

case "$1" in
    clean)
        msg "Cleaning build tree"

        rm -rf "${OUT_DIR}"

        msg "Done"
        exit 0
        ;;

    *)
        TARGET="$1"
        require_target "${TARGET}"
        ;;
esac

###############################################################################
# Paths
###############################################################################

DEFCONFIG="$(defconfig_for_target "${TARGET}")"

CROSS_COMPILE="${TOOLCHAIN_BIN}/aarch64-linux-"
CC="${TOOLCHAIN_BIN}/aarch64-linux-gcc"

ANYKERNEL_DIR="${REPO_ROOT}/AnyKernel"

ensure_directories

###############################################################################
# Build
###############################################################################

msg "Building ${TARGET}"

make \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    CC="${CC}" \
    O="${OUT_DIR}" \
    -j"$(nproc)" \
    "${DEFCONFIG}"

make \
    CROSS_COMPILE="${CROSS_COMPILE}" \
    CC="${CC}" \
    O="${OUT_DIR}" \
    -j"$(nproc)"

###############################################################################
# Verify output
###############################################################################

IMAGE="${OUT_DIR}/arch/arm64/boot/Image.lz4"

[[ -f "${IMAGE}" ]] || die "Build failed: Image.lz4 not found."

DTB_DIR="${OUT_DIR}/google-devices/${TARGET}/dts"

[[ -d "${DTB_DIR}" ]] || die "DTB directory not found."

###############################################################################
# Prepare AnyKernel
###############################################################################

msg "Preparing AnyKernel"

rm -f \
    "${ANYKERNEL_DIR}/Image.lz4" \
    "${ANYKERNEL_DIR}/dtb"

cp "${IMAGE}" "${ANYKERNEL_DIR}/Image.lz4"

cat "${DTB_DIR}"/*.dtb > "${ANYKERNEL_DIR}/dtb"

msg "Kernel build completed"

if [[ "${CI:-false}" != "true" ]]; then
    ZIP_NAME="SULTAN17_${TARGET}.zip"

    msg "Creating ${ZIP_NAME}"
    msg "Packaging ${ZIP_NAME}"

	(
    		cd "${ANYKERNEL_DIR}"

    		zip -r \
        	"${DIST_DIR}/${ZIP_NAME}" \
        	. \
        	-x ".git/*" \
       		-x ".github/*" \
        	-x "*.zip" \
        	-x "README.md"
	)

    msg "Created ${REPO_ROOT}/${DIST_DIR}/${ZIP_NAME}"
fi
