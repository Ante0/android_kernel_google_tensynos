# AGENTS.md for Pixel Kernel Codebase

## Project Overview
This repository contains the Pixel kernel codebase, responsible for building the
kernel, kernel modules, and kernel images for Pixel devices.

## Build System
The project uses **Kleaf**, which is a Bazel-based build system for the Linux kernel.

- **Build Command:** Use `tools/bazel` for all build operations.
- **Documentation:** Detailed documentation for Kleaf can be found in `build/kernel/kleaf/docs`.

## Key Directories
- `build/kernel/kleaf`: Kleaf build system source and docs.
- `common/ack`: Android common kernel source and docs.
- `private/devices/google`: Configuration for Pixel devices.
- `private/google-modules`: Kernel modules for Pixel devices.

## Development Guidelines
- **Building:**
  - Always use `tools/bazel`.
  - To build the distribution package of a device, run:
    ```
    tools/bazel run --config=<device> //private/devices/google/<device>:<device>/dist
    ```
  - To build a single module for a device, run:
    ```
    tools/bazel build --config=<device> //<path_to_module>:<module>
    ```
  - Ask the user if you need to know which device configuration to use.
- **Searching:**
  - Prioritize searching in `common/ack` and `private/` directories for relevant source code.
  - Ignore `out` and `bazel-*`, they are for output files.
- **Licensing:**
  - **SPDX License Identifier:** Every source file must contain an SPDX-License-Identifier.
    - **Placement:** Must be on the **first possible line** that can contain a comment. If a shebang (`#!`) is present, place the identifier on the **second line**.
    - **New Files:**
      - For new code, use `GPL-2.0-only`.
      - For new devicetree or DT binding files, use `GPL-2.0-only OR BSD-2-Clause`.
    - **Comment Style:** Adhere to `common/ack/Documentation/process/license-rules.rst`:
      - C source (`.c`): `// SPDX-License-Identifier: GPL-2.0-only`
      - C header (`.h`): `/* SPDX-License-Identifier: GPL-2.0-only */`
      - Assembly (`.S`, `.s`): `/* SPDX-License-Identifier: GPL-2.0-only */`
      - Scripts (`.sh`, `.py`, etc.): `# SPDX-License-Identifier: GPL-2.0-only`
      - Documentation (`.rst`): `.. SPDX-License-Identifier: GPL-2.0-only`
      - Device Tree (`.dts`, `.dtsi`): `// SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause`
- **Coding Style:**
  - **General:** Follow the Linux kernel coding style as described in `common/ack/Documentation/process/coding-style.rst`.
  - **Indentation:**
    - C (`.c`, `.h`), Assembly (`.S`, `.s`), Device Tree (`.dts`, `.dtsi`), Kconfig, and Makefiles: Use **Tabs** (8 characters).
    - Python (`.py`): Use **4 spaces**.
    - Shell scripts (`.sh`): Use **2 spaces**.
    - Bazel files (`BUILD`, `WORKSPACE`, `.bazel`, `.bzl`): Use **4 spaces**.
  - **Formatting:**
    - Max line length is 100 characters.
    - Every file must end with a single newline character (no multiple trailing empty lines).
    - No trailing whitespace at the end of any line.

## Source Control
- **Mandatory Sign-off:** **Every** commit made in this workspace **must** include a
  `Signed-off-by:` tag. This is a non-negotiable requirement for all kernel
  changes.
- **Signing Method:** Use ONLY the `git commit -s` flag to add the signature. NEVER manually
  construct, type, or append the `Signed-off-by:` tag in the commit message body.
