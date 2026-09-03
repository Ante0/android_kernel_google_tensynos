# BSTE Example with ddk_module

This directory contains an example of how to implement, register, and run a BSTE (Base System
Test Environment) test suite for a kernel module using Kleaf.

It demonstrates a complete integration testing flow, including:
1.  A kernel module that creates a device node (`/dev/bste_example`).
2.  A userspace helper binary (`ioctl_helper`) to test `ioctl` interfaces.
3.  A BSTE test suite with two test cases:
    *   A system suspend-to-RAM test using `rtcwake`.
    *   An IOCTL functional test using the userspace helper.

## Directory Structure

- `BUILD.bazel`: Defines the Bazel targets for the module, the helper binary, and the BSTE test
  suite.
- `bste_example_module.c`: Source code for the kernel module under test, registering a misc
  device.
- `ioctl_helper.c`: Source code for the userspace helper binary that interacts with the driver
  via `ioctl`.
- `bste_example_test.sh`: The BSTE test script containing the test cases and lifecycle hooks.

## Key Concepts

### Userspace Testing of Kernel Interfaces
Unlike KUnit (which runs entirely in kernel space), BSTE is designed for **user-space testing**
of the kernel. To test interfaces like `ioctl` that cannot be easily triggered from a shell
script, we compile a small C helper program (`ioctl_helper`) using `bste_cc_binary`. This binary
is packaged with the test suite and executed by the shell script.

### Surviving ADB Disconnection (Autonomy)
A key feature of BSTE is its ability to run tests autonomously on the device, surviving ADB
disconnections. The suspend test case (`test_suspend`) directly demonstrates this: when the
device suspends, the ADB/USB connection is temporarily lost. The BSTE background runner continues
executing the test on-device, and the host can safely re-attach to the session once the device
wakes up.

### BSTE Best Practices
- **Lifecycle Hooks**: Uses `suite_init` to automatically load the required kernel module using
  `modprobe` and verify the device node.
- **Assertions**: Uses BSTE's native `assert_true` and `assert_eq` helpers for clean, readable
  validation and standardized failure reporting.
- **Hermeticity**: Uses relative paths (`./ioctl_helper`) to execute packaged binaries.

## Building and Running

### 1. Register with a Device
To run these tests on a device, the test suite must be registered in the device's
`device_build` target.

Example (`private/devices/google/<device>/BUILD.bazel`):
```python
device_build(
    name = "<device>",
    vendor_dlkm_modules = [
        "//private/google-modules/misc/examples/bste:bste_example_module",
    ],
    bste_test_suites = [
        "//private/google-modules/misc/examples/bste:bste_example_test_suite",
    ],
)
```
*Note: The module under test must be in `vendor_dlkm_modules`, while the test suite is in
`bste_test_suites`.*

### 2. Build and Flash the Device
Build the distribution and flash the device to update the kernel, modules, and BSTE test suites:
```bash
tools/bazel run --config=<device> //private/devices/google/<device>:<device>/dist
# Flash the built images in out/<device>/dist/ to the device
# (e.g., using fastboot flash vendor_dlkm out/<device>/dist/vendor_dlkm.img)
```

### 3. Run the Tests
Use the host-side `tools/bste_test` tool to install and run the tests on a connected device:
```bash
tools/bste_test -p out/<device>/dist/bste_test_package.tar.gz run
```
