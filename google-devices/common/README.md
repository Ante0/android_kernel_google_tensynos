# Building with Bazel

```shell
# Files are copied to out/raviole/dist
$ tools/bazel run --config=raviole //private/devices/google/raviole:gs101_raviole_dist
```

See `build/kernel/kleaf/README.md` for details.

# ABI monitoring with Bazel

**Note**: ABI monitoring is not supported on `android-mainline` branch.

```shell
# Compare ABI and build files for distribution
$ tools/bazel build --config=raviole //private/devices/google/raviole:gs101_raviole_abi

# Update symbol list aosp/android/abi_gki_aarch64_pixel
$ tools/bazel run --config=raviole //private/devices/google/raviole:gs101_raviole_abi_update_symbol_list

# Update ABI aosp/android/abi_gki_aarch64.xml
$ tools/bazel run //aosp:kernel_aarch64_abi_update
```
