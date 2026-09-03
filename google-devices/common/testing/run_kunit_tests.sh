#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# KUNIT test script intends to be used by local tests and PTS tests.
# run_kunit_test.sh packed with module test.ko into kunit_tests.zip.
# This file needs to be pushed to device and run from shell.
# Example:
#   adb root
#   adb push kunit_tests.zip /data/local/tmp/test_folder
#   adb shell unzip /data/local/tmp/test_folder/kunit_tests.zip -d /data/local/tmp/test_folder
#   # The [filter] parameter is optional and allows you to run specific suites or tests.
#   # For example: "apc_irm_test*" or "apc_irm_test.my_test_case" (quotes are necessary).
#   adb shell sh /data/local/tmp/test_folder/run_kunit_tests.sh [filter]

KUNIT_TEST_DIR="$(dirname "$(readlink -f "$0")")"
KUNIT_KO="kunit.ko"
KUNIT_MODULES_DIR="${KUNIT_TEST_DIR}/kunit_test_modules"

function log() {
    echo "[KernelKunitTest]" "$@"
}

function umount_debugfs() {
    if [[ "$1" == true ]]; then
        umount /sys/kernel/debug
    fi
}

function test_kernel_kunit() {
    local filter="$1"
    local mount_debugfs=false
    local all_modules_result=true

    if ! mount | grep -q debugfs; then
        mount -t debugfs debugfs /sys/kernel/debug
        echo "mount debugfs"
        mount_debugfs=true
    fi

    local kunit_params="enable=1"
    if [[ -n "${filter}" ]]; then
        kunit_params+=" filter_glob=${filter}"
    fi

    if ! insmod "${KUNIT_MODULES_DIR}/${KUNIT_KO}" ${kunit_params}; then
        log "Failed to load ${KUNIT_KO}."
        umount_debugfs "${mount_debugfs}"
        return 1
    fi

    if [[ -d "/sys/kernel/debug/gcov" ]]; then
        echo 1 > "/sys/kernel/debug/gcov/reset"
    fi

    for file in "${KUNIT_MODULES_DIR}"/*.ko; do
        module="$(basename "${file}")"
        if [[ "${module}" == "${KUNIT_KO}" ]]; then
            continue
        fi
        if ! test_module "${module}"; then
            all_modules_result=false
        fi
    done

    if [[ -d "/sys/kernel/debug/gcov" ]]; then
        local gcda_dir="$(find /sys/kernel/debug/gcov -path '/*/out/*' -prune)"
        local gcda_staging_dir="${KUNIT_TEST_DIR}/gcda"
        mkdir -p "${gcda_staging_dir}"
        # We need to copy gcda files first, directly tar debugfs gcda files doesn't work.
        cp -a "${gcda_dir}"/* "${gcda_staging_dir}"
        tar -czf "${KUNIT_TEST_DIR}/gcda.tar.gz" -C "${gcda_staging_dir}" . --exclude="*.gcno"
    fi

    rmmod "kunit"
    umount_debugfs "${mount_debugfs}"

    if [[ "${all_modules_result}" == false ]]; then
        log "testKernelKunit failed"
        return 1
    fi
    return 0
}

function test_module() {
    local module="$1"
    local module_path="${KUNIT_MODULES_DIR}/${module}"
    local module_name="$(modinfo -F name "${module_path}")"

    if ! insmod "${module_path}"; then
        log "Failed to load module ${module}."
        return 1
    fi

    local results
    if ! results="$(cat /sys/kernel/debug/kunit/*/results 2>/dev/null)"; then
        rmmod "${module_name}"
        log "Test result for module ${module}: No tests executed (skipped by filter?)"
        return 0
    fi

    log "KUnit Test Result Details:"
    echo "${results}"
    rmmod "${module_name}"

    if grep -q 'not ok' <<< "${results}"; then
        log "Test result for module ${module}: Failed"
        return 1
    fi
    log "Test result for module ${module}: Passed"
    return 0
}

# Main Execution
test_kernel_kunit "$1"
