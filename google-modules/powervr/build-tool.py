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

ALLOWED_MODULES = [
    "powervr",
    "trusty",
    "soc",
    "perf",
    "iif",
    "power/mitigation",
]
ALLOWED_DEVICES = ["common", "lga", "muzel", "malibu", "spacecraft"]
DISALLOWED_KOS = ["dwc3-google"]
DISALLOWED_PIXEL_SUBDIRS = ["common-staging", "common-bringup"]
DISALLOWED_PLATFORM_SUBDIRS = [
    "external/nanopb-c",
    "vendor/google/firmware/tools/gem5test",
]
SUPPRESSED_CONFIGS = [
    "CONFIG_CODEC3P",
    "CONFIG_DRM_DW_MIPI_CDPHY",
    "CONFIG_DRM_DW_MIPI_DSI2H",
    "CONFIG_DRM_G2D",
    "CONFIG_DRM_VERISILICON",
    "CONFIG_GS_DRM_PANEL_UNIFIED",
    "CONFIG_GS_PANEL_SIMPLE",
    "CONFIG_LWIS",
    "CONFIG_VERISILICON_CHIP_9x00",
    "CONFIG_VERISILICON_DC9400_0x32a",
    "CONFIG_VERISILICON_MIPI_DSI2H",
    "CONFIG_ARM_SMMU_V3_PIXEL",
    "CONFIG_ARM_SMMU_V3_PKVM_PIXEL",
    "CONFIG_AOC_DRIVER",
    "CONFIG_AOC_ALSA_DP_AUDIO",
    "CONFIG_AOC_ALSA_INCALL_CAP_3",
    "CONFIG_AOC_ALSA_USB",
    "CONFIG_AOC_USB_AUDIO_OFFLOAD",
    "CONFIG_CH_EXTENSION",
    "CONFIG_COMMON_PANEL_TEST",
    "CONFIG_CPIF_AP_SUSPEND_DURING_VOICE_CALL",
    "CONFIG_CPIF_PAGE_RECYCLING",
    "CONFIG_CPIF_TP_MONITOR",
    "CONFIG_LINK_DEVICE_PCIE_IOMMU",
    "CONFIG_CP_PKTPROC",
    "CONFIG_CP_PKTPROC_UL",
    "CONFIG_CP_PMIC",
    "CONFIG_CP_THERMAL",
    "CONFIG_DEBUG_PANEL_TEST",
    "CONFIG_DWC_DPTX",
    "CONFIG_DWC_DPTX_AUDIO",
    "CONFIG_EXYNOS_MODEM_IF",
    "CONFIG_GOOGLE_H2OMG",
    "CONFIG_GOOGLE_LOGBUFFER",
    "CONFIG_GOOGLE_VOTABLE",
    "CONFIG_GS_PANEL_S6E3HC4",
    "CONFIG_LINK_DEVICE_PCIE",
    "CONFIG_LINK_DEVICE_PCIE_IOCC",
    "CONFIG_LINK_DEVICE_PCIE_SOC_GOOGLE",
    "CONFIG_METRICS_COLLECTION_FRAMEWORK",
    "CONFIG_MODEM_IF_QOS",
    "CONFIG_QCOM_QBT_HANDLER",
    "CONFIG_SEC_MODEM_S5100",
    "CONFIG_SHM_IPC",
    "CONFIG_TOUCHSCREEN_TBN",
    "CONFIG_TOUCHSCREEN_TBN_AOC_CHANNEL_MODE",
    "CONFIG_TOUCHSCREEN_HEATMAP",
    "CONFIG_TOUCHSCREEN_OFFLOAD",
    "CONFIG_GOOG_TOUCH_INTERFACE",
    "CONFIG_VERISILICON_DC9400_0x316",
    "CONFIG_ARM_SMMU_V3_PMU_PIXEL",
    "CONFIG_AOSS_SSR_NOTIFIER",
    # suppress all USB drivers in soc which depend on bms/misc code
    "CONFIG_TYPEC_FUSB307",
    "CONFIG_USB_PSY",
    "CONFIG_TYPEC_MAX77759",
    "CONFIG_TYPEC_MAX77759",
    "CONFIG_TYPEC_MAX77759_CONTAMINANT",
    "CONFIG_TYPEC_MAX77779_CONTAMINANT",
    "CONFIG_TYPEC_MAX777X9_I2C",
    "CONFIG_TYPEC_MAX777X9_SPMI",
    "CONFIG_POGO_TRANSPORT",
    "CONFIG_TYPEC_COOLING_DEV",
    "CONFIG_GOOGLE_USB_ROLE_SW",
    "CONFIG_TCPCI_VENDOR_HOOKS",
    # suppress AOC
    "CONFIG_AOC_LGA",
    # power controller also depends on bms/misc
    "CONFIG_GOOGLE_POWER_CONTROLLER",
    "CONFIG_GOOGLE_ACFW_DEBUG",
    "CONFIG_PIXEL_POWER_REBOOT",
    # USB driver is broken
    "CONFIG_USB_DWC3_GOOGLE",
    # Malibu-specific
    "CONFIG_GOOGLE_SYSPM",
    "CONFIG_VERISILICON_DC9400_0x20000005",
    "CONFIG_AOC_MBU",
    "CONFIG_GOOGLE_WWAN_PKT_PRIO",
    "CONFIG_MTK_WWAN_PWRCTL_SUPPORT",
    "CONFIG_MTK_HOST_DRV_WWAN_SUPPORT",
    "CONFIG_MTK_DATA_CPU_LOADING_OPTIMIZE",
    "CONFIG_MTK_DEVLINK",
    "CONFIG_MTK_DEVLINK_LOGDUMP_SUPPORT",
    "CONFIG_TOUCHSCREEN_SEC_TS",
    "CONFIG_GOOGLE_RADIO_BRIDGE",
    "CONFIG_VERISILICON_ONE_LAYER_G2D",
    "CONFIG_AOC_ALSA_IAMF",
    "CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR",
    "CONFIG_GOOGLE_MODEM_CDD",
    "CONFIG_GOOGLE_MODEM_SOC_BW_QOS",
    "CONFIG_MTK_MEMLOG_EVENT_SUPPORT",
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


def patch_kernel_module_bzl():
    def patcher(line):
        if "if not" in line and "exclude_kernel_build_module_srcs" in line:
            return None
        if "module_hdrs" in line:
            return None
        return line

    patch_path(
        os.path.join(WORKSPACE, "kernel", "kleaf", "impl", "kernel_module.bzl"),
        patcher,
    )


def patch_unneeded_dependencies():
    devices_path = os.path.join(WORKSPACE, "private", "devices", "google")
    patterns = [
        os.path.join(devices_path, "*", "BUILD.bazel"),
        os.path.join(devices_path, "*", "device.bazelrc"),
        os.path.join(devices_path, "*", "Kconfig.ext.*"),
    ]
    paths_to_patch = [
        os.path.join(
            WORKSPACE, "private", "google-modules", "soc", "rdo", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "google-modules", "soc", "gs", "BUILD.bazel"
        ),
    ]
    for pattern in patterns:
        paths_to_patch.extend(glob.glob(pattern))
    module_re = re.compile(r"private/google-modules/[a-z]")

    def patcher(line):
        if module_re.search(line):
            for m in ALLOWED_MODULES:
                if "google-modules/" + m in line:
                    return line
            return None
        if ".ko" in line:
            for m in DISALLOWED_KOS:
                if m in line:
                    return None
            return line
        return line

    for p in paths_to_patch:
        patch_path(p, patcher)


def patch_defconfig():
    defconfig_paths = [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "lga", "lga_defconfig"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "muzel", "muzel_defconfig"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "malibu", "malibu_defconfig"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "spacecraft", "spacecraft_defconfig"
        ),
    ]

    def patcher(line):
        for c in SUPPRESSED_CONFIGS:
            if c in line:
                return None
        return line

    for defconfig_path in defconfig_paths:
        patch_path(defconfig_path, patcher)


