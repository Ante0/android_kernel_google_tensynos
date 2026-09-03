#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Perform Architecture Timer tests

# Global configuration
readonly GREP_ISR_CMD="grep arch_timer /proc/interrupts"
readonly SCRIPT_DIR="$(dirname "$(realpath "$0")")"
readonly TEST_LIST=("arch_timer_read" "arch_timer_isr")

# Default test settings
arch_timer_read_sleep_time=30
arch_timer_isr_sleep_time=5

source ${SCRIPT_DIR}/timer_common.sh

# Extract isr number from `cat proc/interrupts` string result
# $1 : one lint string result of `cat proc/interrupts`
# $2 : number of cpus
# $3 : result array
extract_isr_number() {
  local isr_string="$1"
  local num_cpus="$2"
  local -n isr_array=$3

  # Extract values using parameter expansion and array slicing
  isr_array=(${isr_string// / })
  isr_array=("${isr_array[@]:1:num_cpus}")
}

test_arch_timer_read() {
  # Record start timestamp
  execute_adb_command sys_read_start CHECK NO_DMESG shell "date '+%s'"

  sleep ${arch_timer_read_sleep_time}

  # Record end timestamp
  execute_adb_command sys_read_end CHECK NO_DMESG shell "date '+%s'"

  local sys_difference="$(( sys_read_end - sys_read_start ))"
  local difference="$(( sys_difference - arch_timer_read_sleep_time ))"
  local file_content=$(
    echo "Sys start since epoch : ${sys_read_start}" &&
    echo "Sys end since epoch   : ${sys_read_end}" &&
    echo "Sys Period            : ${sys_difference}" &&
    echo "Expected Period       : ${arch_timer_read_sleep_time}" &&
    echo "period difference: ${difference}"
  )

  local result=-1
  if [[ ${difference} -le 1 ]] && [[ ${difference} -ge -1 ]]; then
    result=0
  fi
  handle_result "${result}" "${file_content}"
}

test_arch_timer_isr() {
  # Record number of arch timer interrupts in the beginning
  execute_adb_command arch_timer_isr_start NO_CHECK NO_DMESG shell "${GREP_ISR_CMD}"

  execute_adb_command num_cpus CHECK NO_DMESG shell "nproc"

  for ((i=0; i<num_cpus; i++)); do
    local CPU_BITMASK=$((1 << i))
    local BUSY_LOOP_COMMAND="nohup taskset -a $CPU_BITMASK yes > /dev/null 2>&1 &"
    execute_adb_command NO_RET CHECK NO_DMESG shell "${BUSY_LOOP_COMMAND}"
  done

  sleep ${arch_timer_isr_sleep_time} # wait for a while to have arch timer interrupts

  execute_adb_command NO_RET CHECK NO_DMESG shell "pkill -f '^yes$' > /dev/null 2>&1"

  # Record number of arch timer interrupts in the end
  execute_adb_command arch_timer_isr_end NO_CHECK NO_DMESG shell "${GREP_ISR_CMD}"

  extract_isr_number "${arch_timer_isr_start}" "${num_cpus}" arch_timer_isr_array_start
  extract_isr_number "${arch_timer_isr_end}" "${num_cpus}" arch_timer_isr_array_end

  # Check if the number of arch timer interrupts changes on each core
  local file_content=$(
    echo -e "Start state from ${GREP_ISR_CMD}:\n${arch_timer_isr_start}" &&
    echo -e "End state from ${GREP_ISR_CMD}:\n${arch_timer_isr_end}"
  )

  local result=0
  for ((i=0; i<num_cpus; i++)); do
    file_content+="Core $i : ${arch_timer_isr_array_start[i]}\t${arch_timer_isr_array_end[i]}"

    if [[ ${arch_timer_isr_array_start[i]} -ge ${arch_timer_isr_array_end[i]} ]]; then
      file_content+=" (not increasing)"
      result=$((result-1))
    fi

    file_content+="\n"
  done
  file_content=$(echo -e "${file_content}")

  handle_result "${result}" "${file_content}"
}

main() {
  local customized_help_message=$(
    echo "  -t, --test <test_name>         Tests to run:"
    echo "                                  arch_timer_read , compare the passing time of system time with host time"
    echo "                                  arch_timer_isr , compare the number of arch timer interrupts before and after a period"
    echo "                                  all (runs all tests)"
    echo "  --arch-timer-read-sleep <second>      Specify how long to sleep during arch_timer_read test (default: ${arch_timer_read_sleep_time})"
    echo "  --arch-timer-isr-sleep <second>      Specify how long to sleep during arch_timer_isr test (default: ${arch_timer_isr_sleep_time})"
  )

  # Parse command-line options
  while [[ $# -gt 0 ]]; do
    local shift_amount=0
    case "$1" in
      --arch-read-sleep) arch_timer_read_sleep_time=$2 ; shift 2 ;;
      --arch-isr-sleep) arch_timer_isr_sleep_time=$2 ; shift 2 ;;
      -t | --test)
        case "$2" in
          arch_timer_read | arch_timer_isr) selected_tests+=("$2") ;;
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
        arch_timer_read) test_arch_timer_read ;;
        arch_timer_isr) test_arch_timer_isr ;;
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
