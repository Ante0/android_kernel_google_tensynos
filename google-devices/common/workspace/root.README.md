# Pixel Kernel Codebase

## Project Overview
This repository contains the authoritative kernel codebase for Pixel devices. It is used to build
the Linux kernel, out-of-tree kernel modules (vendor drivers), and complete device distribution
packages.

The project is built on **Kleaf**, the Bazel-based build system for the Linux kernel.

## Repository Structure
- `common/` - Kernel packages. Refer to the `kernel_package` section for selection details.
- `private/devices/` - Device configurations and build definitions for Pixel target devices.
- `private/google-modules/` - Kernel modules (out-of-tree vendor drivers) for Pixel devices.
- `tools/` - Command-line tools and execution utilities.
- `build/` - Kleaf build system implementation, build rules, and documentation.
- `prebuilts/` - Prebuilt binaries, toolchains, and host utilities.
- `external/` - External dependencies and third-party projects.

## Building
Ensure you have `bazel` installed on your host system. The host `bazel` command automatically
locates and delegates to `tools/bazel` (a custom Bazel wrapper) at the workspace root, enabling
you to run `bazel` seamlessly from any directory within the workspace. If `bazel` is not
installed on your host, you must explicitly invoke `tools/bazel` from the workspace root.

### Distribution Packages
To build the distribution package (`dist`) for a target device, run:
```bash
# General format:
bazel run --config=<device> //private/devices/google/<device>:<device>/dist
# Example for muzel:
bazel run --config=muzel //private/devices/google/muzel:muzel/dist
```
Alternatively, use the distribution build helper scripts:
```bash
# General format:
tools/build_dist.sh <device>
# Or:
./build_<device>.sh

# Example for muzel:
tools/build_dist.sh muzel
# Or:
./build_muzel.sh
```

### Individual Modules
To build a standalone module for a specific device configuration, run:
```bash
# General format:
bazel build --config=<device> //<path_to_module>:<module>
# Example for muzel:
bazel build --config=muzel //private/google-modules/display/common/gs_panel:gs-panel
```

### Kernel Selection
The repository supports dynamic selection of the underlying kernel base via the
`--kernel_package` build flag. This mechanism, powered by `kernel_selection()`, routes kernel
build dependencies without requiring modifications to downstream target definitions.

The available packages (defined in `common/BUILD.bazel`) are:
- `ack`: Android Common Kernel (ACK) source tree (e.g., `android16-6.12`).
  - Source: `common/ack`
- `gki`: GKI prebuilts and the corresponding source tree (e.g., `android16-6.12-2026-03_r40`).
  - Source: `common/gki`
  - Prebuilts: `prebuilts/gki`
  - Note: Using `--config=pixel_debug_common` or `--nouse_prebuilt_kernel` forces building
    from source instead of using prebuilt artifacts.
- `staging`: Staging kernel source tree.
  - Source: `common/staging`
- `bringup`: Bring-up kernel source tree.
  - Source: `common/bringup`

Example usage:
```bash
# Example for muzel:
bazel run --config=muzel --kernel_package=ack //private/devices/google/muzel:muzel/dist
# Or:
./build_muzel.sh --kernel_package=ack
```

#### Cross-Package Compatibility
If you encounter compatibility differences between kernel packages, you can conditionally
compile specific code paths using Bazel build configurations or kernel Kconfig flags.

When using conditional compilation, always include a `TODO` comment referencing a tracking
bug. This helps maintainers identify when the conditional code can be safely removed after a
package update.

In C source code, use the dynamically generated Kconfig macros:
```c
#if IS_ENABLED(CONFIG_KERNEL_PACKAGE_IS_ACK)
	/* TODO: b/12345678 - Remove condition once GKI updates to Linux 6.12-rc5 */
	// Implementation specific to the ACK source tree
#else
	// Alternative implementation (e.g., for GKI prebuilts)
#endif
```

