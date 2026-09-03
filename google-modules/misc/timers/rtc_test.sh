#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Perform Real Time Clock tests

# Global configuration
readonly GREP_ISR_CMD="grep da9188-rtc /proc/interrupts"
readonly RTC0_NODE="/sys/devices/platform/da9188mfd/da9188-rtc/rtc/rtc0/"
readonly SCRIPT_DIR="$(dirname "$(realpath "$0")")"
readonly TEST_LIST=("rtc_read" "rtc_write" "rtc_wake")

# Default test settings
rtc_read_sleep_time=10
rtc_wake_sleep_time=5

source ${SCRIPT_DIR}/timer_common.sh

test_rtc_read() {
  # Record start timestamp
  execute_adb_command rtc_read_start CHECK NO_DMESG shell "cat ${RTC0_NODE}/since_epoch"
  execute_adb_command sys_read_start CHECK NO_DMESG shell "date '+%s'"

  sleep ${rtc_read_sleep_time}

  # Record end timestamp
  execute_adb_command rtc_read_end CHECK NO_DMESG shell "cat ${RTC0_NODE}/since_epoch"
  execute_adb_command sys_read_end CHECK NO_DMESG shell "date '+%s'"

  local rtc_difference="$(( rtc_read_end - rtc_read_start ))"
  local sys_difference="$(( sys_read_end - sys_read_start ))"
  local difference="$(( sys_difference - rtc_difference ))"
  local file_content=$(
    echo "RTC start since epoch : ${rtc_read_start}" &&
    echo "RTC end since epoch   : ${rtc_read_end}" &&
    echo "RTC Period            : ${rtc_difference}" &&
    echo "Sys start since epoch : ${sys_read_start}" &&
    echo "Sys end since epoch   : ${sys_read_end}" &&
    echo "Sys Period            : ${sys_difference}" &&
    echo "period difference: ${difference}"
  )

  local result=-1
  if [[ ${difference} -le 1 ]] && [[ ${difference} -ge -1 ]]; then
    result=0
  fi
  handle_result "${result}" "${file_content}"
}

test_rtc_write() {
  # Write RTC as system time
  execute_adb_command systohc_rtc CHECK NO_DMESG shell "hwclock -w && cat ${RTC0_NODE}/since_epoch"
  execute_adb_command systohc_sys CHECK NO_DMESG shell "date '+%s'"
  local systohc_difference="$(( systohc_sys - systohc_rtc ))"

  # Write RTC to system time
  execute_adb_command hctosys_rtc CHECK NO_DMESG shell "hwclock -s && cat ${RTC0_NODE}/since_epoch"
  execute_adb_command hctosys_sys CHECK NO_DMESG shell "date '+%s'"
  local hctosys_difference="$(( hctosys_sys - hctosys_rtc ))"

  local file_content=$(
    echo "After writing RTC as system time (hwclock -w)" &&
    echo "RTC since epoch : ${systohc_rtc}" &&
    echo "Sys since epoch : ${systohc_sys}" &&
    echo "Difference      : ${systohc_difference}" &&
    echo "After writing RTC to system time (hwclock -s)" &&
    echo "RTC since epoch : ${hctosys_rtc}" &&
    echo "Sys since epoch : ${hctosys_sys}" &&
    echo "Difference      : ${hctosys_difference}"
  )

  local result=-1
  if [[ ${systohc_difference} -le 1 ]] && [[ ${hctosys_difference} -ge -1 ]] &&
     [[ ${hctosys_difference} -le 1 ]] && [[ ${systohc_difference} -ge -1 ]]; then
    result=0
  fi
  handle_result "${result}" "${file_content}"
}

test_rtc_wake() {
  # Record number of rtc interrupts before wake alarm
  execute_adb_command rtc_isr_start NO_CHECK NO_DMESG shell "${GREP_ISR_CMD}"
  execute_adb_command start_time CHECK NO_DMESG shell "cat ${RTC0_NODE}/since_epoch"

  # Disable the old alarm before set new alarm
  execute_adb_command rtcwake_output CHECK NO_DMESG \
    shell "rtcwake -m disable && rtcwake -m no -s ${rtc_wake_sleep_time} -v"
  sleep $(( rtc_wake_sleep_time + 2 )) # wait for wake alarm

  # Record number of rtc interrupts after wake alarm
  execute_adb_command rtc_isr_end NO_CHECK NO_DMESG shell "${GREP_ISR_CMD}"
  execute_adb_command end_time CHECK NO_DMESG shell "cat ${RTC0_NODE}/since_epoch"

  local file_content=$(
    echo -e "Start state from ${GREP_ISR_CMD}:\n${rtc_isr_start}" &&
    echo -e "End state from ${GREP_ISR_CMD}:\n${rtc_isr_end}" &&
    echo "Start time: ${start_time}" &&
    echo "Output of wakealarm: ${rtcwake_output}" &&
    echo "End time: ${end_time}"
  )

  local result=-1
  if [[ "${rtc_isr_start}" != "${rtc_isr_end}" ]]; then
    result=0
  fi
  handle_result "${result}" "${file_content}"
}

main() {
  local customized_help_message=$(
    echo "  -t, --test <test_name>         Tests to run:"
    echo "                                  rtc_read , compare the passing time of system time with the one of RTC timestamp"
    echo "                                  rtc_write , compare the system time with RTC timestamp after sync between each other"
    echo "                                  rtc_wake , compare the number of rtc interrupts before and after rtc wake alarm"
    echo "                                  all (runs all tests)"
    echo "  --rtc-read-sleep <second>      Specify how long to sleep during rtc_read test (default: ${rtc_read_sleep_time})"
    echo "  --rtc-wake-sleep <second>      Specify how long to sleep during rtc_wake test (default: ${rtc_wake_sleep_time})"
  )

  # Parse command-line options
  while [[ $# -gt 0 ]]; do
    local shift_amount=0
    case "$1" in
      --rtc-read-sleep) rtc_read_sleep_time=$2 ; shift 2 ;;
      --rtc-wake-sleep) rtc_wake_sleep_time=$2 ; shift 2 ;;
      -t | --test)
        case "$2" in
          rtc_read | rtc_write | rtc_wake) selected_tests+=("$2") ;;
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

  for (( current_iteration=1; current_iteration<=num_iterations; current_iteration++ )); do
    echo "Iteration ${current_iteration}"

    for test_name in "${selected_tests[@]}"; do
      current_test="${test_name}"
      case "${test_name}" in
        rtc_read) test_rtc_read ;;
        rtc_write) test_rtc_write ;;
        rtc_wake) test_rtc_wake ;;
        *)
          err "Wrong test_name: ${test_name}" NO_LOG OVERWRITE
          exit 1
          ;;
      esac
    done
  done

  print_result_and_exit
}

main "$@"
