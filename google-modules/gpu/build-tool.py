#!/usr/bin/env python3
# Copyright 2026 Google LLC
# SPDX-License-Identifier: GPL-2.0-only

import argparse
import glob
import os
import re
import subprocess
import sys
import xml.etree.ElementTree

WORKSPACE = os.getcwd()
MANIFEST_BRANCH = "android16-gs-pixel-6.12"

ALLOWED_MODULES = ["gpu", "soc/gs", "power/mitigation"]
DISALLOWED_SOC_GS_MODULES = [
    "drivers/soc/google/vh/kernel/sched",
    "drivers/dma-buf/heaps/samsung",
    "drivers/soc/google/pixel_stat",
    "drivers/usb/dwc3",
]
DISALLOWED_PIXEL_SUBDIRS = ["common-staging", "common-bringup"]
DISALLOWED_PLATFORM_SUBDIRS = [
    "external/nanopb-c",
    "vendor/google/firmware/tools/gem5test",
]
DISALLOWED_PREBUILTS_SUBDIRS = ["gki"]
ALLOWED_DEVICES = [
    "common",
    "gs201",
    "pantah",
    "felix",
    "lynx",
    "tangorpro",
    "zuma",
    "shusky",
    "akita",
    "zumapro",
    "caimito",
    "comet",
    "tegu",
    "stallion",
]

# CONFIGs not defined due to missing or not built repositories
SUPPRESSED_CONFIGS = [
    "CONFIG_AOC_USB_AUDIO_OFFLOAD",
    "CONFIG_AOC_DRIVER",
    "CONFIG_AOC_V1",
    "CONFIG_AOC_ALSA_USB",
    "CONFIG_AOC_ALSA_INCALL_CAP_3",
    "CONFIG_GOOGLE_LOGBUFFER",
    "CONFIG_GS_DRM_PANEL_UNIFIED",
    "CONFIG_DRM_SAMSUNG",
    "CONFIG_DSIM_LOGBUFF",
    "CONFIG_DRM_SAMSUNG_DP",
    "CONFIG_DRM_SAMSUNG_DP_AUDIO",
    "CONFIG_DRM_SAMSUNG_DP_ZUMA",
    "CONFIG_DRM_SAMSUNG_CAL_9865",
    "CONFIG_DRM_PANEL_GOOGLE_COMMON",
    "CONFIG_DRM_PANEL_SAMSUNG_COMMON",
    "CONFIG_DRM_PANEL_SAMSUNG_EMUL",
    "CONFIG_DRM_PANEL_SAMSUNG_S6E3HC2",
    "CONFIG_DRM_PANEL_SAMSUNG_S6E3FC3",
    "CONFIG_DRM_PANEL_SAMSUNG_S6E3FC3_P10",
    "CONFIG_DRM_PANEL_SAMSUNG_SOFEF01",
    "CONFIG_DRM_PANEL_SAMSUNG_S6E3HC3",
    "CONFIG_DRM_PANEL_SAMSUNG_S6E3HC3_C10",
    "CONFIG_DRM_PANEL_SAMSUNG_S6E3HC4",
    "CONFIG_DRM_PANEL_BOE_NT37290",
    "CONFIG_FPS_TOUCH_HANDLER",
    "CONFIG_PIXEL_POWER_REBOOT",
    "CONFIG_TRUSTY",
    "CONFIG_TRUSTY_DMA_BUF_FFA_TAG",
    "CONFIG_TRUSTY_CRASH_IS_PANIC",
    "CONFIG_EXYNOS_MODEM_IF",
    "CONFIG_SEC_MODEM_S5100",
    "CONFIG_SHM_IPC",
    "CONFIG_CP_PKTPROC",
    "CONFIG_CP_PKTPROC_UL",
    "CONFIG_MODEM_IF_QOS",
    "CONFIG_CPIF_AP_SUSPEND_DURING_VOICE_CALL",
    "CONFIG_CPIF_TP_MONITOR",
    "CONFIG_LINK_DEVICE_PCIE_IOCC",
    "CONFIG_LINK_DEVICE_PCIE_IOMMU",
    "CONFIG_CH_EXTENSION",
    "CONFIG_CP_THERMAL",
    "CONFIG_TOUCHSCREEN_TBN",
    "CONFIG_TOUCHSCREEN_TBN_AOC_CHANNEL_MODE",
    "CONFIG_TOUCHSCREEN_OFFLOAD",
    "CONFIG_GOOG_TOUCH_INTERFACE",
    "CONFIG_TOUCHSCREEN_FTS",
    "CONFIG_TOUCHSCREEN_SEC_TS",
    "CONFIG_TOUCHSCREEN_HEATMAP",
    "CONFIG_DEBUG_PANEL_TEST",
    "CONFIG_GOOGLE_DRM_BRIDGE_MODE_SET",
    "CONFIG_GS_PANEL_SIMPLE",
    "CONFIG_COMMON_PANEL_TEST",
    "CONFIG_QCOM_QBT_HANDLER",
    "CONFIG_LINK_DEVICE_PCIE_SOC_EXYNOS",
    "CONFIG_CP_PMIC",
]


