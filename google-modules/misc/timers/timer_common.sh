#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Provide common APIs, variables, settings in timer related tests

# Abort on errors, unbound variables, and pipefail
# (append '|| true' to the command if the nonzero exitstatus is expected)
set -o errexit -o nounset -o pipefail

# Default test settings
Dev=""
num_iterations=1000
pass_file=0 # whether to save files even for passed case
backup_ramdump_mode="" # backup ramdump mode
selected_tests=() # empty array to store selected tests
declare -A test_results # associative array to store pass/fail counts
total_fail_count=0

# Common variables
current_iteration=0
current_test="presetting"

# Print error message to stderr and optionally log to file.
# Globals: None
# Arguments:
#   $1: input message
#   $2: log file name (use "NO_LOG" to disable logging)
#   $3: append message to log file (use "APPEND" or "OVERWRITE")
# Outputs:
#   Error message to stderr
#   (Optional) Error message to specified log file
# Returns: None
err() {
  local input_message="$1"
  local logfile="$2"
  local append_to_file="$3"
  local output_message="[$(date +'%Y-%m-%dT%H:%M:%S%z')]: ${input_message}"

  if [[ ${logfile} == "NO_LOG" ]]; then
    echo "${output_message}" >&2
  elif [[ ${append_to_file} == "APPEND" ]]; then
    echo "${output_message}" >&2 > >(tee -a ${logfile})
  else
    echo "${output_message}" >&2 > >(tee ${logfile})
  fi
}

# Execute an ADB command, and optionally handle errors and return the output
# Globals: Dev, current_test, current_iteration
# Arguments:
#   $1: variable name to store output (use "NO_RET" to skip)
#   $2: check exit status (use "CHECK" to check exit status, or "NO_CHECK")
#   $3: save dmesg on error (use "DMESG" to save dmesg on error, or "NO_DMESG")
#   $4+: the adb command to execute
# Outputs:
#   (On error) Error messages and potentially dmesg to stderr and log files
#   (Optional) Output to a variable named by $1
# Returns:
#   0 if successful or check_status is not "CHECK"
#   1 if adb command fails and check_status is "CHECK"
execute_adb_command() {
  local ret_variable=$1
  local check_status="$2"
  local store_dmesg="$3"
  local output # assign value later for the exit status of adb command
  shift 3 # rest arguments are for adb command

  # Temporarily disable abort on nonzero exitstatus, and handle its error later
  set +o errexit
  if [[ $1 == "shell" ]]; then
    shift 1
    output="$(adb -s "${Dev}" shell "$*" 2>&1)" # rest as a single argument of shell
  else
    output="$(adb -s "${Dev}" "$@" 2>&1)" # all as arguments of adb
  fi
  local exit_status=$?
  set -o errexit

  if [[ ( ${check_status} == "CHECK" ) && ( ${exit_status} -ne 0 ) ]]; then
    local error_filename="${current_test}_error_${current_iteration}.txt"

    err "Error executing ADB command: $@" "output_${error_filename}" OVERWRITE
    err "Error output: ${output}" "output_${error_filename}" APPEND
    if [[ ${store_dmesg} == "DMESG" ]]; then
      adb -s ${Dev} shell dmesg > "dmesg_${error_filename}"
      err "Save dmesg to dmesg_${error_filename}" NO_LOG APPEND
    fi
    exit 1
  fi

  # If $ret_variable is a variable but not NO_RET, store output to it
  if [[ $ret_variable != "NO_RET" ]]; then
    local -n ret_val=$ret_variable
    ret_val="${output}"
  fi
}

