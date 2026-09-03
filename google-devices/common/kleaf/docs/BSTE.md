# BSTE (Base System Test Environment)

## Introduction
BSTE provides a test framework tailored for kernel development, optimized for environments where
assumptions like stable ADB connections or standard userspace libraries do not hold.

## Key Features
*   **Hermeticity**: Binaries are fully static and portable.
*   **Autonomy**: A background runner handles disconnections.
*   **Persistence**: Survivable cross-panic crash tracking state.

## Developer Guidelines
- Test code must be enclosed in shell functions. No top-level code.
- Use `suite_init`/`suite_exit` and `test_init`/`test_exit` for lifecycle management.
- Use relative paths to locate embedded binaries and data.
- Put artifacts in the scoped `$OUT` directory.

## Rules Usage

### `bste_cc_binary`
Wraps `cc_binary` to produce fully static, standalone C/C++ binaries for the target Android
device.
- **Attributes**:
  - `srcs`: Sources.
  - `data`: (optional) Runtime dependencies.
  - `*`: Inherits all standard `cc_binary` attributes (`deps`, `copts`, etc.).
- **Usage**:
```python
load("//private/devices/google/common/kleaf:bste.bzl", "bste_cc_binary")

bste_cc_binary(
    name = "checker",
    srcs = ["checker.c"],
)
```

### `bste_prebuilt_binary`
Incorporates existing prebuilt executables or shell scripts into the BSTE test environment, applying
a device platform transition and ensuring executable permissions.
- **Attributes**:
  - `src`: The source executable file or script.
  - `data`: (optional) Runtime dependencies.
- **Usage**:
```python
load("//private/devices/google/common/kleaf:bste.bzl", "bste_prebuilt_binary")

bste_prebuilt_binary(
    name = "log_parser",
    src = "scripts/parse_logs.sh",
    data = ["data/error_codes.csv"],
)
```

### `bste_test_suite`
The primary packaging unit for defining a self-contained BSTE test suite. Encapsulates test scripts,
binaries, and data into an isolated directory and generates runtime harness metadata.
- **Attributes**:
  - `srcs`: Shell script source files (must end in `.sh`).
  - `data`: (optional) Runtime dependencies (`bste_cc_binary`, `bste_prebuilt_binary`, data files).
  - `test_cases`: Explicit list of shell function names representing individual test cases.
  - `suite_init` / `suite_exit`: (optional) Suite-level setup and cleanup function names.
  - `test_init` / `test_exit`: (optional) Test-level setup and cleanup function names.
- **Usage**:
```python
load("//private/devices/google/common/kleaf:bste.bzl", "bste_test_suite")

bste_test_suite(
    name = "my_suite",
    srcs = ["test.sh"],
    data = [
        ":checker",
        ":log_parser",
    ],
    suite_init = "my_suite_init",
    test_cases = ["check_feature"],
)
```

### `bste_test_package`
Aggregates multiple BSTE test suites and the core execution harness into a single relocatable
deployment tarball (`.tar.gz`).
- **Attributes**:
  - `test_suites`: List of `bste_test_suite` targets to aggregate.
- **Usage**:
```python
load("//private/devices/google/common/kleaf:bste.bzl", "bste_test_package")

bste_test_package(
    name = "my_test_package",
    test_suites = [":my_suite"],
)
```

### Device Build Integration (`device_build`)
Integrates BSTE test suites directly into a device build target. The build system automatically
aggregates all device-specific and inherited base device test suites into a unified deployment
tarball (`{name}/bste_test_package.tar.gz`) and includes it in the device distribution (`dist`)
outputs.
- **Usage**:
```python
load("//private/devices/google/common/kleaf:device_build.bzl", "device_build")

# Include my_suite in out/shusky/dist/bste_test_suites.tar.gz
device_build(
    name = "shusky",
    ...
    bste_test_suites = [":my_suite"],
    ...
)
```

## Test Script Implementation Guidelines