def patch_usb_kos():
    def patcher(line):
        if "drivers/usb" in line and not re.search(
            r"gadget|xhci|role-sw", line
        ):
            return None
        return line

    paths_to_patch = [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "lga", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "lga", "Kconfig.ext.lga"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "malibu", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "malibu", "Kconfig.ext.malibu"
        ),
    ]
    for p in paths_to_patch:
        patch_path(p, patcher)


def patch_drm_ko():
    def patcher(line):
        if "drivers/gpu" in line:
            return None
        return line

    for path in [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "muzel", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "lga", "constants.bzl"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "malibu", "constants.bzl"
        ),
    ]:
        patch_path(path, patcher)


def patch_power_controller_ko():
    def patcher(line):
        if "power_controller" in line:
            return None
        return line

    paths = [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "lga", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "devices", "google", "malibu", "BUILD.bazel"
        ),
        os.path.join(
            WORKSPACE, "private", "google-modules", "soc", "rdo", "drivers", "soc", "google", "dvfs-target-frontend", "BUILD.bazel"
        ),
    ]
    for p in paths:
        patch_path(p, patcher)


def patch_kernel_package():
    def patcher(line):
        target = "--kernel_package=@//"
        if target in line:
            return line.split(target)[0] + target + "aosp\n"
        return line

    lga_path = os.path.join(
        WORKSPACE, "private", "devices", "google", "lga", "device.bazelrc"
    )
    malibu_path = os.path.join(
        WORKSPACE, "private", "devices", "google", "malibu", "device.bazelrc"
    )
    patch_path(lga_path, patcher)
    patch_path(malibu_path, patcher)


# Disable errors on modpost linking failures
def patch_modpost():
    paths = [
        os.path.join(
            WORKSPACE, "private", "devices", "google", "malibu", "build.config.malibu"
        ),
    ]
    for path in paths:
        with open(path, "a") as f:
            f.write("export KBUILD_MODPOST_WARN=1\n")


def check_workspace():
    assert os.path.exists(os.path.join(WORKSPACE, "tools", "bazel"))
    assert os.path.exists(
        os.path.join(WORKSPACE, "private", "google-modules", "powervr")
    )
    assert os.path.exists(
        os.path.join(WORKSPACE, "private", "devices", "google")
    )


def patch_workspace():
    patch_unneeded_dependencies()
    patch_defconfig()
    patch_usb_kos()
    patch_drm_ko()
    patch_power_controller_ko()
    patch_modpost()
    patch_kernel_package()


def build_workspace(build_config):
    if build_config is None:
        print(
            "The --build_config {muzel,spacecraft} argument is required when"
            ' mode is "build".',
            file=sys.stderr,
        )
        sys.exit(1)
    subprocess.check_call(
        [
            os.path.join("tools", "bazel"),
            "build",
            "--config=stamp",
            "--config=" + build_config,
            "//private/google-modules/powervr",
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
PARSER.add_argument("--build_config", choices=["muzel", "spacecraft"])
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