In `BUILD.bazel` files, use Bazel `select()` with package configuration settings:
```python
copts = select({
    "//common:kernel_package_is_ack": ["-DACK_BUILD"],
    "//conditions:default": [],
})
```

## Development Guides

### Adding a Device
The codebase organizes hardware targets using a two-tier **SoC-device inheritance structure**:

1. **SoC Base Definition (`private/devices/google/<soc>`)**:
   Defines shared silicon-level configurations (e.g., `lga_base`) using `device_build()`. This
   target encapsulates base Kconfigs, defconfigs, and modules for all devices using this SoC.
2. **Device Definition (`private/devices/google/<device>`)**:
   Defines specific hardware products (e.g., `muzel`) using `device_build()`. It inherits silicon
   support by setting `base_device = "//private/devices/google/<soc>:<soc>_base"`, adding device
   trees, display panels, touch controllers, or other modules specific to this device.

#### `device_build`
The `device_build()` macro is the primary hardware packaging rule. It defines all core targets
(such as `<name>/dist`, `<name>/kernel`, and `<name>/kernel_compile_commands`) and automatically
orchestrates building device trees (`dtb`, `dtbo`), out-of-tree vendor drivers, and boot images.

When a `base_device` is specified, most attributes (such as Kconfigs, defconfigs, and module lists)
are inherited from the base silicon target. Attributes that are **not** inherited and must be
configured explicitly on the product device include:
- **Device Tree Inputs/Outputs**: `dts_srcs`, `dts_makefile`, `dtb_outs`, `dtbo_outs`,
  `dtbo_additional_outs`, `ext_dtbos`.
- **In-Tree Module Outputs**: `module_outs`.
- **Image Generation Settings**: `build_dtb`, `build_dtbo`, `build_vendor_kernel_boot`,
  `build_system_dlkm`, `build_vendor_dlkm`, `fs_type`.

#### Configuration Files per Device or SoC
The following files are required:
- `BUILD.bazel`: Contains the `device_build()` definition for the SoC base or target device.
- `device.bazelrc`: Contains Bazel build configurations. All `device.bazelrc` files under
  `private/devices` are automatically imported into the root build environment (handled via
  the `tools/bazel` wrapper).

Other supporting files are optional and can be referenced as needed in `BUILD.bazel`:
- `build.config.<device>`: Shell build configuration.
- `<device>_defconfig`: Defconfig fragment defining kernel options.
- etc.

#### Example SoC Base Definition (`private/devices/google/lga`)

**`BUILD.bazel`**:
```python
load("//private/devices/google/common/kleaf:device_build.bzl", "device_build")

device_build(
    name = "lga_base",
    build_configs = ["build.config.lga"],
    defconfig_fragments = ["lga_defconfig"],
    vendor_dlkm_modules = [
        "//private/google-modules/display/common/gs_panel:gs-panel",
    ],
)
```

**`device.bazelrc`**:
```bazelrc
build:lga_base --//private/devices/google/common:soc=laguna
```

#### Example Product Device Definition (`private/devices/google/muzel`)

**`BUILD.bazel`**:
```python
load("//private/devices/google/common/kleaf:device_build.bzl", "device_build")

device_build(
    name = "muzel",
    base_device = "//private/devices/google/lga:lga_base",
    defconfig_fragments = ["muzel_defconfig"],
    vendor_dlkm_modules = [
        "//private/google-modules/display/panels/muzel:panel-gs-bmea",
    ],
)
```

**`device.bazelrc`**:
```bazelrc
build:muzel --config=lga_base
build:muzel --//private/devices/google/common:kernel=//private/devices/google/muzel:kernel
```

### Adding a Module
To add a new out-of-tree kernel module (vendor driver):