# Execute a fastboot command, and optionally handle errors and return the output
# Globals: Dev, current_test, current_iteration
# Arguments:
#   $1: variable name to store output (use "NO_RET" to skip)
#   $2: check exit status (use "CHECK" to check exit status, or "NO_CHECK")
#   $3+: the fastboot command to execute
# Outputs:
#   (On error) Error messages to stderr and log files
#   (Optional) Output to a variable named by $1
# Returns:
#   0 if successful or check_status is not "CHECK"
#   1 if fastboot command fails and check_status is "CHECK"
execute_fastboot_command() {
  local ret_variable=$1
  local check_status="$2"
  local output # assign value later for the exit status of fastboot command
  shift 2 # rest arguments are for fastboot command

  # Temporarily disable abort on nonzero exitstatus, and handle its error later
  set +o errexit
  output="$(fastboot -s "${Dev}" "$@" 2>&1)" # all as arguments of fastboot
  local exit_status=$?
  set -o errexit

  if [[ ( ${check_status} == "CHECK" ) && ( ${exit_status} -ne 0 ) ]]; then
    local error_filename="${current_test}_error_${current_iteration}.txt"

    err "Error executing fastboot command: $@" "output_${error_filename}" OVERWRITE
    err "Error output: ${output}" "output_${error_filename}" APPEND
    exit 1
  fi

  # If $ret_variable is a variable but not NO_RET, store output to it
  if [[ $ret_variable != "NO_RET" ]]; then
    local -n ret_val=$ret_variable
    ret_val="${output}"
  fi
}

# Wait until boot complete
# Globals: None
# Arguments: None
# Outputs: None
# Returns: None
wait_boot_complete() {
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device
  execute_adb_command boot_completed CHECK NO_DMESG shell "getprop sys.boot_completed"
  while [[ -z ${boot_completed} ]] ; do
    sleep 1
    execute_adb_command boot_completed CHECK NO_DMESG shell "getprop sys.boot_completed"
  done
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device
}

# Update pass/fail counts for a test and optionally save output to files.
# Globals: test_results (modified), current_test, current_iteration, pass_file,
#          total_fail_count (modified)
# Arguments:
#   $1: result (0 for pass, non-zero for fail)
#   $2: file content to be saved to file, if applicable
# Outputs:
#   "test_name passed!" or "test_name failed!" to stdout
#   (Optional) file_content to *_pass_*.txt or *_fail_*.txt
# Returns: None
handle_result() {
  local result="$1"
  local file_content="$2"
  local result_counts=(${test_results[${current_test}]})
  local pass_count=${result_counts[0]}
  local fail_count=${result_counts[1]}

  if [[ ${result} -eq 0 ]]; then
    (( pass_count++ )) || true
    if [[ ${pass_file} -eq 1 ]]; then
      echo "${file_content}" > "${current_test}_pass_${current_iteration}.txt"
    fi
    echo "${current_test} passed!"
  else
    (( fail_count++ )) || true
    (( total_fail_count++ )) || true
    echo "${file_content}" > "${current_test}_fail_${current_iteration}.txt"
    echo "${current_test} failed!"
  fi
  test_results[$current_test]="$pass_count $fail_count"
}

# Parse common command-line options and updates script variables.
# Globals: BASH_SOURCE, num_iterations (modified), pass_file (modified),
#          Dev (modified)
# Arguments:
#   $1: common_shift_amount (name reference to a variable to store shift amount)
#   $2: customized_help_message (additional help message specific to the script)
# Outputs:
#   Print help message to stdout if -h or --help is encountered
#   Print error message to stderr for invalid options
#   Modify the 'common_shift_amount' variable based on parsed options
# Returns:
#   0 if -h or --help is encountered
#   1 if an invalid option is encountered
parse_common_option() {
  local -n common_shift_amount=$1
  local customized_help_message="$2"
  shift 2

  local help_message=$(
    echo "Usage: bash ${BASH_SOURCE[1]} [-n <num_iterations>] -t <test_name>"
    echo "Options:"
    echo "  -n, --number <num_iterations>  Number of iterations (default: ${num_iterations})"
    echo "  -p, --pass-file                Save log file even for passed case (default: ${pass_file})"
    echo "  -s <Device Serial>             Use device with given serial"
    echo "${customized_help_message}"
    echo "  -h, --help                     Display this help message"
  )

  case "$1" in
    -n | --number)
      num_iterations="$2"
      common_shift_amount=2
      ;;
    -p | --pass-file)
      pass_file=1
      common_shift_amount=1
      ;;
    -s)
      Dev="$2"
      common_shift_amount=2
      ;;
    -h | --help)
      echo "${help_message}"
      exit 0
      ;;
    *)
      err "Invalid option: $1" NO_LOG OVERWRITE
      echo "${help_message}"
      exit 1
      ;;
  esac
}

