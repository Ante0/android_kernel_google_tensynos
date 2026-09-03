# APC IRM Driver and Testing

## Overview

This directory contains the APC IRM (Interconnect Resource Manager) driver,
which manages bandwidth and performance level voting for various hardware
clients (e.g., IP blocks) on Pixel devices.

## Key Files

-   `apc_irm_drv.c`: Main driver implementation. Handles vote aggregation and
    hardware programming.
-   `include/perf/core/apc_irm.h`: Public API header. Exposes client/subclient
    management and voting functions.
-   `apc_irm_debug.h`: Debug API header. Exposes internal state for testing.
-   `test/apc_irm_test.c`: KUnit test suite for validating driver logic.

## Testing

The driver is tested using a KUnit module `apc_irm_test.ko`.

### Building the Test

Build the test module using Bazel (example for `spacecraft` target): `bash
tools/bazel build --config=spacecraft
//private/google-modules/perf/core/apc_irm:apc_irm_test`

### Running the Test

**Option 1: Using the test script (Recommended)**

After flashing the kernel to the target device (which is connected to the
development machine), from the project root directory, run the test by executing
on your development machine:

```bash
./run_kunit_tests_on_device.sh -f apc_irm_test.apc_irm_test_* spacecraft
```

You can then check the results in the output of the script.

**Option 2: Manual steps**

1.  **Push the module to the device:** The build output is located in the
    `bazel-bin` directory structure. `bash adb push
    bazel-bin/private/google-modules/perf/core/apc_irm/apc_irm_test/apc_irm_test.ko
    /data/local/tmp/`

2.  **Run the test:**

    ```bash
    # We use 6.12.45-android16-5 kernel as an example here
    # If the kunit module has been installed without enable=1, you need to "rmmod kunit" first.
    # You can adapt the filter_glob parameter to your case to run specific tests or delete it to run all tests.
    adb shell insmod /vendor/lib/modules/6.12.45-android16-5/kernel/lib/kunit/kunit.ko enable=1 filter_glob=apc_irm_test.apc_irm_test*

    # Run with default clients
    adb shell insmod /data/local/tmp/apc_irm_test.ko

    # Run with different clients
    adb shell insmod /data/local/tmp/apc_irm_test.ko sync_client_name="my_sync_client" async_client_name="my_async_client"
    ```

3.  **View Results:** Check `dmesg` for KUnit output. You should see "ok" for
    passing tests. `bash adb shell dmesg | grep "kunit"`

4.  **Cleanup:** To run the test again, you must remove the module first. `bash
    adb shell rmmod apc_irm_test`

## Debugging

The driver exposes internal state via `debugfs` at `/sys/kernel/debug/apc_irm/`.

-   **Client State**: `/sys/kernel/debug/apc_irm/<client_name>/published_vote`
    shows the aggregated vote sent to hardware.
-   **Subclient State**:
    `/sys/kernel/debug/apc_irm/<client_name>/<subclient_name>/staged_vote` shows
    the current vote request from a specific subclient.

## Development Notes

-   **Vote Aggregation**: The driver aggregates votes from multiple *subclients*
    into a single vote for the *parent client*.
    -   Bandwidth votes are **summed**.
    -   Performance Level (PF) votes use the **minimum** value (representing the
        strongest requirement, where lower values usually mean higher
        performance or specific constraints).
-   **Synchronization**:
    -   **Overall Scheme (Hybrid)**: The driver employs a hybrid locking
        strategy to support both atomic and sleepable contexts.
        -   A **spinlock** protects shared data structures (client lists, vote
            aggregation), allowing updates from atomic contexts.
        -   A **mutex** is used *additionally* for synchronous clients to
            serialize slow hardware operations that may sleep.
    -   **Async Clients**: Use the fully atomic path (spinlock only).
    -   **Sync Clients**: Use the sleepable path (mutex for serialization +
        spinlock for data protection).
-   **Testing Hooks**:
    -   `get_irm_client_published_vote`: Allows the test module to inspect the
        internal aggregated vote state.
    -   `get_irm_register`: Allows the test module to read back register values
        to verify hardware writes (assuming registers are readable).
-   **Dependencies**: The test depends on the `apc_irm` driver being loaded and
    initialized (probing completed).
