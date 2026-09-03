# SPDX-License-Identifier: GPL-2.0-only

"""
Extract GKI modules from GKI system_dlkm archive.
"""

load("@kleaf//build/kernel/kleaf:hermetic_tools.bzl", "hermetic_toolchain")

def _extracted_system_dlkm(ctx):
    hermetic_tools = hermetic_toolchain.get(ctx)

    system_dlkm_archive = None
    for f in ctx.files.images:
        if f.basename == "system_dlkm_staging_archive.tar.gz":
            system_dlkm_archive = f
    if not system_dlkm_archive:
        fail("system_dlkm_staging_archive.tar.gz is not found in images")

    outs = []
    for m in ctx.attr.gki_modules:
        out = ctx.actions.declare_file("{}/{}".format(ctx.attr.name, m))

        command = hermetic_tools.setup
        command += """
            tar -xf {system_dlkm_archive} \
                    --wildcards \
                    --transform='s@.*/{m}@{out}@' \
                    '**/{m}'
        """.format(
            m = m,
            out = out.path,
            system_dlkm_archive = system_dlkm_archive.path,
        )

        ctx.actions.run_shell(
            mnemonic = "ExtractedSystemDlkm",
            inputs = [system_dlkm_archive],
            outputs = [out],
            tools = hermetic_tools.deps,
            progress_message = "Extracting {}".format(m),
            command = command,
        )

        outs.append(out)

    return [DefaultInfo(files = depset(outs))]

extracted_system_dlkm = rule(
    doc = """Extracts the system_dlkm archive so that they can be copied to the dist_dir""",
    implementation = _extracted_system_dlkm,
    attrs = {
        "images": attr.label(
            doc = "The kernel_images target that contains the system_dlkm archive.",
            allow_files = True,
            mandatory = True,
        ),
        "gki_modules": attr.string_list(
            doc = "A list of GKI modules",
            mandatory = True,
        ),
    },
    toolchains = [hermetic_toolchain.type],
)
