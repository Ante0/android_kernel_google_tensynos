# Minimal C ddk_module Example

This is a minimal example of a C kernel module using `ddk_module`.

## Files

- `minimal.c`: The main C source code for the kernel module.
- `minimal_priv.h`: An internal header used by the module.
- `include/minimal.h`: An exported header that other modules can include.
- `BUILD.bazel`: The Bazel build file using `ddk_module`.
- `Kconfig`: The Kconfig definition for the module.
- `defconfig`: The configuration to enable the module.

## How to build

To build this module, run:

```bash
tools/bazel build //private/google-modules/misc/examples/minimal:minimal
```
