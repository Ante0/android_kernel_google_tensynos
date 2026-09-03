# Full Feature DDK Module Example

This directory contains a comprehensive example of a kernel module setup using the Kleaf build
system. It demonstrates best practices for structuring kernel modules, handling inter-module
dependencies, conditional compilation, exporting UAPI headers, and integrating Device Tree bindings.

## Directory Structure

*   `drivers/example/`: Contains the driver source code.
    *   `foo_core.c`, `foo_helper.c`, `foo_internal.h`: Source files for the `foo` module.
    *   `bar.c`: Source file for the `bar` module.
    *   `Kconfig`: Kernel configuration options (`CONFIG_EXAMPLE_FOO`, `CONFIG_EXAMPLE_BAR`).
*   `include/`:
    *   `example/foo.h`: Kernel-space APIs exported by `foo` for other modules (like `bar`).
    *   `uapi/example/foo_info.h`: User-space APIs (UAPI) exported by `foo`.
    *   `dt-bindings/example/google,foo.h`: Device Tree binding definitions.
*   `Documentation/`:
    *   `devicetree/bindings/example/google,foo.yaml`: Device Tree schema binding style.
*   `BUILD.bazel`: The Bazel build file defining the targets.

## Key Concepts Demonstrated

### 1. Inter-Module Dependencies

The example defines two modules where one depends on the other:
*   `foo.ko` (defined by `:foo`): A platform driver that exports `foo_api_func()`.
*   `bar.ko` (defined by `:bar`): A module that calls `foo_api_func()`.

In `BUILD.bazel`, the dependency is declared in the `deps` attribute of the `bar` module:
```python
ddk_module(
    name = "bar",
    srcs = [ ... ],
    out = "bar.ko",
    deps = [
        ":foo",  # Dependency on the foo alias
        "//common:all_headers_aarch64",
    ],
)
```

### 2. Conditional Compilation & Interface Stubs

The `foo` module demonstrates how to handle optional modules. It allows dependent modules (like
`bar`) to compile cleanly even if `foo` is not enabled.

*   **Alias Selection**: In `BUILD.bazel`, the `:foo` target is an `alias` that selects between
    `:foo.module` (builds `foo.ko`) and `:foo.headers` (only provides headers), based on the
    `foo.enable` Bazel flag.
    ```python
    alias(
        name = "foo",
        actual = select({
            ":foo.enabled": ":foo.module",
            "//conditions:default": ":foo.headers",
        }),
    )
    ```
*   **Interface Stubs**: In `include/example/foo.h`, if `CONFIG_EXAMPLE_FOO` is not enabled,
    `foo_api_func()` is defined as an empty static inline function:
    ```c
    #if IS_ENABLED(CONFIG_EXAMPLE_FOO)
    void foo_api_func(void);
    #else
    static inline void foo_api_func(void) {}
    #endif
    ```
*   **Compile-time Safeguard**: If `foo.module` is explicitly built but `CONFIG_EXAMPLE_FOO` is not
    enabled in the kernel configuration, the build will fail with a compile-time error. This is
    achieved using `conditional_srcs` in `BUILD.bazel`:
    ```python
    conditional_srcs = {
        "CONFIG_EXAMPLE_FOO": {
            True: [
                "drivers/example/foo_core.c",
                ...
            ],
            False: [
                ":foo.missing_required_config.c", # Contains "#error CONFIG_EXAMPLE_FOO is not set"
            ],
        },
    }
    ```

### 3. UAPI Headers Export

It demonstrates how to export UAPI headers to userspace using the `ddk_uapi_headers` rule, which
packages them into a tarball (`foo_uapi_headers.tar.gz`).

```python
ddk_uapi_headers(
    name = "foo.uapi_headers",
    srcs = [
        "include/uapi/example/foo_info.h",
    ],
    out = "foo_uapi_headers.tar.gz",
    kernel_build = "//private/devices/google/common:kernel",
)
```

### 4. Device Tree Bindings

It shows how to integrate Device Tree bindings (`include/dt-bindings/example/google,foo.h`) and
document them using DT schema (`Documentation/devicetree/bindings/example/google,foo.yaml`).

```python
filegroup(
    name = "foo.dt-bindings",
    srcs = [
        "include/dt-bindings/example/google,foo.h",
    ],
)
```

## Integrating and Building with a Device

To actually use these modules on a device, they must be integrated into the device's build
configuration. Below are two examples using a generic device (referred to as `<device>`, with
configuration files located in `private/devices/google/<device>`) to demonstrate how to integrate
them.

The integration involves modifying the following device files:
*   `private/devices/google/<device>/BUILD.bazel`: Device build targets.
*   `private/devices/google/<device>/build.config.<device>`: Device-specific environment variables.
*   `private/devices/google/<device>/device.bazelrc`: Device-specific Bazel flags.
*   `private/devices/google/<device>/Kconfig.ext.<device>`: External Kconfig sourcing.
*   `private/devices/google/<device>/<device>_defconfig`: Kernel configuration.