When implementing BSTE test scripts (e.g., `test.sh`), adhere to the following structural rules
and utilize the built-in variables and helper functions provided by the harness.

### Structural Rules
1. **No Top-Level Code**: All test logic, setup, and cleanup must be encapsulated within shell
   functions (e.g., `my_suite_init`, `check_feature`). Commands executed outside functions will
   run prematurely during the harness discovery phase.
2. **Subshell Isolation**: Every function (including `suite_init`, `suite_exit`, `test_init`,
   `test_exit`, and test cases) is executed within a dedicated subshell. Consequently, any
   environment variables or state modifications made within a function will not be carried over
   to subsequent functions or test cases. Use `$SUITE_OUT` or `$TEST_OUT` to share state files.
3. **Relative Paths**: Access auxiliary binaries and data files using relative paths (e.g.,
   `./checker`, `data/config.json`), as `bste_test_suite` preserves the exact package directory
   structure.
4. **Init/Exit Return Values**: All setup and cleanup functions (`suite_init`, `suite_exit`,
   `test_init`, `test_exit`) must return `0` on success. A non-zero return value indicates an
   infrastructure failure and will be treated as an `ERROR`.
5. **Test Case Return Values**: Individual test case functions must either return `0` on success
   or explicitly invoke a helper function (`pass`, `fail`, `skip`, `error`). If a test case
   function terminates with a non-zero return value without calling a helper, it will be treated
   as an `ERROR`.

### Environment Variables
The harness manages persistent storage per run and automatically exports the following scoped
directory variables:
- `SUITE_OUT`: Absolute path to a persistent directory dedicated to the current test suite
  (`$RUN_DIR/test_suites/$TEST_SUITE/out`). Shared across all test cases within the suite.
- `TEST_OUT`: Absolute path to a persistent directory dedicated entirely to the current individual
  test case (`$RUN_DIR/test_suites/$TEST_SUITE/$TEST_CASE/out`).
- `OUT`: Alias variable automatically managed by the harness. Points to `SUITE_OUT` during
  `suite_init`/`suite_exit`, and `TEST_OUT` during `test_init`, `test_cases`, and `test_exit`.
  Always use `$OUT` to store test artifacts, logs, or diagnostic dumps.

### Helper Functions (Aliases)
The harness automatically injects the following helper aliases into your test environment for
standardized reporting and flow control:
- `pass`: Terminates the current test case immediately with a `PASSED` status. Takes no arguments.
- `fail [message]`: Terminates the current test case immediately with a `FAILED` status. Accepts
  an optional explanation message, which is logged along with the caller's file and line number.
- `skip [message]`: Terminates the current test case immediately with a `SKIPPED` status. Accepts
  an optional explanation message.
- `error [message]`: Terminates the current test case immediately with an `ERROR` status
  (indicating an infrastructure or unrecoverable setup failure). Accepts an optional error message.
- `assert_true <expr> [message]`: Evaluates a shell expression (e.g., `"[ $val -eq 1 ]"`). If
  false, calls `fail` with diagnostic details and the optional message.
- `assert_false <expr> [message]`: Evaluates a shell expression. If true, calls `fail`.
- `assert_eq <expected> <actual> [message]`: Compares `expected` and `actual` strings. If they
  differ, calls `fail`.
- `assert_ne <val1> <val2> [message]`: Compares `val1` and `val2` strings. If they are equal,
  calls `fail`.

### Example Test Script (`test.sh`)
```bash
# SPDX-License-Identifier: GPL-2.0-only

my_suite_init() {
  echo "Initializing suite environment..."
  # Verify required hardware or binaries exist
  [ -f "./checker" ] || return 1
  # Store suite-level diagnostic data
  echo "init_ok" > "${SUITE_OUT}/suite_state.txt"
  return 0
}

check_feature() {
  echo "Running checker binary..."
  # Direct execution output to case-specific $OUT
  ./checker > "${OUT}/checker.log" 2>&1
  local ret="$?"

  # Use assertions for clean validation
  assert_eq 0 "${ret}" "Checker binary failed to execute successfully"

  # Terminate with explicit pass
  pass
}
```

