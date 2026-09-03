#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Perform SubSystem Timer tests.

# Global configuration
readonly CLK_EVT_DEBUG_NODE="/sys/kernel/debug/200c1000.sst/200c1000.sst.clk_evt.0/debug"
readonly GTC_DEBUG_NODE="/sys/kernel/debug/200c1000.sst/gtc_debug"
readonly SCRIPT_DIR="$(dirname "$(realpath "$0")")"
readonly TEST_LIST=("gtc_debug" "sst_clk_event_debug" "gtc_and_clock_boottime")

# Default test settings
gtc_debug_sleep_time=30
sst_evt_debug_sleep_time=5
sst_out_of_bound_check=0 # whether to check SST out of bound pattern

source ${SCRIPT_DIR}/timer_common.sh

test_gtc_debug() {
  # Clear dmesg (optional, start fresh)
  execute_adb_command NO_RET CHECK DMESG shell "dmesg -c"

  # Reset test counters, and trigger test twice for start and end respectively
  execute_adb_command NO_RET CHECK DMESG shell "echo 0 > ${GTC_DEBUG_NODE}"
  execute_adb_command NO_RET CHECK DMESG shell "echo 1 > ${GTC_DEBUG_NODE}"
  sleep $gtc_debug_sleep_time
  execute_adb_command NO_RET CHECK DMESG shell "echo 1 > ${GTC_DEBUG_NODE}"

  # Check whether debug test passed patterns are in dmesg
  execute_adb_command dmesg_output CHECK DMESG shell dmesg
  local file_content="$(echo -e "dmesg:\n${dmesg_output}")"

  local result=-1
  if [[ $dmesg_output == *"google-sst 200c1000.sst: Clean last timestamp!"* ]] && \
     [[ $dmesg_output == *"google-sst 200c1000.sst: Initialize time stamp!"* ]] && \
     [[ $dmesg_output == *"google-sst 200c1000.sst: Test pass!"* ]]; then
    result=0
  fi
  handle_result "${result}" "${file_content}"
}

test_sst_clk_event_debug() {
  # Clear dmesg (optional, start fresh)
  execute_adb_command NO_RET CHECK DMESG shell "dmesg -c"

  # Write first 1 to enable counting sst isr, second 1 to get counter value,
  # then write 0 to disable counting.
  execute_adb_command NO_RET CHECK DMESG shell "echo 1 > ${CLK_EVT_DEBUG_NODE}"
  execute_adb_command NO_RET CHECK DMESG shell "sleep ${sst_evt_debug_sleep_time}"
  execute_adb_command NO_RET CHECK DMESG shell "echo 1 > ${CLK_EVT_DEBUG_NODE}"
  execute_adb_command NO_RET CHECK DMESG shell "echo 0 > ${CLK_EVT_DEBUG_NODE}"

  # Count pattern occurrences and check if the first counter is nonzero in dmesg
  execute_adb_command dmesg_output CHECK DMESG shell dmesg
  local pattern_count="$(echo "${dmesg_output}" | grep -c "google-sst 200c1000.sst: Test count: " || true)"
  local second_occurrence_value="$(echo "${dmesg_output}" | grep -oP "google-sst 200c1000.sst: Test count: \K\d+" | head -1 || true)"
  local out_of_bound_count="$(echo "${dmesg_output}" | grep -c "SST Test out of bound!" || true)"
  local ignore_out_of_bound=""
  if [[ ${sst_out_of_bound_check} -eq 0 ]]; then
    ignore_out_of_bound="(ignored)"
  fi
  local file_content=$(
    echo "count of \"SST Test out of bound!\": ${out_of_bound_count} ${ignore_out_of_bound}"
    echo "count of \"google-sst 200c1000.sst: Test count: \": ${pattern_count}"
    echo "last Test count: ${second_occurrence_value}"
    echo -e "dmesg:\n${dmesg_output}"
  )

  local result=-1
  if [[ ( ${out_of_bound_count} -eq 0 || ${sst_out_of_bound_check} -eq 0 ) &&
    ${pattern_count} -ge 2 && ${second_occurrence_value} -gt 0 ]]; then
    result=0
  fi
  handle_result "${result}" "${file_content}"
}

test_gtc_and_clock_boottime() {
  # Get GTC and clock boottime
  execute_adb_command boottime_output CHECK NO_DMESG \
    shell "cat /sys/devices/platform/200c1000.sst/gtc_and_clock_boottime"

  local result=-1
  # Check for two numbers separated by a space
  if [[ ${boottime_output} =~ ^[0-9]+[[:space:]]+[0-9]+$ ]]; then
    result=0
  fi
  handle_result "${result}" "${boottime_output}"
}

main() {
  local customized_help_message=$(
    echo "  -t, --test <test_name>         Tests to run:"
    echo "                                  gtc_debug , comapre GTC tick rate with kernel tick rate"
    echo "                                  sst_clk_event_debug , check SST isr count"
    echo "                                  gtc_and_clock_boottime , check GTC timestamp and clock boottime"
    echo "                                  all (runs all tests)"
    echo "  --gtc-debug-sleep <second>     Specify how long to sleep during gtc_debug test (default: ${gtc_debug_sleep_time})"
    echo "  --sst-evt-debug-sleep <second> Specify how long to sleep during sst_clk_event_debug test (default: ${sst_evt_debug_sleep_time})"
    echo "  --sst-out-of-bound-check       Enable \"SST out of bound\" check in sst_clk_event_debug test"
  )

  # Parse command-line options
  while [[ $# -gt 0 ]]; do
    local shift_amount=0
    case "$1" in
      --sst-out-of-bound-check) sst_out_of_bound_check=1 ; shift 1 ;;
      --gtc-debug-sleep) gtc_debug_sleep_time=$2 ; shift 2 ;;
      --sst-evt-debug-sleep) sst_evt_debug_sleep_time=$2 ; shift 2 ;;
      -t | --test)
        case "$2" in
          gtc_debug | sst_clk_event_debug | gtc_and_clock_boottime)
            selected_tests+=("$2") ;;
          all) selected_tests=("${TEST_LIST[@]}") ;;
          *) err "Invalid test name: $2" NO_LOG OVERWRITE ; exit 1 ;;
        esac
        shift 2
        ;;
      *)
        parse_common_option shift_amount "${customized_help_message}" $@
        shift "${shift_amount}"
        ;;
    esac
  done

  setup_script_context

  # Presetting
  wait_boot_complete
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device root
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device
  execute_adb_command NO_RET NO_CHECK NO_DMESG shell "mount -t debugfs none /d"
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device

  for (( current_iteration=1; current_iteration<=num_iterations; current_iteration++ )); do
    echo "Iteration ${current_iteration}"

    for test_name in "${selected_tests[@]}"; do
      current_test="${test_name}"
      case "${test_name}" in
        gtc_debug) test_gtc_debug ;;
        sst_clk_event_debug) test_sst_clk_event_debug ;;
        gtc_and_clock_boottime) test_gtc_and_clock_boottime ;;
        *)
          err "Wrong test_name: $test_name" NO_LOG OVERWRITE
          exit 1
          ;;
      esac
    done
  done

  print_result_and_exit
}

main "$@"
