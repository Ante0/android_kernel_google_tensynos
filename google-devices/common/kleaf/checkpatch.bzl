# SPDX-License-Identifier: GPL-2.0-only

"""
Wrappers to the kleaf checkpatch() rule.
"""

load("@kleaf//build/kernel/kleaf:kernel.bzl", "checkpatch")
load(":create_file.bzl", "create_file")

CHECKPATCH_DEFAULT_IGNORES = [
    "GERRIT_CHANGE_ID",
    "BAD_FIXES_TAG",
    "GIT_COMMIT_ID",
    "UNKNOWN_COMMIT_ID",
    "FILE_PATH_CHANGES",
]

def checkpatch_strict(
        name,
        checkpatch_pl = "//common:scripts/checkpatch.pl",
        strict = True,
        ignores = []):
    """A wrapper around checkpatch() with strict mode and a default ignorelist.

    Args:
        name: name of the rule.
        checkpatch_pl: path to checkpatch.pl.
        strict: whether to use strict mode.
        ignores: additional types to ignore, appended to `CHECKPATCH_DEFAULT_IGNORES`.
    """

    create_file(
        name = "{}_ignorelist".format(name),
        out = "{}_ignorelist.txt".format(name),
        contents = CHECKPATCH_DEFAULT_IGNORES + ignores,
    )

    checkpatch(
        name = name,
        checkpatch_pl = checkpatch_pl,
        ignorelist = "{}_ignorelist".format(name),
        strict = strict,
    )
