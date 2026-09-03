#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only

set -e

DEVICE=
DIST_DIR=
FILTER=
GCOV=
GCOV_DIR=

function fail() {
  echo "$@" >&2
  exit 1
}

function usage() {
  cat <<EOF
usage: $0 [options] <DEVICE>

Options:
  -s SERIAL             Use device with given serial.
  -f FILTER             Filter KUnit test suites (glob pattern). e.g. "apc_irm_test*" or
                        "apc_irm_test.my_test_case"
  --dist_dir DIR        Directory of dist files. Default: out/<DEVICE>/dist
  --gcov                Generate code coverage report with gcov.
  --gcov_dir DIR        Directory for gcov outputs. Default: out/<DEVICE>/gcov
  --help, -h            Show this help.
EOF
}

function parse_args() {
  while (( $# > 0 )); do
    case "$1" in
      -s)
        export ANDROID_SERIAL="$2"
        shift
        ;;
      -f|--filter)
        FILTER="$2"
        shift
        ;;
      --dist_dir)
        DIST_DIR="$2"
        shift
        ;;
      --gcov)
        GCOV=1
        ;;
      --gcov_dir)
        GCOV_DIR="$2"
        shift
        ;;
      -h|--help)
        usage
        exit
        ;;
      -*)
        echo "Unknown option: $1" >&2
        usage >&2
        exit 1
        ;;
      *)
        DEVICE="$1"
        ;;
    esac
    shift
  done

  if [[ -z "${DEVICE}" ]]; then
    echo "DEVICE is required." >&2
    usage >&2
    exit 1
  fi

  if [[ -z "${DIST_DIR}" ]]; then
    DIST_DIR="out/${DEVICE}/dist"
  fi

  if [[ -z "${GCOV_DIR}" ]]; then
    GCOV_DIR="out/${DEVICE}/gcov"
  fi
}

function main() {
  parse_args "$@"

  local kunit_tests_zip="${DIST_DIR}/kunit_tests.zip"
  if [[ ! -f "${kunit_tests_zip}" ]]; then
    fail "${kunit_tests_zip} not found! Please build ${DEVICE} first."
  fi

  if [[ -n "${GCOV}" ]]; then
    if ! which lcov >/dev/null; then
      fail "Please install lcov first."
    fi
    local kernel_modules_gcno_tar="${DIST_DIR}/kernel_modules_install.gcno.tar.gz"
    if [[ ! -f "${kernel_modules_gcno_tar}" ]]; then
      fail "${kernel_modules_gcno_tar} not found!" \
        "Please build ${DEVICE} with --config=testing first."
    fi
    local build_config_constants="common/build.config.constants"
    if [[ ! -f "${build_config_constants}" ]]; then
      fail "${build_config_constants} not found! Please run this script at repo root."
    fi
    local clang_version
    clang_version="$(sed -n 's/CLANG_VERSION=\(.*\)/\1/p' "${build_config_constants}")"
    local llvm_cov="prebuilts/clang/host/linux-x86/clang-${clang_version}/bin/llvm-cov"

    rm -rf "${GCOV_DIR}"
    mkdir -p "${GCOV_DIR}"
    echo "exec \"$(realpath "${llvm_cov}")\" gcov \"\$@\"" > "${GCOV_DIR}/gcov.sh"
    chmod +x "${GCOV_DIR}/gcov.sh"
    tar -xzof "${kernel_modules_gcno_tar}" -C "${GCOV_DIR}"
  fi

  adb root
  adb shell rm -rf /data/local/tmp/kunit
  adb shell mkdir -p /data/local/tmp/kunit
  adb push "${DIST_DIR}/kunit_tests.zip" /data/local/tmp/kunit
  adb shell unzip -q /data/local/tmp/kunit/kunit_tests.zip -d /data/local/tmp/kunit

  local run_cmd="sh /data/local/tmp/kunit/run_kunit_tests.sh"
  if [[ -n "${FILTER}" ]]; then
    run_cmd+=" '${FILTER}'"
  fi

  if ! adb shell "${run_cmd}"; then
    local test_failed=1
  fi

  if [[ -n "${GCOV}" ]]; then
    if ! adb shell test -f /data/local/tmp/kunit/gcda.tar.gz; then
      fail "/data/local/tmp/kunit/gcda.tar.gz not found on the device!" \
        "Please use images built with --config=testing."
    fi
    adb pull /data/local/tmp/kunit/gcda.tar.gz "${GCOV_DIR}"
    tar -xzof "${GCOV_DIR}/gcda.tar.gz" -C "${GCOV_DIR}"
    echo "Generating code coverage report at ${GCOV_DIR}"
    lcov \
      --directory "${GCOV_DIR}/private" \
      --base-directory aosp \
      --gcov-tool "${GCOV_DIR}/gcov.sh" \
      --capture -o "${GCOV_DIR}/private.info" \
      > "${GCOV_DIR}/lcov.stdout.txt" \
      2>"${GCOV_DIR}/lcov.stderr.txt"
    genhtml \
      "${GCOV_DIR}/private.info" \
      --ignore-errors inconsistent \
      --output-directory "${GCOV_DIR}/html" \
      --title "Pixel kunit code coverage" \
      > "${GCOV_DIR}/genhtml.stdout.txt" \
      2>"${GCOV_DIR}/genhtml.stderr.txt"
  fi

  if [[ -n "${test_failed}" ]]; then
    echo "FAILED"
    exit 1
  fi
  echo "PASSED"
}

main "$@"