## Execution

### Host-Side Execution (`tools/bste_test`)
Use the host-side `tools/bste_test` tool to install and run test packages on a connected device.
You can use the `-s SERIAL` global option to specify a target device when multiple devices are
connected.

> **Note**: `tools/bste_test` is a symlink to `private/devices/google/common/tools/bste_test.sh`.

```bash
# 1. Build the package
tools/bazel build //private/devices/google/common/kleaf/tests/bste_test:sample_test_package

# 2. Install package & List test suites and cases
tools/bste_test \
  -p bazel-bin/private/devices/google/common/kleaf/tests/bste_test/sample_test_package.tar.gz list

# 3. Run all tests
tools/bste_test run

# 4. Run only a specific test suite (e.g., sample_test_suite)
tools/bste_test run sample_test_suite

# 5. Run only a specific test case within a suite (e.g., sample_test_suite.test_case_1)
tools/bste_test run sample_test_suite.test_case_1

# 6. Run tests matching a glob pattern (e.g., all test_* cases across all suites)
tools/bste_test run *.test_*

# 7. View summary of the latest run
tools/bste_test summary
```

### Device-Side Execution (`bste_test`)
Once a BSTE package is installed on the device (located at
`/data/local/tmp/bste_test_package/bste_test`), you can interact with it directly via `adb shell`
or an on-device terminal using the following commands:

- `run [-d|--detach] [filter...]`: Start a new test session. Use `-d` to run in the background.
- `attach`: Stream the live execution log (`session.log`) of the currently active session.
- `status [--raw]`: Display the real-time state of the active session (Run ID, PID, Phase,
  Suite/Case).
- `summary [--raw]`: Display cumulative pass/fail test results in chronological order.
- `kill`: Terminate the active test session and clean up orphaned background process groups.
- `list [filter...]`: Discover installed test suites and test cases without executing them.
- `history`: View a scoreboard summary of past execution sessions and their final statuses.
- `log [run_id]`: Dump the full stdout/stderr session log (defaults to the latest run).
- `help`: Show the built-in device-side help and usage message.

#### Example: Interactive On-Device Testing
Log into the device shell to run tests, check status, and view results directly.

```bash
# 1. Access the device shell
adb shell

# 2. Navigate to the installed package directory
cd /data/local/tmp/bste_test_package

# 3. List available test suites and cases
./bste_test list

# 4. Run a specific test suite
./bste_test run sample_test_suite

# 5. View cumulative test results
./bste_test summary
```

### Detached Execution Workflows
BSTE is specifically designed to survive ADB disconnections or terminal closures. You can leverage
the background runner using either interactive detachment or fully asynchronous execution.

#### Example 1: Interactive Detachment and Re-attachment
Start a test session interactively, detach at will, and re-attach later to check progress.

```bash
# 1. Install package & List test suites and cases
tools/bste_test \
  -p bazel-bin/private/devices/google/common/kleaf/tests/bste_test/sample_test_package.tar.gz list

# 2. Start the test session interactively
tools/bste_test run

# 3. Press Ctrl-C at any time during execution to detach.
# The on-device background runner will continue executing uninterrupted.

# 4. Re-attach to the live session log later
tools/bste_test attach
```

#### Example 2: Fully Asynchronous Execution and Polling
Start a test session in the background immediately, poll its real-time status, and view results
upon completion.

```bash
# 1. Install package & List test suites and cases
tools/bste_test \
  -p bazel-bin/private/devices/google/common/kleaf/tests/bste_test/sample_test_package.tar.gz list

# 2. Start the test session in detached mode (-d)
tools/bste_test run -d

# 3. Poll the session status periodically until STATUS transitions from RUNNING
tools/bste_test status

# 4. Show cumulative test results once finished
tools/bste_test summary
```
