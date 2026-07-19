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
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_else(|_| {
        if cfg!(target_os = "windows") { "windows".into() }
        else if cfg!(target_os = "macos") { "macos".into() }
        else { "linux".into() }
    });
    let target_windows = target_os == "windows";
    let target_macos = target_os == "macos";

    // ========================================================================
    // Kern-Runtime (OS-neutral)
    // ========================================================================
    build
        .file("runtime/moo_value.c")
        .file("runtime/moo_memory.c")
        .file("runtime/moo_frame.c")
        .file("runtime/moo_surface_core.c") // P016-O1: freestanding RGBA8-Rasterkern
        .file("runtime/moo_compositor_core.c")   // P016-O3: allocatorfreier Multi-Client-Zustandskern
        .file("runtime/moo_compositor_raster.c") // P016-O3: deterministische RGBA-Komposition
        .file("runtime/moo_compositor_effects_state.c")  // P016-I1: Effects-State und Validierung
        .file("runtime/moo_compositor_animation.c")      // P016-I1: deterministische Animationen
        .file("runtime/moo_compositor_effects_math.c")   // P016-I1: portable Effekt-Mathematik
        .file("runtime/moo_compositor_effects_cpu.c")    // P016-I1: CPU-Referenzrenderer
        .file("runtime/moo_compositor_effects_damage.c") // P016-I1: konservative Damage-Berechnung
        .file("runtime/moo_compositor_effects_gpu.c")    // P016-I1: GPU-Vertragsadapter
        .file("runtime/moo_surface.c")      // P016-O1: refcounteter MOO_SURFACE-Wrapper
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
        .file("runtime/moo_quant.c")        // KI-Q1: Hadamard-Rotation (Registry-Op) + QJL Sign-JL (inferenz-only)
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
        .file("runtime/moo_tls.c")          // TLS-Client backend-agnostische Builtins (Handle-Tabelle, immer)
        .file("runtime/moo_web.c")
        .file("runtime/moo_eval.c")
        .file("runtime/moo_profiler.c")
        .include("runtime")
        .opt_level(2)
        .flag_if_supported("-fPIC")
        .define("MOO_HAS_SURFACE", None);

    // ========================================================================
    // P013: POSIX-Regex-Vendoring fuer Windows (musl v1.2.5 / TRE) — siehe
    // runtime/win_regex/README.md. moo_regex.c schaltet per #ifdef _WIN32 auf
    // diesen Header um. Unter Linux NICHT bauen: dort liefert die System-libc
    // regcomp/regexec (Doppel-Definition vermeiden).
    // ========================================================================
    if target_windows {
        // MSVC markiert portable ISO-/POSIX-Namen wie fopen/getenv/strdup
        // standardmaessig als veraltet. Moo nutzt diese APIs bewusst portabel;
        // die Defines verhindern Warnrauschen ohne die Semantik zu aendern.
        build.define("_CRT_SECURE_NO_WARNINGS", None);
        build.define("_CRT_NONSTDC_NO_DEPRECATE", None);
        build
            .file("runtime/win_regex/regcomp.c")
            .file("runtime/win_regex/regexec.c")
            .file("runtime/win_regex/regerror.c")
            .file("runtime/win_regex/tre-mem.c")
            .include("runtime/win_regex");
    }

    // 3D/Game Runtime-Core nur bauen, wenn ein 3D-Feature aktiv ist.
    // Sonst ziehen UI-only Builds unnötig SDL/GL/Vulkan/GLFW-Symbole in
    // libmoo_runtime.a und gelinkte .moos-Programme scheitern trotz
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

    // Das kompatible ui_moo-Umbrella-Modul enthaelt den Frame-Adapter auch
    // in UI-only Builds. Ohne 3D-Feature stellt dieser kleine Provider die
    // drei benoetigten ABI-Symbole explizit fail-closed bereit, ohne SDL
    // einzubinden. Bei aktivem 3D-Feature liefert moo_graphics.c exklusiv
    // die realen Implementierungen.
    if ui_enabled && !cfg!(any(feature = "gl21", feature = "gl33", feature = "vulkan")) {
        build.file("runtime/moo_graphics_unavailable.c");
    }

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
    println!("cargo:rustc-check-cfg=cfg(moo_has_macos_capture)");
    let target_linux = std::env::var("CARGO_CFG_TARGET_OS")
        .map(|s| s == "linux").unwrap_or(cfg!(target_os = "linux"));
    if target_windows {
        build
            .file("runtime/moo_capture_pull.c")
            .file("runtime/moo_capture_windows_system.c")
            .define("MOO_HAS_WINDOWS_CAPTURE", None);
        println!("cargo:rustc-cfg=moo_has_windows_capture");
    } else if target_macos {
        build
            .file("runtime/moo_capture_pull.c")
            .file("runtime/moo_capture_macos_system.m")
            .flag("-fobjc-arc")
            .define("MOO_HAS_MACOS_CAPTURE", None);
        println!("cargo:rustc-cfg=moo_has_macos_capture");
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

    // TLS-Backend-Auswahl (Dual-Path):
    // Windows: SChannel als nativer Default, mbedTLS als vendored Alternative.
    // Nicht-Windows: OpenSSL als nativer Default, mbedTLS als vendored Alternative.
    println!("cargo:rustc-check-cfg=cfg(moo_tls_mbedtls)");
    println!("cargo:rustc-check-cfg=cfg(moo_tls_schannel)");
    println!("cargo:rerun-if-env-changed=MOO_TLS_BACKEND");
    let tls_backend = std::env::var("MOO_TLS_BACKEND").ok();
    match (target_windows, tls_backend.as_deref()) {
        (_, Some("mbedtls")) => {
            build.file("runtime/moo_tls_mbedtls.c");
            build.include("runtime/mbedtls/include");
            build.include("runtime/mbedtls/library");
            let mbedtls_lib = std::fs::read_dir("runtime/mbedtls/library")
                .expect("runtime/mbedtls/library fehlt — vendored mbedTLS nicht vorhanden");
            for entry in mbedtls_lib {
                let p = entry.expect("mbedtls read_dir").path();
                if p.extension().and_then(|s| s.to_str()) == Some("c") {
                    build.file(&p);
                }
            }
            println!("cargo:rustc-cfg=moo_tls_mbedtls");
            println!("cargo:rerun-if-changed=runtime/mbedtls/library");
        }
        (true, None) | (true, Some("schannel")) => {
            build.file("runtime/moo_tls_schannel.c");
            println!("cargo:rustc-cfg=moo_tls_schannel");
            println!("cargo:rerun-if-changed=runtime/moo_tls_schannel.c");
        }
        (false, None) | (false, Some("openssl")) => {
            build.file("runtime/moo_tls_openssl.c");
        }
        (true, Some("openssl")) => {
            panic!("MOO_TLS_BACKEND=openssl ist fuer Windows nicht verdrahtet; schannel oder mbedtls verwenden");
        }
        (false, Some("schannel")) => {
            panic!("MOO_TLS_BACKEND=schannel ist nur auf Windows verfuegbar");
        }
        (_, Some(other)) => {
            panic!("Unbekanntes MOO_TLS_BACKEND={other}; erlaubt: openssl, mbedtls, schannel");
        }
    }

    // AES-Backend-Auswahl (Dual-Path): MOO_AES_BACKEND=self (Default, hand-rolled
    // AES-256-CTR+HMAC in moo_crypto.c) | openssl (EVP, AES-NI). Beide erzeugen ein
    // byte-identisches Container-Format (IV||CTR-ct||HMAC) und sind interoperabel.
    // Bei openssl blendet MOO_AES_NATIVE die self-Impl in moo_crypto.c aus.
    println!("cargo:rustc-check-cfg=cfg(moo_aes_native)");
    println!("cargo:rerun-if-env-changed=MOO_AES_BACKEND");
    if std::env::var("MOO_AES_BACKEND").as_deref() == Ok("openssl") {
        build.file("runtime/moo_aes_native.c");
        build.define("MOO_AES_NATIVE", None);
        println!("cargo:rustc-cfg=moo_aes_native");
    }

    build.compile("moo_runtime");

    // ========================================================================
    // Core Links (alle Plattformen)
    // ========================================================================
    // HTTP/HTTPS ist self-contained in moo_http.c und nutzt moo_net + moo_tls.
    // libcurl ist deshalb keine Build- oder Laufzeitabhaengigkeit mehr.
    println!("cargo:rustc-link-lib=sqlite3");

    // P013: Winsock2 fuer moo_net.c/moo_web.c + bcrypt fuer moo_crypto.c
    // (BCryptGenRandom) — nur Windows.
    if target_macos {
        for framework in ["AVFoundation", "CoreMedia", "CoreVideo", "CoreAudio", "AudioToolbox", "Foundation"] {
            println!("cargo:rustc-link-lib=framework={framework}");
        }
        println!("cargo:rustc-link-lib=objc");
    }
    if target_windows {
        println!("cargo:rustc-link-lib=ws2_32");
        println!("cargo:rustc-link-lib=bcrypt");
        println!("cargo:rustc-link-lib=crypt32");
        println!("cargo:rustc-link-lib=secur32");
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

    // 3D Backend: bedingt und mit plattformspezifischen Linknamen linken.
    // MSVC/vcpkg (dynamischer x64-windows-Triplet): opengl32.lib,
    // glfw3dll.lib und vulkan-1.lib. Linux: GL/glfw/vulkan.
    // macOS stellt OpenGL als Systemframework bereit; GLFW/Vulkan kommen aus Homebrew.
    #[cfg(feature = "gl21")]
    {
        if target_macos {
            println!("cargo:rustc-link-lib=framework=OpenGL");
        } else {
            println!("cargo:rustc-link-lib={}", if target_windows { "opengl32" } else { "GL" });
        }
        println!("cargo:rustc-link-lib={}", if target_windows { "glfw3dll" } else { "glfw" });
    }
    #[cfg(feature = "gl33")]
    {
        if target_macos {
            println!("cargo:rustc-link-lib=framework=OpenGL");
        } else {
            println!("cargo:rustc-link-lib={}", if target_windows { "opengl32" } else { "GL" });
        }
        println!("cargo:rustc-link-lib={}", if target_windows { "glfw3dll" } else { "glfw" });
    }
    #[cfg(feature = "vulkan")]
    {
        println!("cargo:rustc-link-lib={}", if target_windows { "vulkan-1" } else { "vulkan" });
        println!("cargo:rustc-link-lib={}", if target_windows { "glfw3dll" } else { "glfw" });
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
