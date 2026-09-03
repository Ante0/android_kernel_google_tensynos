#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Perform Watchdog Timer tests

# Global configuration
readonly PIXEL_DEBUG_TRIGGER="/sys/kernel/pixel_debug/trigger"
readonly RST_STAT_WDT_PATTERN="RST_STAT: 0x2 - Fatal Error HW"
readonly SCRIPT_DIR="$(dirname "$(realpath "$0")")"
readonly TEST_LIST=("wdt_trigger")

# Default test settings
wdt_trigger_sleep_time=40

source ${SCRIPT_DIR}/timer_common.sh

test_wdt_trigger() {
  # Get root permission for accessing pixel_debug node
  wait_boot_complete
  execute_adb_command root_output NO_CHECK NO_DMESG wait-for-device root
  while [[ ${root_output} == *"unable to connect for root"* ]] ; do
    sleep 1
    execute_adb_command root_output NO_CHECK NO_DMESG wait-for-device root
  done
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device root
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device

  # Trigger hardlockup via pixel_debug node
  execute_adb_command NO_RET CHECK DMESG shell "echo hardlockup > ${PIXEL_DEBUG_TRIGGER}"
  sleep ${wdt_trigger_sleep_time}

  # Expect the device is adb-disconnected. Then get the abl_log
  execute_adb_command adb_device_list CHECK DMESG devices
  local abl_log=""
  local dmesg=""
  if [[ ${adb_device_list} != *${Dev}* ]] ; then
    # Get ABL log after entering ramdump usb mode, and reboot
    execute_fastboot_command abl_log CHECK oem dmesg
    execute_fastboot_command NO_RET CHECK reboot
  else
    # Otherwise, watchdog reset doesn't happen, store dmesg for debugging, and reboot
    execute_adb_command dmesg CHECK NO_DMESG shell dmesg
    execute_adb_command NO_RET CHECK NO_DMESG reboot
  fi

  # Check if watchdog reset pattern exists in abl log
  local result=-1
  local rst_stat_wdt="Not Found"
  local file_content=""
  if [[ ! -z ${abl_log} ]] ; then
    if [[ ${abl_log} == *${RST_STAT_WDT_PATTERN}* ]] ; then
      result=0
      rst_stat_wdt="Found"
    fi
    file_content=$(
      echo "Whether find pattern ${RST_STAT_WDT_PATTERN} : ${rst_stat_wdt}" &&
      echo -e "ABL log :\n${abl_log}"
    )
  else
    file_content=$(
      echo "Whether find pattern ${RST_STAT_WDT_PATTERN} : ${rst_stat_wdt}" &&
      echo -e "dmesg :\n${dmesg}"
    )
  fi

  handle_result "${result}" "${file_content}"
}

main() {
  local customized_help_message=$(
    echo "  -t, --test <test_name>         Tests to run:"
    echo "                                  wdt_trigger , simulate watchdog reset and check if watchdog is triggered"
    echo "                                  all (runs all tests)"
    echo "  --wdt-trigger-sleep <second>      Specify how long to sleep during wdt_trigger test (default: ${wdt_trigger_sleep_time})"
  )

  # Parse command-line options
  while [[ $# -gt 0 ]]; do
    local shift_amount=0
    case "$1" in
      --wdt-trigger-sleep) wdt_trigger_sleep_time=$2 ; shift 2 ;;
      -t | --test)
        case "$2" in
          wdt_trigger) selected_tests+=("$2") ;;
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
  # Backup ramdump mode, and use ramdump usb mode
  echo "Presetting for ramdump usb mode..."
  set_ramdump_mode_anb_backup usb
  echo "Set to ramdump usb mode, backup original mode: ${backup_ramdump_mode}. Reboot..."

  wait_boot_complete
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device root
  execute_adb_command NO_RET CHECK NO_DMESG wait-for-device

  for (( current_iteration=1; current_iteration<=num_iterations; current_iteration++ )); do
    echo "Iteration ${current_iteration}"

    for test_name in "${selected_tests[@]}"; do
      current_test="${test_name}"
      case "${test_name}" in
        wdt_trigger) test_wdt_trigger ;;
        *)
          err "Wrong test_name: ${test_name}" NO_LOG OVERWRITE
          exit 1
          ;;
      esac
    done
  done

  # Recover the ramdump mode
  echo "Test complete. Recover to original ramdump mode..."
  set_ramdump_mode_anb_backup ${backup_ramdump_mode}
  print_result_and_exit
}

main "$@"
