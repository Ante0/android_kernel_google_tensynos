# KUnit Example with ddk_module

This directory contains an example of how to implement and build a KUnit test for a kernel module
using the `ddk_module` rule in Kleaf. It specifically demonstrates the usage of
`KUNIT_STATIC_STUB_REDIRECT` for function redirection (mocking) of internal functions.

## Directory Structure

- `BUILD.bazel`: Defines the Bazel targets for the module and the test.
- `Kconfig`: Kconfig definition for the main module.
- `kunit_example.c`: Source code for the main module, featuring a public API and a redirectable
  internal function.
- `kunit_example.h`: Header file for the main module.
- `test/`:
    - `Kconfig`: Kconfig definition for the test module.
    - `kunit_defconfig`: Defconfig fragment to enable the module and test during KUnit builds.
    - `kunit_example_test.c`: Source code for the KUnit test, demonstrating how to mock the
      internal function.

## Key Concepts

### Function Redirection (Mocking)

The `KUNIT_STATIC_STUB_REDIRECT` macro allows KUnit tests to replace an internal function call with
a "stub" or "mock" implementation. This is useful for isolating a public API from its dependencies
(e.g., hardware access).

1.  **In the main code**:
    - The public API `kunit_example_process_data` calls the internal helper `kunit_example_raw_add`.
    - `kunit_example_raw_add` uses `KUNIT_STATIC_STUB_REDIRECT` at the start of its definition.

2.  **In the test code**:
    - The test calls the public API `kunit_example_process_data`.
    - It uses `kunit_activate_static_stub` to redirect the internal `kunit_example_raw_add` to a
      mock implementation.

### Visibility and Symbols

- `VISIBLE_IF_KUNIT`: Used in the **source file** (`.c`). It makes a symbol `static` unless
  `CONFIG_KUNIT` is enabled.
- `IS_ENABLED(CONFIG_KUNIT)`: Used in the **header file** (`.h`) to wrap the declaration. This
  ensures the declaration is only available when building for tests, preventing conflicts in normal
  builds where the function is `static`.
- `EXPORT_SYMBOL_IF_KUNIT`: Used in the **source file**. It exports a symbol only if `CONFIG_KUNIT`
  is enabled, placing it in the `EXPORTED_FOR_KUNIT_TESTING` namespace.
- `MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING)`: Required in the test module to use symbols
  exported via `EXPORT_SYMBOL_IF_KUNIT`.

## Building the Example

To build the modules, run:

```bash
# Build the main module
tools/bazel build //private/google-modules/misc/examples/kunit:kunit_example

# Build the KUnit test module
tools/bazel build //private/google-modules/misc/examples/kunit:kunit_example_kunit_test
```
