# SPDX-License-Identifier: GPL-2.0-only

"""
Create a properties file use by build_image.
"""

def _image_props_impl(ctx):
    prop_dict = {
        "fs_type": ctx.attr.fs_type,
        "use_dynamic_partition_size": "true",
        "mount_point": ctx.attr.mount_point,
        "selinux_fc": ctx.file.selinux_fc.path,
    }

    if prop_dict.get("fs_type") == "ext4":
        prop_dict["ext_mkuserimg"] = "mkuserimg_mke2fs"
        prop_dict["ext4_share_dup_blocks"] = "true"

    ctx.actions.write(
        output = ctx.outputs.out,
        content = "\n".join(["{}={}".format(k, v) for k, v in prop_dict.items()]) + "\n",
    )

    return [DefaultInfo(files = depset([ctx.outputs.out]))]

image_props = rule(
    implementation = _image_props_impl,
    doc = "Create a properties file used by build_image.",
    attrs = {
        "out": attr.output(
            doc = "Path of the output file, relative to this package.",
            mandatory = True,
        ),
        "fs_type": attr.string(
            doc = "Filesystem for the image.",
            values = ["ext4", "erofs"],
            mandatory = True,
        ),
        "mount_point": attr.string(
            doc = "Mount point of the image.",
            mandatory = True,
        ),
        "selinux_fc": attr.label(
            allow_single_file = True,
            doc = "File contexts of the image.",
            mandatory = True,
        ),
    },
)