def acquire_manifest():
    subprocess.check_call(
        [
            "repo",
            "init",
            "-u",
            "{}://partner-android.googlesource.com/kernel-pixel/manifest"
            .format(PROTOCOL),
            "-b",
            MANIFEST_BRANCH,
        ],
        cwd=WORKSPACE,
    )


def fix_manifest():
    tree = xml.etree.ElementTree.parse(
        os.path.join(WORKSPACE, ".repo", "manifests", "default.xml")
    )
    root = tree.getroot()
    assert root.tag == "manifest"

    def apply_allow_list(pattern, allow_list):
        for project in root.findall("project"):
            name = project.attrib["name"]
            if pattern in name:
                found = False
                for am in allow_list:
                    if pattern + "/" + am in name:
                        found = True
                        break
                if not found:
                    root.remove(project)

    def apply_disallow_list(pattern, disallow_list):
        for project in root.findall("project"):
            name = project.attrib["name"]
            if pattern in name:
                found = False
                for am in disallow_list:
                    if pattern + "/" + am in name:
                        found = True
                        break
                if found:
                    root.remove(project)

    apply_allow_list("google-modules", ALLOWED_MODULES)
    apply_allow_list("devices/google", ALLOWED_DEVICES)
    apply_disallow_list("kernel-pixel", DISALLOWED_PIXEL_SUBDIRS)
    apply_disallow_list("platform", DISALLOWED_PLATFORM_SUBDIRS)
    apply_disallow_list("prebuilts", DISALLOWED_PREBUILTS_SUBDIRS)

    tree.write(os.path.join(WORKSPACE, ".repo", "manifests", "default.xml"))


def sync_manifest():
    subprocess.check_call(["repo", "init", "-m", "default.xml"], cwd=WORKSPACE)
    subprocess.check_call(["repo", "sync"], cwd=WORKSPACE)


def patch_path(path, patcher):
    with open(path, "r") as f:
        lines = f.readlines()
    with open(path, "w") as f:
        for line in lines:
            patched_line = patcher(line)
            if patched_line is not None:
                f.write(patched_line)


def patch_unneeded_dependencies():
    devices_path = os.path.join(WORKSPACE, "private", "devices", "google")
    paths_to_patch = []
    patterns = [
        os.path.join(devices_path, "*", "BUILD.bazel"),
        os.path.join(devices_path, "*", "device.bazelrc"),
        os.path.join(devices_path, "*", "Kconfig.ext.*"),
        os.path.join(devices_path, "*", "constants.bzl"),
    ]
    for pattern in patterns:
        paths_to_patch.extend(glob.glob(pattern))
    pattern_soc_gs = os.path.join(
        WORKSPACE, "private", "google-modules", "soc", "gs", "**", "BUILD.bazel"
    )
    paths_to_patch.extend(glob.glob(pattern_soc_gs, recursive=True))
    paths_to_patch.extend([
        os.path.join(
            WORKSPACE, "private", "google-modules", "soc", "gs", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "google-modules", "gpu", "mali_pixel", "BUILD.bazel"
        ),
    ])

    allowed_module_strings = []
    for m in ALLOWED_MODULES:
        allowed_module_strings.append("google-modules/" + m)

    module_re = re.compile(r"private/google-modules/[a-z]")

    def patcher(line):
        if not module_re.search(line):
            return line

        for disallowed in DISALLOWED_SOC_GS_MODULES:
            if disallowed in line and ":kconfig" not in line:
                return None

        for allowed in allowed_module_strings:
            if allowed in line:
                return line
        return None

    for p in paths_to_patch:
        patch_path(p, patcher)

def patch_constants_bzl():
    devices_path = os.path.join(WORKSPACE, "private", "devices", "google")
    pattern = os.path.join(devices_path, "*", "constants.bzl")
    paths = glob.glob(pattern)

    def patcher(line):
        if "drivers/gpu/drm/display" in line:
            return None
        return line

    for p in paths:
        patch_path(p, patcher)

