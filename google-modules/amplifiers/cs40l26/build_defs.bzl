# SPDX-License-Identifier: GPL-2.0-only

"""Build definitions for the cs40l26 driver."""

def define_driver_versions(name, versions):
    """A macro to define version-selectable config_settings.

    Args:
        name: The name for this macro invocation (for buildifier compliance).
        versions: A list of version name strings (e.g., ["v1", "v2"]).
    """
    for v in versions:
        native.config_setting(
            name = v,
            flag_values = {"version": v},
            visibility = [
                "//private/devices/google:__subpackages__",
            ],
        )

def get_versioned_sources(versions, source_files):
    """Creates a dictionary for a select() call to choose sources based on version.

    Args:
        versions: A list of version name strings (e.g., ["v1.0", "v2.0"]).
        source_files: A list of source file names for a specific target.

    Returns:
        A dictionary formatted for use inside a select() statement. e.g.
        {":v14.5.0+v4.1.1": ["v14.5.0+v4.1.1/cs40l26.c", "v14.5.0+v4.1.1/cs40l26-i2c.c", ...],
         ":v15.5.0+v4.1.3": ["v15.5.0+v4.1.3/cs40l26.c", "v14.5.0+v4.1.1/cs40l26-i2c.c", ...]}
    """

    def _prepend_path(subfolder, files):
        """Helper to prepend a subfolder to a list of filenames."""
        return [subfolder + "/" + f for f in files]

    # Create and return the final source map
    # e.g., {":v14.5.0+v4.1.1": ["v14.5.0+v4.1.1/cs40l26.c", ...], ...}
    return {
        ":" + v: _prepend_path(v, source_files)
        for v in versions
    }
