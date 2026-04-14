fn main() {
    let mut build = cc::Build::new();
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
        .flag("-fPIC");

    // 3D Backends: bedingt in denselben Build einfuegen
    #[cfg(feature = "gl21")]
    {
        build.file("runtime/moo_3d_gl21.c");
        build.define("MOO_HAS_GL21", None);
    }

    #[cfg(feature = "gl33")]
    {
        build.file("runtime/moo_3d_gl33.c");
        build.file("runtime/moo_3d_gl33_mesh.c");
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

    // Link libcurl for HTTP support
    println!("cargo:rustc-link-lib=curl");

    // Link libsqlite3 for database support
    println!("cargo:rustc-link-lib=sqlite3");

    // Link SDL2 + SDL2_image for graphics + sprites
    println!("cargo:rustc-link-lib=SDL2");
    println!("cargo:rustc-link-lib=SDL2_image");

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
    println!("cargo:rerun-if-changed=runtime/moo_3d_vulkan_vert_spv.h");
    println!("cargo:rerun-if-changed=runtime/moo_3d_vulkan_frag_spv.h");
}
