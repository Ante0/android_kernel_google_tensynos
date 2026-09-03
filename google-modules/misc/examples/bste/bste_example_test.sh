# SPDX-License-Identifier: GPL-2.0-only

# BSTE Example Test Suite

suite_init() {
  echo "Initializing BSTE example suite..."

  # Try to load the module if not already loaded
  if ! lsmod | grep -q "bste_example_module"; then
    echo "Attempting to load bste_example_module..."
    modprobe bste_example_module
  fi

  # Verify device node exists
  if [ ! -c "/dev/bste_example" ]; then
    echo "Error: /dev/bste_example does not exist."
    return 1
  fi

  return 0
}

test_suspend() {
  echo "Running suspend test..."

  # Suspend to RAM for 5 seconds
  rtcwake -m mem -s 5
  local ret=$?
  assert_eq 0 "${ret}" "rtcwake failed with exit code ${ret}"

  echo "Suspend verified successfully."
  pass
}

test_ioctl() {
  echo "Running ioctl test..."

  # Execute the helper binary
  ./ioctl_helper
  local ret=$?

  assert_eq 0 "${ret}" "ioctl_helper failed with exit code ${ret}"
  pass
}

suite_exit() {
  echo "Cleaning up BSTE example suite..."
  # Optionally unload the module
  # modprobe -r bste_example_module
  return 0
}
