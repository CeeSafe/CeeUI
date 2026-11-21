// SPDX-License-Identifier: MIT
// Links claywind-capi's archive (ceesafec build in claywind-capi): it is named claywind_capi.a, so it is passed to
// the linker by its path rather than with -l. CLAYWIND_CAPI_ARCHIVE names another one.
use std::env;
use std::path::PathBuf;

fn main() {
    let archive = match env::var("CLAYWIND_CAPI_ARCHIVE") {
        Ok(path) => PathBuf::from(path),
        Err(_) => PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("../../out/claywind_capi.a"),
    };
    let archive = archive.canonicalize().unwrap_or_else(|_| {
        panic!("no {}: run `ceesafec build` in claywind-capi first", archive.display())
    });
    println!("cargo:rerun-if-changed={}", archive.display());
    println!("cargo:rerun-if-env-changed=CLAYWIND_CAPI_ARCHIVE");
    println!("cargo:rustc-link-arg={}", archive.display());
    match env::var("CARGO_CFG_TARGET_OS").as_deref() {
        Ok("macos") => {
            for framework in ["AppKit", "QuartzCore", "CoreText", "CoreGraphics"] {
                println!("cargo:rustc-link-lib=framework={framework}");
            }
        }
        Ok("linux") => {
            // The archive is the link's last argument, after Rust's own libraries: what it needs from the C library
            // (pthread_atfork is in glibc's libc_nonshared.a) is named again after it.
            for lib in ["m", "pthread", "dl", "c"] {
                println!("cargo:rustc-link-arg=-l{lib}");
            }
        }
        _ => {}
    }
}
