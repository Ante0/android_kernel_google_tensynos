# SPDX-License-Identifier: GPL-2.0-only

"""
Defines helper functions for creating kernel target aliases that can dynamically
switch between building from different sources and using prebuilt artifacts.
"""

load("@bazel_skylib//lib:selects.bzl", "selects")
load("@bazel_skylib//rules:common_settings.bzl", "bool_flag", "string_flag")
load(":create_file.bzl", "create_file")

def _create_prebuilt_target(name, pkg, target):
    """Creates an alias for a specific prebuilt kernel target.

     This function handles the logic for different types of kernel artifacts,
     such as the kernel image, modules, headers, and system_dlkm images, and
     also handles different page sizes.

     Args:
         name: The base name for the kernel selection rule (e.g., "kernel_package").
         pkg: The kernel_package struct containing prebuilt repo information.
         target: The specific kernel target to create (e.g., "kernel_aarch64").

     Returns:
         The label of the created target if a prebuilt target was configured.
         None if there is no prebuilt repos or if the target does not have a prebuilt.
     """

    prefix = "{}/{}_prebuilt".format(name, pkg.name)
    actual = {}
    for build, repo in pkg.prebuilt_repos.items():
        if repo != "download_or_build":
            fail("This branch does not support prebuilt repo. Use \"download_or_build\" instead.")

        if target in (
            "kernel_aarch64",
            "kernel_aarch64_gki_artifacts",
            "kernel_aarch64_headers",
            "kernel_aarch64_images",
        ):
            actual[build] = "{}:{}_download_or_build".format(
                pkg.package,
                target.replace("kernel_aarch64", build),
            )

    if not actual:
        return None

    native.alias(
        name = "{}/{}".format(prefix, target),
        actual = select({
            "//build/kernel/kleaf:page_size_16k": actual["kernel_aarch64_16k"],
            "//conditions:default": actual["kernel_aarch64"],
        }),
    )
    return ":{}/{}".format(prefix, target)

def _create_target(name, pkg, target):
    """Creates an alias that can switch between a source and prebuilt target.

    This function defines an alias that points to the prebuilt target if the
    `{name}_use_prebuilts` config_setting is true, and falls back to the
    source target otherwise.

    Args:
        name: The base name for the kernel selection rule (e.g., "kernel_package").
        pkg: The kernel_package struct containing source and prebuilt info.
        target: The specific kernel target to create (e.g., "kernel_aarch64").

    Returns:
        The label of the created alias.
    """
    prefix = "{}/{}".format(name, pkg.name)
    source_target = "{}:{}".format(pkg.package, target)
    prebuilt_target = _create_prebuilt_target(name, pkg, target)

    actual_target = source_target
    if prebuilt_target:
        actual_target = select({
            ":{}_use_prebuilts".format(name): prebuilt_target,
            "//conditions:default": source_target,
        })

    native.alias(
        name = "{}/{}".format(prefix, target),
        actual = actual_target,
    )

    return ":{}/{}".format(prefix, target)

def _to_word(s, sep):
    """Replace non alphanumeric characters with the given sep."""
    return "".join([c if c.isalnum() else sep for c in s.elems()])

def _kconfig_symbol(name, pkg):
    """Return the kconfig symbol for a package.

    Args:
        name: The base name for the kernel selection rule (e.g., "kernel_package").
        pkg: The kernel_package struct containing source and prebuilt info.
    """
    return "{}_IS_{}".format(
        _to_word(name, "_").upper(),
        _to_word(pkg.name, "_").upper(),
    )

def kernel_package(name, package, prebuilt_repos = {}, aliases = []):
    """Create a kernel package item to be used in kernel_selection().

    Args:
        name: Kernel package name.
        package: The actual kernel source packages, e.g. "//common/ack".
        prebuilt_repos: Prebuilt repos defined using kernel_prebuilt_ext.declare_kernel_prebuilts,
            e.g. { "kernel_aarch64": "@gki_prebuilts", "kernel_aarch64_16k": "@gki_prebuilts_16k" }
        aliases: Aliases which also refer to this kernel package.
    """
    return struct(
        name = name,
        package = package,
        prebuilt_repos = prebuilt_repos,
        aliases = aliases,
    )

def kernel_selection(name, packages, targets, default = ""):
    """Define the flag to select between kernel packages.

    This function defines a string flag which can be used to select between kernel packages.
    All `targets` are aliased to the one in the selected kernel package.

    Args:
        name: Name of the flag used to select the kernel package.
        packages: Selectable kernel packages created by kernel_package().
        targets: List of targets that will be aliased to the ones in the selected kernel package.
        default: Name of the default package.
    """
    all_flag_values = []
    for pkg in packages:
        flag_values = [pkg.name] + pkg.aliases
        for idx, value in enumerate(flag_values):
            native.config_setting(
                name = "{}_is_{}_{}".format(name, pkg.name, idx),
                flag_values = {
                    name: value,
                },
            )
        selects.config_setting_group(
            name = "{}_is_{}".format(name, pkg.name),
            match_any = [
                ":{}_is_{}_{}".format(name, pkg.name, idx)
                for idx in range(len(flag_values))
            ],
        )
        all_flag_values += flag_values

    string_flag(
        name = name,
        build_setting_default = default,
        values = all_flag_values,
    )

    bool_flag(
        name = "{}_use_prebuilt_kernel".format(name),
        build_setting_default = True,
    )

    native.config_setting(
        name = "{}_use_prebuilts".format(name),
        flag_values = {
            ":{}_use_prebuilt_kernel".format(name): "true",
        },
    )

    for target in targets:
        native.alias(
            name = "{}".format(target),
            actual = select({
                ":{}_is_{}".format(name, pkg.name): _create_target(name, pkg, target)
                for pkg in packages
            }),
        )

    create_file(
        name = "{}_kconfig".format(name),
        out = "Kconfig.{}".format(name),
        contents = [
            'menu "{}"'.format(_to_word(name, " ").title()),
            'choice\n\tprompt "{} selection"\n'.format(name),
        ] + [
            'config {}\n\tbool "{} is {}"\n'.format(
                _kconfig_symbol(name, pkg),
                name,
                pkg.name,
            )
            for pkg in packages
        ] + [
            "endchoice",
            "endmenu",
        ],
    )

    create_file(
        name = "{}_defconfig_fragment".format(name),
        out = "{}_defconfig".format(name),
        contents = select({
            ":{}_is_{}".format(name, pkg.name): [
                "CONFIG_{}=y".format(_kconfig_symbol(name, pkg)),
            ]
            for pkg in packages
        }),
    )