---

### Scenario 1: Build `bar` WITH `foo`

In this scenario, both `foo` and `bar` are enabled. `foo.ko` is built, `bar.ko` uses the symbols
exported by `foo.ko`, and we integrate Device Tree bindings.

#### 1. Configure Kconfig
*   Add the example's `:kconfig` target to `kconfigs` in the device's `BUILD.bazel` file:
    ```python
    device_build(
        name = "<device>",
        ...
        kconfigs = [
            ...
            "//private/google-modules/misc/examples/full:kconfig",
        ],
    )
    ```
*   Source the example's `Kconfig` in the device's `Kconfig.ext.<device>` file:
    ```makefile
    source "$(KCONFIG_EXT_MODULES_PREFIX)private/google-modules/misc/examples/full/drivers/example/Kconfig"
    ```

#### 2. Enable both Configs in Defconfig
In the device's `<device>_defconfig` file, enable both:
```makefile
CONFIG_EXAMPLE_FOO=m
CONFIG_EXAMPLE_BAR=m
```

#### 3. Enable `foo` in Bazel Configuration
To tell the `:foo` alias to resolve to `:foo.module` instead of just headers, enable the
`foo.enable` flag in the device's `device.bazelrc` file:
```
build:<device> --//private/google-modules/misc/examples/full:foo.enable
```

#### 4. Add Modules to the Device Modules
Add both `:bar` and `:foo` targets to `vendor_dlkm_modules` in the device's `BUILD.bazel` file:
```python
device_build(
    name = "<device>",
    ...
    vendor_dlkm_modules = [
        ...
        "//private/google-modules/misc/examples/full:bar",
        "//private/google-modules/misc/examples/full:foo",
    ],
)
```

#### 5. Add DT Bindings and Update Device Tree
*   **Export DT Bindings**: Add `:foo.dt-bindings` to `dts_srcs` in the device's `BUILD.bazel` file.
    ```python
    device_build(
        name = "<device>",
        ...
        dts_srcs = glob(["dts/**"]) + [
            "//private/google-modules/misc/examples/full:foo.dt-bindings",
        ],
    )
    ```
*   **Update DTC_INCLUDE**: Add the include path to `DTC_INCLUDE` in `build.config.<device>`.
    This tells Kleaf to include the binding headers in the DTC compiler's search path.
    ```bash
    DTC_INCLUDE=" \
      ...
      ${ROOT_DIR}/private/google-modules/misc/examples/full/include \
    "
    ```
    DTS files can include the headers like:
    ```dts
    #include <dt-bindings/example/google,foo.h>
    ```

#### 6. Add UAPI Headers
Add the `:foo.uapi_headers` target to `ddk_uapi_headers` in the device's `BUILD.bazel` file to
include them in the device's merged UAPI package:
```python
device_build(
    name = "<device>",
    ...
    ddk_uapi_headers = [
        ...
        "//private/google-modules/misc/examples/full:foo.uapi_headers",
    ],
)
```

#### 7. Build
Build the device distribution package:
```bash
tools/bazel run --config=<device> //private/devices/google/<device>:<device>/dist
```
This builds both `foo.ko` and `bar.ko` and includes them in `vendor_dlkm.img`.

---

### Scenario 2: Build `bar` WITHOUT `foo`

In this scenario, the `foo` module is disabled. The `bar` module is built, but it uses the inline
stub for `foo_api_func()` defined in `foo.h`. `foo.ko` is NOT built.

#### 1. Configure Kconfig
*   Add the example's `:kconfig` target to `kconfigs` in the device's `BUILD.bazel` file:
    ```python
    device_build(
        name = "<device>",
        ...
        kconfigs = [
            ...
            "//private/google-modules/misc/examples/full:kconfig",
        ],
    )
    ```
*   Source the example's `Kconfig` in the device's `Kconfig.ext.<device>` file:
    ```makefile
    source "$(KCONFIG_EXT_MODULES_PREFIX)private/google-modules/misc/examples/full/drivers/example/Kconfig"
    ```

#### 2. Enable the Config in Defconfig
In the device's `<device>_defconfig` file, enable only `CONFIG_EXAMPLE_BAR`:
```makefile
CONFIG_EXAMPLE_BAR=m
```

#### 3. Add `bar` to the Device Modules
Add the `:bar` target to the `vendor_dlkm_modules` list of the `device_build` target in the device's
`BUILD.bazel` file:
```python
device_build(
    name = "<device>",
    ...
    vendor_dlkm_modules = [
        ...
        "//private/google-modules/misc/examples/full:bar",
    ],
)
```

#### 4. Build
Build the device distribution package:
```bash
tools/bazel run --config=<device> //private/devices/google/<device>:<device>/dist
```
This builds only `bar.ko` and includes it in `vendor_dlkm.img`.
