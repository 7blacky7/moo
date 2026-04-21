// build.rs — moo Runtime C-Build + Plattform-spezifisches UI-Modul.
//
// Cross-Platform-Strategie (Plan plan-002-moo-ui-cross-platform):
//   Linux   → runtime/moo_ui_gtk.c    + runtime/moo_tray_linux.c
//   Windows → runtime/moo_ui_win32.c  + runtime/moo_tray_win32.c  (Phase 3)
//   macOS   → runtime/moo_ui_cocoa.m  + runtime/moo_tray_cocoa.m  (Phase 4)
//
// Phase 1 liefert nur den Linux-Pfad funktional; die Cross-Platform-
// Struktur (target_os-Zweige, cargo-Features) steht bereits.

fn main() {
    let mut build = cc::Build::new();

    // ========================================================================
    // Kern-Runtime (OS-neutral)
    // ========================================================================
    build
        .file("runtime/moo_value.c")
        .file("runtime/moo_memory.c")
        .file("runtime/moo_string.c")
        .file("runtime/moo_list.c")
        .file("runtime/moo_dict.c")
        .file("runtime/moo_ops.c")
        .file("runtime/moo_print.c")
        .file("runtime/moo_error.c")
        .file("runtime/moo_object.c")
        .file("runtime/moo_stdlib.c")
        .file("runtime/moo_file.c")
        .file("runtime/moo_thread.c")
        .file("runtime/moo_json.c")
        .file("runtime/moo_http.c")
        .file("runtime/moo_crypto.c")
        .file("runtime/moo_db.c")
        .file("runtime/moo_result.c")
        .file("runtime/moo_graphics.c")
        .file("runtime/moo_3d.c")
        .file("runtime/moo_3d_math.c")
        .file("runtime/moo_regex.c")
        .file("runtime/moo_core.c")
        .file("runtime/moo_net.c")
        .file("runtime/moo_web.c")
        .file("runtime/moo_eval.c")
        .file("runtime/moo_profiler.c")
        .file("runtime/moo_world.c")
        .file("runtime/moo_sprite.c")
        .include("runtime")
        .include("/usr/include/SDL2")
        .opt_level(2)
        .flag_if_supported("-fPIC");

    // ========================================================================
    // UI-Modul: Plattform-spezifisches Backend
    //
    // Feature-Flag `moo_ui` ist default-on. Kann mit
    //   cargo build --no-default-features
    // deaktiviert werden (kein UI, kein Tray → keine GTK/appindicator-Dep).
    // ========================================================================
    let ui_enabled = cfg!(feature = "moo_ui");

    if ui_enabled {
        if cfg!(target_os = "linux") {
            build_ui_linux(&mut build);
        } else if cfg!(target_os = "windows") {
            build_ui_windows(&mut build);
        } else if cfg!(target_os = "macos") {
            build_ui_macos(&mut build);
        } else {
            println!(
                "cargo:warning=moo_ui: Unbekannte Zielplattform, UI-Backend wird ausgelassen."
            );
        }
    }

    // ========================================================================
    // 3D Backends: bedingt in denselben Build einfuegen
    // ========================================================================
    #[cfg(feature = "gl21")]
    {
        build.file("runtime/moo_3d_gl21.c");
        build.define("MOO_HAS_GL21", None);
    }

    #[cfg(feature = "gl33")]
    {
        build.file("runtime/moo_3d_gl33.c");
        build.file("runtime/moo_3d_gl33_mesh.c");
        build.file("runtime/moo_hybrid.c");
        build.file("runtime/glad/src/glad.c");
        build.include("runtime/glad/include");
        build.define("MOO_HAS_GL33", None);
    }

    #[cfg(feature = "vulkan")]
    {
        build.file("runtime/moo_3d_vulkan.c");
        build.file("runtime/moo_3d_vulkan_mem.c");
        build.define("MOO_HAS_VULKAN", None);
    }

    build.compile("moo_runtime");

    // ========================================================================
    // Core Links (alle Plattformen)
    // ========================================================================
    println!("cargo:rustc-link-lib=curl");
    println!("cargo:rustc-link-lib=sqlite3");
    println!("cargo:rustc-link-lib=SDL2");
    println!("cargo:rustc-link-lib=SDL2_image");

    // UI-Links (plattform-spezifisch)
    if ui_enabled {
        if cfg!(target_os = "linux") {
            link_ui_linux();
        } else if cfg!(target_os = "windows") {
            link_ui_windows();
        } else if cfg!(target_os = "macos") {
            link_ui_macos();
        }
    }

    // 3D Backend: bedingt linken
    #[cfg(feature = "gl21")]
    {
        println!("cargo:rustc-link-lib=GL");
        println!("cargo:rustc-link-lib=glfw");
    }
    #[cfg(feature = "gl33")]
    {
        println!("cargo:rustc-link-lib=GL");
        println!("cargo:rustc-link-lib=glfw");
    }
    #[cfg(feature = "vulkan")]
    {
        println!("cargo:rustc-link-lib=vulkan");
        println!("cargo:rustc-link-lib=glfw");
    }

    println!("cargo:rerun-if-changed=runtime/");
    println!("cargo:rerun-if-changed=runtime/moo_ui.h");
    println!("cargo:rerun-if-changed=runtime/moo_tray.h");
    println!("cargo:rerun-if-changed=runtime/moo_3d_vulkan_vert_spv.h");
    println!("cargo:rerun-if-changed=runtime/moo_3d_vulkan_frag_spv.h");
}