1. **Add Source Code**: Place driver source files under
   `private/google-modules/<category>/[[<vendor>/]<model>]`.
   We recommend following the standard Linux kernel directory structure:
   - Exported headers should be located in `include/` with a unique include path.
   - UAPI headers should be located in `include/uapi/`.
   - Headers for devicetree binding should be located in `include/dt-bindings/`.
   - Documentation should be located in `Documentation/`.
2. **Create `BUILD.bazel`**: Define your module target using `ddk_module`, loaded from
   `@kleaf//build/kernel/kleaf:kernel.bzl`.
3. **Register with Target Devices**: Add your module target label to the appropriate modules list
   within the target device's `device_build()` definition inside
   `private/devices/google/<device>/BUILD.bazel`.
   * **`vendor_ramdisk_modules`**: For boot-critical modules.
   * **`system_dlkm_modules`**: For GKI modules (must use `gki_module()`).
   * **`system_dlkm_modules_blocklisted`**: For GKI modules that should not be auto-loaded.
   * **`vendor_dlkm_modules`**: For typical out-of-tree vendor modules.
   * **`vendor_dlkm_modules_blocklisted`**: For vendor modules that should not be auto-loaded.
   *(Note: For KUnit test modules, register them using the `kunit_modules` attribute. See the
   [KUnit Tests](#kunit-tests) section for details.)*

#### Simple Module Example (`private/google-modules/misc/examples/minimal`)

Here is the recommended structure for a simple module. Exported headers should be located in
`include/` to separate them from internal headers. Use angle brackets for exported headers
(`#include <minimal.h>`) and double quotes for internal headers (`#include "minimal_priv.h"`).

**File Structure**:
```text
private/google-modules/misc/examples/minimal/
├── BUILD.bazel
├── minimal.c                           # Source code
├── minimal_priv.h                      # Internal header
└── include/
    └── minimal.h                       # Exported header
```

**`BUILD.bazel`**:
```python
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

# Visible to all devices and modules.
package(default_visibility = [
    "//private/devices/google:__subpackages__",
    "//private/google-modules:__subpackages__",
])

ddk_module(
    name = "minimal",
    srcs = [
        "minimal.c",
        "minimal_priv.h",               # Internal headers are in srcs.
    ],
    out = "minimal.ko",
    hdrs = ["include/minimal.h"],       # Exported headers are in hdrs.
    includes = ["include"],             # Include path for this module and the dependent modules.
    kernel_build = "//private/devices/google/common:kernel",
    deps = [
        "//common:all_headers_aarch64",
    ],
)
```

#### Example Device Registration (`private/devices/google/muzel`)

**`BUILD.bazel`**:
```python
load("//private/devices/google/common/kleaf:device_build.bzl", "device_build")

device_build(
    name = "muzel",
    base_device = "//private/devices/google/lga:lga_base",
    vendor_dlkm_modules = [
        "//private/google-modules/misc/examples/minimal",
    ],
)
```

#### More Complex Module Example (`private/google-modules/misc/examples/full`)

For complex modules, we recommend following the standard Linux kernel structure. This
example demonstrates:
- **Exported Headers**: Located in `include/` with unique include paths. They should be
  included using angle brackets, e.g., `#include <example/foo.h>`.
- **UAPI Headers**: Located in `include/uapi/` with unique include paths. The `include/uapi`
  directory must be added to the include search path. If a UAPI header is included by another
  UAPI header, it should be included without the `uapi/` prefix, e.g.,
  `#include <example/foo_info.h>`.
- **Device Tree Bindings**: Located in `include/dt-bindings` with unique include paths.
  These are exported to DTS files and should be included with the `dt-bindings/` prefix,
  e.g., `#include <dt-bindings/example/google,foo.h>`.
- **Documentation**: Located in `Documentation/`, following the kernel documentation structure.

**File Structure**:
```text
private/google-modules/misc/examples/full/
├── BUILD.bazel
├── Documentation/
│   └── devicetree/
│       └── bindings/
│           └── example/
│               └── google,foo.yaml     # DT binding documentation
├── drivers/
│   └── example/
│       ├── foo_core.c                  # Core driver source
│       ├── foo_helper.c                # Helper driver source
│       └── foo_internal.h              # Internal driver header
└── include/
    ├── dt-bindings/
    │   └── example/
    │       └── google,foo.h            # DT binding header
    ├── example/
    │   └── foo.h                       # Exported header for other modules
    └── uapi/
        └── example/
            └── foo_info.h              # User-space API header
```

**`BUILD.bazel`**:
```python
# Groups all public headers and exports include directories
ddk_headers(
    name = "foo.headers",
    hdrs = [
        "include/dt-bindings/example/google,foo.h",
        "include/example/foo.h",
        "include/uapi/example/foo_info.h",
    ],
    includes = [
        "include",
        "include/uapi",
    ],
)

# Kernel module target that builds 'foo.ko'.
ddk_module(
    name = "foo.module",
    out = "foo.ko",
    # Exports the headers so dependent modules can use them.
    hdrs = [
        ":foo.headers",
    ],
    srcs = [
        "drivers/example/foo_core.c",
        "drivers/example/foo_helper.c",
        "drivers/example/foo_internal.h",
    ],
    kernel_build = "//private/devices/google/common:kernel",
    deps = [
        "//common:all_headers_aarch64",
    ],
)
```

See `private/google-modules/misc/examples/full/README.md` for more details about this example.

## Testing

### KUnit Tests
KUnit is the Linux kernel's unit testing framework. It allows you to write tests that run
within the kernel, typically executed at module load time. KUnit tests are compiled as kernel
modules that depend on the code under test.

For a complete, working demonstration and instructions on how to implement a KUnit test
(including advanced patterns like function redirection/mocking and visibility management),
please refer to the detailed guide in `private/google-modules/misc/examples/kunit/README.md`.

#### Registering KUnit Tests with a Device

To enable KUnit tests for a device, you must register the test module target using the
`kunit_modules` attribute of the `device_build()` macro. This ensures the test modules are
compiled and packaged into `kunit_tests.zip` for the device.

Do **not** add test modules to the `ext_modules` list, as they should not be included in production
images.

**Example registration (`private/devices/google/muzel/BUILD.bazel`):**
```python
device_build(
    name = "muzel",
    base_device = "//private/devices/google/lga:lga_base",
    vendor_dlkm_modules = [
        "//private/google-modules/misc/examples/kunit:kunit_example",
    ],
    kunit_modules = [
        "//private/google-modules/misc/examples/kunit:kunit_example_kunit_test",
    ],
)
```

#### Running KUnit Tests

Before running KUnit tests, ensure you have built the distribution and flashed the device to
update the kernel and test modules (see [Distribution Packages](#distribution-packages) for
details).

To execute the tests, run the standalone KUnit runner script:
```bash
# General format:
./run_kunit_tests_on_device.sh <device> [-f <filter_pattern>] [--gcov]
# Example for muzel:
./run_kunit_tests_on_device.sh muzel
```

### BSTE (Base System Test Environment)
BSTE is a testing environment for running **user-space tests** that validate the kernel or
kernel modules. This differentiates it from KUnit, which is used for **kernel-space** unit
testing. BSTE provides static, hermetic, and persistent system test suites designed to
withstand kernel panics and intermittent ADB disconnections.

For detailed instructions on how to implement a BSTE test suite, please refer to the
documentation in `private/devices/google/common/kleaf/docs/BSTE.md`.

For a complete, working demonstration of a BSTE test suite (including a driver with an IOCTL
interface and a suspend-to-RAM test), see the example in
`private/google-modules/misc/examples/bste/README.md`.

#### Adding a BSTE Test Suite to a Device

To add a BSTE test suite to a device, you must register the test suite target in the
`bste_test_suites` attribute of the device's `device_build()` definition.

**Example registration (`private/devices/google/muzel/BUILD.bazel`):**
```python
device_build(
    name = "muzel",
    base_device = "//private/devices/google/lga:lga_base",
    vendor_dlkm_modules = [
        "//private/google-modules/misc/examples/bste:bste_example_module",
    ],
    bste_test_suites = [
        "//private/google-modules/misc/examples/bste:bste_example_test_suite",
    ],
)
```

#### Running BSTE Tests

Before running BSTE tests, ensure you have built the distribution and flashed the device to
update the kernel, modules, and test suites (see [Distribution Packages](#distribution-packages)
for details).

To run the tests on a connected device, use the host-side `tools/bste_test` tool to install and
execute the test package from the distribution directory:
```bash
# General format:
tools/bste_test -p out/<device>/dist/bste_test_package.tar.gz run
# Example for muzel:
tools/bste_test -p out/muzel/dist/bste_test_package.tar.gz run
```
*(Note: You can specify a target device using the `-s <serial>` option if multiple devices
are connected.)*

For interactive on-device debugging or detached background execution, refer to the detailed
documentation in `private/devices/google/common/kleaf/docs/BSTE.md`.

## Developer Tools
The repository provides out-of-the-box integration with standard kernel development utilities.

### `checkpatch`
Each out-of-tree module project root should include a `checkpatch` build target to validate
commits and source modifications against the Linux kernel coding style. We recommend using the
`checkpatch_strict()` macro instead of the base `checkpatch()` rule.

To run `checkpatch` on a specific module directory:
```bash
bazel run //private/google-modules/misc:checkpatch -- --git_sha1 HEAD
```

**Example `checkpatch` Definition**:
```python
load("//private/devices/google/common/kleaf:checkpatch.bzl", "checkpatch_strict")

checkpatch_strict(
    name = "checkpatch",
    ignores = [
        "LINUX_VERSION_CODE",  # Project-level checkpatch ignores
    ],
)
```

**About `checkpatch_strict` and Ignores**:
The `checkpatch_strict()` macro automatically applies a default ignore list covering common
infrastructure tags (`GERRIT_CHANGE_ID`, `BAD_FIXES_TAG`, `GIT_COMMIT_ID`, `UNKNOWN_COMMIT_ID`,
and `FILE_PATH_CHANGES`).

You can suppress checkpatch warnings in two ways:
- **Project-Level Suppression**: Add the warning type to the `ignores` attribute in your
  `BUILD.bazel` target as shown above. This suppresses the warning across all files in the
  module project.
- **Single-Commit Suppression**: Add the `Ignore-Checkpatch: <Reason>` tag to your Git
  commit message to suppress the warning for that individual commit only.

### `compile_commands.json`
To enable accurate code navigation, autocompletion, and static analysis (using tools like
`clangd`, `clang-tidy`, etc.), generate the root `compile_commands.json` compilation
database.
```bash
# General format:
bazel run --config=<device> //private/devices/google/<device>:<device>/kernel_compile_commands
# Example for muzel:
bazel run --config=muzel //private/devices/google/muzel:muzel/kernel_compile_commands
```
Alternatively, use the helper script:
```bash
# General format:
tools/build_compile_commands.sh <device>
# Example for muzel:
tools/build_compile_commands.sh muzel
```
Once generated, configure your editor or language server to use the repository root.

### `clang-format`
The codebase uses a hierarchical `.clang-format` configuration:
- **Root configuration (`.clang-format`)**: Inherited from the Android Common Kernel
  (8-character tabs, 80-column limit).
- **Private configuration (`private/.clang-format`)**: Overrides `ColumnLimit` to 100 for all
  modules and devices under `private/`.

#### Subproject Customization
To customize formatting for a specific subproject, add a local `.clang-format` file that
inherits from the parent configuration:
```yaml
---
# Inherit most settings from .clang-format in parent directory
BasedOnStyle: InheritParentConfig
# Override specific settings
ColumnLimit: 80
...
```
