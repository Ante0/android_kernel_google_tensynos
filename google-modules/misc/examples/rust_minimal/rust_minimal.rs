// SPDX-License-Identifier: GPL-2.0

//! Rust minimal sample for DDK.

use kernel::prelude::*;

module! {
    type: RustMinimal,
    name: "rust_minimal",
    author: "Google",
    description: "Rust minimal sample for DDK",
    license: "GPL",
}

struct RustMinimal;

impl kernel::Module for RustMinimal {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        // Use pr_warn instead of pr_info because INFO symbols might be trimmed
        // from the kernel's Module.symvers if not used by any in-tree code.
        pr_warn!("Rust minimal sample (init)\n");
        Ok(RustMinimal)
    }
}

impl Drop for RustMinimal {
    fn drop(&mut self) {
        pr_warn!("Rust minimal sample (exit)\n");
    }
}