// ============================================================================
// Linux — GTK3 + libappindicator3
// ============================================================================
#[cfg(target_os = "linux")]
fn build_ui_linux(build: &mut cc::Build) {
    // Cross-Platform-Backends (Linux):
    build.file("runtime/moo_ui_gtk.c");
    build.file("runtime/moo_tray_linux.c");

    // Include-Pfade via pkg-config ermitteln (portabler als hardcoded
    // /usr/include/gtk-3.0 etc.).
    match pkg_config::Config::new()
        .atleast_version("3.0")
        .probe("gtk+-3.0")
    {
        Ok(lib) => {
            for inc in &lib.include_paths {
                build.include(inc);
            }
        }
        Err(e) => {
            println!("cargo:warning=pkg-config gtk+-3.0 fehlgeschlagen ({e}); Fallback auf Hardcoded-Pfade.");
            fallback_linux_includes(build);
        }
    }

    match pkg_config::Config::new().probe("appindicator3-0.1") {
        Ok(lib) => {
            for inc in &lib.include_paths {
                build.include(inc);
            }
        }
        Err(e) => {
            println!(
                "cargo:warning=pkg-config appindicator3-0.1 fehlgeschlagen ({e}); Fallback-Include-Pfade."
            );
        }
    }
}

#[cfg(not(target_os = "linux"))]
fn build_ui_linux(_build: &mut cc::Build) {}

#[cfg(target_os = "linux")]
fn fallback_linux_includes(build: &mut cc::Build) {
    for inc in &[
        "/usr/include/libappindicator3-0.1",
        "/usr/include/libdbusmenu-glib-0.4",
        "/usr/include/gtk-3.0",
        "/usr/include/pango-1.0",
        "/usr/include/cairo",
        "/usr/include/gdk-pixbuf-2.0",
        "/usr/include/atk-1.0",
        "/usr/include/harfbuzz",
        "/usr/include/glib-2.0",
        "/usr/lib/glib-2.0/include",
        "/usr/include/gio-unix-2.0",
        "/usr/include/fribidi",
        "/usr/include/pixman-1",
        "/usr/include/freetype2",
        "/usr/include/libpng16",
        "/usr/include/at-spi2-atk/2.0",
        "/usr/include/at-spi-2.0",
        "/usr/include/dbus-1.0",
        "/usr/lib/dbus-1.0/include",
        "/usr/include/cloudproviders",
    ] {
        build.include(inc);
    }
}

#[cfg(target_os = "linux")]
fn link_ui_linux() {
    // Bevorzugt pkg-config → emittiert cargo:rustc-link-lib Zeilen selbst.
    let gtk = pkg_config::Config::new()
        .atleast_version("3.0")
        .probe("gtk+-3.0");
    let ind = pkg_config::Config::new().probe("appindicator3-0.1");
    // cairo wird direkt von moo_ui_gtk.c (Leinwand/Zeichner-API) benoetigt —
    // gtk+-3.0 zieht es zwar transitiv, der Linker will es aber explizit.
    let _cairo = pkg_config::Config::new().probe("cairo");

    if gtk.is_err() || ind.is_err() {
        // Fallback-Liste (alte Hard-Codings).
        for l in &[
            "appindicator3",
            "dbusmenu-glib",
            "gtk-3",
            "gdk-3",
            "gio-2.0",
            "gobject-2.0",
            "glib-2.0",
            "cairo",
        ] {
            println!("cargo:rustc-link-lib={l}");
        }
    }
}

#[cfg(not(target_os = "linux"))]
fn link_ui_linux() {}

// ============================================================================
// Windows — Win32 API (Phase 3)
// ============================================================================
#[cfg(target_os = "windows")]
fn build_ui_windows(build: &mut cc::Build) {
    build.file("runtime/moo_ui_win32.c");
    build.file("runtime/moo_tray_win32.c");
    build.define("MOO_UI_WIN32", None);
}

#[cfg(not(target_os = "windows"))]
fn build_ui_windows(_build: &mut cc::Build) {}

#[cfg(target_os = "windows")]
fn link_ui_windows() {
    for l in &["user32", "gdi32", "comctl32", "comdlg32", "shell32", "ole32", "uxtheme"] {
        println!("cargo:rustc-link-lib={l}");
    }
}

#[cfg(not(target_os = "windows"))]
fn link_ui_windows() {}

// ============================================================================
// macOS — Cocoa (Phase 4)
// ============================================================================
#[cfg(target_os = "macos")]
fn build_ui_macos(build: &mut cc::Build) {
    // cc-crate erkennt `.m` am Suffix und compiliert mit Objective-C.
    build.file("runtime/moo_ui_cocoa.m");
    build.file("runtime/moo_tray_cocoa.m");
    build.flag("-fobjc-arc");
    build.define("MOO_UI_COCOA", None);
}

#[cfg(not(target_os = "macos"))]
fn build_ui_macos(_build: &mut cc::Build) {}

#[cfg(target_os = "macos")]
fn link_ui_macos() {
    for fw in &["Cocoa", "AppKit", "Foundation", "CoreGraphics"] {
        println!("cargo:rustc-link-lib=framework={fw}");
    }
}

#[cfg(not(target_os = "macos"))]
fn link_ui_macos() {}