def patch_defconfig():
    defconfig_paths = []
    pattern = os.path.join(
        WORKSPACE, "private", "devices", "google", "*", "*_defconfig"
    )
    defconfig_paths.extend(glob.glob(pattern))

    def patcher(line):
        for c in SUPPRESSED_CONFIGS:
            if c in line:
                return None
        return line

    for defconfig_path in defconfig_paths:
        patch_path(defconfig_path, patcher)


def patch_bms():
    def patcher(line):
        if "google-modules/bms" in line:
            return None
        return line

    path = os.path.join(
        WORKSPACE, "private", "google-modules", "power", "mitigation", "BUILD.bazel"
    )
    patch_path(path, patcher)

# Disable errors on modpost linking failures to reduce number of dependencies
def patch_modpost():
    paths = [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "gs201", "build.config.gs201"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "zuma", "build.config.zuma"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "zumapro", "build.config.zumapro"
        ),
    ]
    for path in paths:
        with open(path, "a") as f:
            f.write("export KBUILD_MODPOST_WARN=1\n")


def patch_gsa():
    def patcher(line):
        if "gsa:gsa_ko" in line:
            return None
        return line

    paths = [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "gs201", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "zuma", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "zumapro", "BUILD.bazel"
        ),
    ]
    for path in paths:
        patch_path(path, patcher)


# Keep power/mitigation headers, but don't build it together with mali_kbase
def patch_bcl():
    def patcher(line):
        if "mitigation:google_bcl" in line:
            return None
        return line

    path = os.path.join(
        WORKSPACE, "private", "google-modules", "gpu", "mali_kbase", "BUILD.bazel"
    )
    patch_path(path, patcher)

# Force kernel_package to AOSP if defined otherwise
def patch_kernel_package():
    def patcher(line):
        target = "--kernel_package=@//"
        if target in line:
            return line.split(target)[0] + target + "aosp\n"
        return line

    paths = [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "gs201", "device.bazelrc"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "zuma", "device.bazelrc"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "zumapro", "device.bazelrc"
        ),
    ]
    for path in paths:
        patch_path(path, patcher)


def check_workspace():
    assert os.path.exists(os.path.join(WORKSPACE, "tools", "bazel"))
    assert os.path.exists(
        os.path.join(WORKSPACE, "private", "google-modules", "gpu")
    )
    assert os.path.exists(
        os.path.join(WORKSPACE, "private", "devices", "google")
    )


def patch_workspace():
    patch_unneeded_dependencies()
    patch_constants_bzl()
    patch_defconfig()
    patch_bms()
    patch_gsa()
    patch_bcl()
    patch_modpost()
    patch_kernel_package()


def build_workspace(build_config):
    if build_config is None:
        print(
            "The --build_config {pantah, felix, lynx, tangorpro, shusky, akita,"
            " caimito, comet,tegu, stallion} argument is required when mode is"
            ' "build".',
            file=sys.stderr,
        )
        sys.exit(1)
    subprocess.check_call(
        [
            os.path.join("tools", "bazel"),
            "build",
            "--config=stamp",
            "--config=" + build_config,
            "//private/google-modules/gpu/mali_kbase",
        ],
        cwd=WORKSPACE,
    )


PARSER = argparse.ArgumentParser(
    description="Prepare and checkout manifests for building powervr"
)
PARSER.add_argument(
    "mode", choices=["acquire", "fix", "sync", "patch", "build"]
)
PARSER.add_argument(
    "--workspace", action="store", help="Directory to use as repo root"
)
PARSER.add_argument(
    "--protocol",
    action="store",
    default="https",
    help="Protocol to use when checking out manifest, e.g. {https, sso}",
)
PARSER.add_argument(
    "--build_config",
    choices=[
        "pantah",
        "felix",
        "lynx",
        "tangorpro",
        "shusky",
        "akita",
        "caimito",
        "comet",
        "tegu",
        "stallion",
    ],
)
ARGS = PARSER.parse_args()

if ARGS.workspace is not None:
    WORKSPACE = ARGS.workspace
    if not os.path.isdir(WORKSPACE):
        os.mkdir(WORKSPACE)

PROTOCOL = ARGS.protocol

match ARGS.mode:
    case "acquire":
        acquire_manifest()
    case "fix":
        fix_manifest()
    case "sync":
        sync_manifest()
    case "patch":
        check_workspace()
        patch_workspace()
    case "build":
        check_workspace()
        build_workspace(ARGS.build_config)
