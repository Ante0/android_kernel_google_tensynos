# Rust ddk_module Example

This is an example of a Rust kernel module using `ddk_module`.

## Files

- `rust_minimal.rs`: The Rust source code for the kernel module.
- `BUILD.bazel`: The Bazel build file using `ddk_module`.
- `Kconfig`: The Kconfig definition for the module.
- `defconfig`: The default configuration to enable the module.

## How to build

To build this module, run:

```bash
tools/bazel build //private/google-modules/misc/examples/rust_minimal:rust_minimal
```