# Check if device serial is available
get_device_serial() {
  if [[ -z "${Dev}" ]]; then
    Dev="$(adb devices | awk 'NR>1 {print $1; exit}')"
    if [[ -z "${Dev}" ]]; then
      err "Error: Please provide a device serial" NO_LOG OVERWRITE
      exit 1
    fi
  fi
  echo "Device Serial: ${Dev}"
}

# Remove the duplicates in selected_tests while preserving order
unique_test_list() {
  # Use associative array to remove duplicates while preserving order
  declare -A unique_args
  for arg in "${selected_tests[@]}"; do
    unique_args[$arg]=1 # Store each unique argument as a key
  done

  # Extract the unique keys (original arguments) in the original order
  selected_tests=()
  for key in "${!unique_args[@]}"; do
    selected_tests+=("$key")
  done

  echo "Test: ${selected_tests[@]}"
  if [[ ${#selected_tests[@]} -eq 0 ]]; then
    err "No tests are specified" NO_LOG OVERWRITE
    exit 1
  fi
}

# Initialize result counts for each test in selected_tests
initialize_test_results() {
  for test_name in "${selected_tests[@]}"; do
    test_results[$test_name]="0 0" # "pass_count fail_count"
  done
}

# Initialize the script's context
# Globals: Dev (modified), selected_tests (modified), test_results (modified)
# Arguments: None
# Outputs: None
# Returns: None
setup_script_context() {
  get_device_serial
  unique_test_list
  initialize_test_results
}

# Set ramdump mode and backup original ramdump mode
# Globals: backup_ramdump_mode (modified)
# Arguments:
#   $1: the new ramdump mode to set
# Outputs: None
# Returns: None
set_ramdump_mode_anb_backup() {
  new_mode="$1"

  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device
  execute_adb_command NO_RET CHECK NO_DMESG reboot bootloader
  execute_fastboot_command backup_ramdump_mode CHECK oem ramdump
  backup_ramdump_mode="$(echo "${backup_ramdump_mode}" | grep "(bootloader) ramdump" | head -1)"
  case "${backup_ramdump_mode}" in
    "(bootloader) ramdump enabled (USB)" ) backup_ramdump_mode="usb" ;;
    "(bootloader) ramdump enabled" ) backup_ramdump_mode="enable" ;;
    "(bootloader) ramdump disabled" ) backup_ramdump_mode="disable" ;;
    * )
      err "Unexpected ramdump mode: ${backup_ramdump_mode}" NO_LOG OVERWRITE
      exit 1
      ;;
  esac
  execute_fastboot_command NO_RET CHECK oem ramdump "${new_mode}"
  execute_fastboot_command NO_RET CHECK reboot
}

# Print results to the console and a file, then exit with the total fail count
# Globals: test_results, total_fail_count
# Arguments: None
# Outputs:
#   Test results summary to stdout and a file
# Returns:
#   Exit status equal to the value of the 'total_fail_count' variable
print_result_and_exit() {
  {
    echo "=================== Test Results ===================="
    for test_name in "${!test_results[@]}"; do
      counts=(${test_results[$test_name]})
      echo "$test_name: Passed ${counts[0]} times, Failed ${counts[1]} times"
    done
  } | tee "test_result_$(date +'%Y-%m-%dT%H:%M:%S%z').txt"
  exit ${total_fail_count}
}
