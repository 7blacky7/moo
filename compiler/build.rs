fn main() {
    cc::Build::new()
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
        .include("runtime")
        .opt_level(2)
        .flag("-fPIC")
        .compile("moo_runtime");

    // Link libcurl for HTTP support
    println!("cargo:rustc-link-lib=curl");

    println!("cargo:rerun-if-changed=runtime/");
}
