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

    // P013: Target-OS korrekt ermitteln. In build.rs ist cfg!(target_os) das
    // HOST-OS des Build-Rechners; fuer Cross-Builds zaehlt CARGO_CFG_TARGET_OS.
    let target_windows = std::env::var("CARGO_CFG_TARGET_OS")
        .map(|s| s == "windows")
        .unwrap_or(cfg!(target_os = "windows"));

    // ========================================================================
    // Kern-Runtime (OS-neutral)
    // ========================================================================
    build
        .file("runtime/moo_value.c")
        .file("runtime/moo_memory.c")
        .file("runtime/moo_frame.c")
        .file("runtime/moo_frame_tensor.c") // Frame<->Tensor-Bruecke (KI-MULTI-V1, SDL-frei)
        .file("runtime/moo_capture.c")      // KI-MULTI-C1: gemeinsame Handle-/Lifecycle-Schicht
        .file("runtime/moo_audio.c")        // FFT/STFT/WAV-Reader (KI-MULTI-A1, SDL-frei)
        .file("runtime/moo_gif.c")        // GIF89a+LZW-Encoder-Kern (Plan-008 A3B, pure C)
        .file("runtime/moo_gif_handle.c") // moo-Heap-Wrapper MOO_GIF (immer gebaut)
        .file("runtime/moo_video.c")        // ffmpeg-Pipe MP4-Kern (Plan-009 V0, pure C/POSIX)
        .file("runtime/moo_video_handle.c") // moo-Heap-Wrapper MOO_VIDEO (immer gebaut)
        .file("runtime/moo_tensor.c")       // KI-Tensor-Kern MOO_TENSOR (Plan-014 A1, immer gebaut)
        .file("runtime/moo_tensor_ops.c")   // Tensor-Ops + Op-Registry (Plan-014 A2, immer gebaut)
        .file("runtime/moo_autograd.c")     // Autograd-Tape + backward (Plan-014 B1, immer gebaut)
        .file("runtime/moo_nn.c")           // NN-Schichten/Loss/Optimizer (Plan-014 C1, immer gebaut)
        .file("runtime/moo_contrastive.c")  // KI-MULTI-L1: Kosinus + InfoNCE-Komposition
        .file("runtime/moo_nn_easy.c")      // Kinderleicht-API ki_netz/trainiere (Plan-014 D1, immer gebaut)
        .file("runtime/moo_dataset.c")      // Daten-Pipeline MNIST/CSV/PGM (Plan-014 E1, immer gebaut)
        .file("runtime/moo_tokenizer.c")    // Byte-level BPE-Tokenizer (KIP-T2, immer gebaut)
        .file("runtime/moo_shard.c")        // Streaming-Token-Shards + Dataloader (KIP-E1, immer gebaut)
        .file("runtime/moo_ki_gpu.c")       // GPU2: Vulkan-Compute-Ops; ohne vulkan-Feature reiner Stub (immer gebaut)
        .file("runtime/moo_ki_gpu_statistik.c") // KIP-FINAL-FIX e413b176: MooValue-Wrapper gpu_statistik() ausgelagert (Vollruntime-only, haelt moo_ki_gpu.c standalone-linkbar fuer die GPU-Gate-Skripte)
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
        .file("runtime/moo_regex.c")
        .file("runtime/moo_core.c")
        .file("runtime/moo_net.c")
        .file("runtime/moo_web.c")
        .file("runtime/moo_eval.c")
        .file("runtime/moo_profiler.c")
        .include("runtime")
        .opt_level(2)
        .flag_if_supported("-fPIC");

    // ========================================================================
    // P013: POSIX-Regex-Vendoring fuer Windows (musl v1.2.5 / TRE) — siehe
    // runtime/win_regex/README.md. moo_regex.c schaltet per #ifdef _WIN32 auf
    // diesen Header um. Unter Linux NICHT bauen: dort liefert die System-libc
    // regcomp/regexec (Doppel-Definition vermeiden).
    // ========================================================================
    if target_windows {
        build
            .file("runtime/win_regex/regcomp.c")
            .file("runtime/win_regex/regexec.c")
            .file("runtime/win_regex/regerror.c")
            .file("runtime/win_regex/tre-mem.c")
            .include("runtime/win_regex");
    }

    // 3D/Game Runtime-Core nur bauen, wenn ein 3D-Feature aktiv ist.
    // Sonst ziehen UI-only Builds unnötig SDL/GL/Vulkan/GLFW-Symbole in
    // libmoo_runtime.a und gelinkte .moo-Programme scheitern trotz
    // `--no-default-features --features moo_ui`.
    #[cfg(any(feature = "gl21", feature = "gl33", feature = "vulkan"))]
    {
        build
            .file("runtime/moo_graphics.c")
            .file("runtime/moo_test_api.c")
            .file("runtime/moo_3d.c")
            .file("runtime/moo_3d_math.c")
            .file("runtime/moo_noise.c")
            .file("runtime/moo_world.c")
            .file("runtime/moo_voxel.c")
            .file("runtime/moo_sprite.c")
            .include("/usr/include/SDL2");
        // MOO_HAS_3D: signalisiert der OS-neutralen Kern-Runtime (moo_memory.c),
        // dass moo_graphics.c/moo_voxel.c mitgelinkt werden. Ohne 3D-Feature
        // duerfen moo_window_free/moo_voxel_free NICHT referenziert werden
        // (CI-Run 27437564015: undefined reference im UI-only-Build).
        build.define("MOO_HAS_3D", None);
    }

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

    // KI-MULTI-C1/C2: native Capture-Backends target-basiert. Windows nutzt
    // Media Foundation + WASAPI; Linux bleibt zweistufig pkg-config-gegatet.
    println!("cargo:rustc-check-cfg=cfg(moo_has_v4l2)");
    println!("cargo:rustc-check-cfg=cfg(moo_has_alsa)");
    println!("cargo:rustc-check-cfg=cfg(moo_has_windows_capture)");
    let target_linux = std::env::var("CARGO_CFG_TARGET_OS")
        .map(|s| s == "linux").unwrap_or(cfg!(target_os = "linux"));
    if target_windows {
        build
            .file("runtime/moo_capture_pull.c")
            .file("runtime/moo_capture_windows_system.c")
            .define("MOO_HAS_WINDOWS_CAPTURE", None);
        println!("cargo:rustc-cfg=moo_has_windows_capture");
    } else {
        let v4l2 = target_linux &&
            pkg_config::Config::new().probe("libv4l2").is_ok() &&
            pkg_config::Config::new().probe("libv4lconvert").is_ok();
        if v4l2 {
            build.file("runtime/moo_capture_v4l2.c");
            build.define("MOO_HAS_V4L2", None);
            println!("cargo:rustc-cfg=moo_has_v4l2");
        } else {
            build.file("runtime/moo_capture_camera_stub.c");
            println!("cargo:warning=moo capture: natives Kamera-Backend fuer dieses Ziel nicht verfuegbar; Stub wird gebaut");
        }
        let alsa = target_linux && pkg_config::Config::new().probe("alsa").is_ok();
        if alsa {
            build.file("runtime/moo_capture_alsa.c");
            build.define("MOO_HAS_ALSA", None);
            println!("cargo:rustc-cfg=moo_has_alsa");
        } else {
            build.file("runtime/moo_capture_audio_stub.c");
            println!("cargo:warning=moo capture: natives Mikrofon-Backend fuer dieses Ziel nicht verfuegbar; Stub wird gebaut");
        }
    }

    build.compile("moo_runtime");

    // ========================================================================
    // Core Links (alle Plattformen)
    // ========================================================================
    // P013: vcpkg (x64-windows) nennt die curl-Import-Lib `libcurl.lib` —
    // `curl.lib` existiert dort nicht (LNK1181). sqlite3.lib heisst gleich.
    if target_windows {
        println!("cargo:rustc-link-lib=libcurl");
    } else {
        println!("cargo:rustc-link-lib=curl");
    }
    println!("cargo:rustc-link-lib=sqlite3");

    // P013: Winsock2 fuer moo_net.c/moo_web.c + bcrypt fuer moo_crypto.c
    // (BCryptGenRandom) — nur Windows.
    if target_windows {
        println!("cargo:rustc-link-lib=ws2_32");
        println!("cargo:rustc-link-lib=bcrypt");
        // C2-WIN: Media Foundation + WASAPI/COM.
        for lib in ["mfplat", "mf", "mfreadwrite", "mfuuid", "ole32", "oleaut32", "uuid"] {
            println!("cargo:rustc-link-lib={lib}");
        }
    }

    // SDL2 wird nur von den feature-gated 3D/Grafik-Quellen referenziert.
    // Windows (P013): nur bei aktivem 3D-Feature linken, sonst braeuchte
    // JEDER Build (auch UI-/stdlib-only) installierte SDL2-Libs.
    // Nicht-Windows: unveraendertes Bestandsverhalten (immer linken).
    let any_3d = cfg!(any(feature = "gl21", feature = "gl33", feature = "vulkan"));
    if !target_windows || any_3d {
        println!("cargo:rustc-link-lib=SDL2");
        println!("cargo:rustc-link-lib=SDL2_image");
    }

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
    // gdk-pixbuf wird direkt von moo_ui_gtk.c (Snapshot-API, Plan-004 P2)
    // benoetigt; gtk+-3.0 zieht es transitiv, der Linker will es explizit.
    let _pixbuf = pkg_config::Config::new().probe("gdk-pixbuf-2.0");

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
