/// LLVM-Deklarationen aller C-Runtime-Funktionen.
/// Jede Funktion aus moo_runtime.h wird hier als LLVM-Funktion deklariert.

use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::types::{BasicMetadataTypeEnum, BasicTypeEnum, StructType};
use inkwell::values::FunctionValue;
use inkwell::AddressSpace;

pub struct RuntimeBindings<'ctx> {
    pub moo_value_type: StructType<'ctx>,
    // Konstruktoren
    pub moo_number: FunctionValue<'ctx>,
    pub moo_string_new: FunctionValue<'ctx>,
    pub moo_bool_fn: FunctionValue<'ctx>,
    pub moo_none: FunctionValue<'ctx>,
    pub moo_list_new: FunctionValue<'ctx>,
    pub moo_dict_new: FunctionValue<'ctx>,
    pub moo_object_new: FunctionValue<'ctx>,
    pub moo_error: FunctionValue<'ctx>,
    // Ops
    pub moo_add: FunctionValue<'ctx>,
    pub moo_sub: FunctionValue<'ctx>,
    pub moo_mul: FunctionValue<'ctx>,
    pub moo_div: FunctionValue<'ctx>,
    pub moo_mod: FunctionValue<'ctx>,
    pub moo_pow: FunctionValue<'ctx>,
    pub moo_neg: FunctionValue<'ctx>,
    pub moo_eq: FunctionValue<'ctx>,
    pub moo_neq: FunctionValue<'ctx>,
    pub moo_lt: FunctionValue<'ctx>,
    pub moo_gt: FunctionValue<'ctx>,
    pub moo_lte: FunctionValue<'ctx>,
    pub moo_gte: FunctionValue<'ctx>,
    pub moo_and: FunctionValue<'ctx>,
    pub moo_or: FunctionValue<'ctx>,
    pub moo_not: FunctionValue<'ctx>,
    // Bitwise
    pub moo_bitand: FunctionValue<'ctx>,
    pub moo_bitor: FunctionValue<'ctx>,
    pub moo_bitxor: FunctionValue<'ctx>,
    pub moo_bitnot: FunctionValue<'ctx>,
    pub moo_lshift: FunctionValue<'ctx>,
    pub moo_rshift: FunctionValue<'ctx>,
    // Memory
    pub moo_mem_read: FunctionValue<'ctx>,
    pub moo_mem_write: FunctionValue<'ctx>,
    // String
    pub moo_string_concat: FunctionValue<'ctx>,
    pub moo_string_length: FunctionValue<'ctx>,
    pub moo_string_index: FunctionValue<'ctx>,
    // List
    pub moo_list_append: FunctionValue<'ctx>,
    pub moo_list_get: FunctionValue<'ctx>,
    pub moo_list_set: FunctionValue<'ctx>,
    pub moo_list_length: FunctionValue<'ctx>,
    pub moo_list_pop: FunctionValue<'ctx>,
    pub moo_list_sort: FunctionValue<'ctx>,
    pub moo_list_reverse: FunctionValue<'ctx>,
    pub moo_list_join: FunctionValue<'ctx>,
    pub moo_list_contains: FunctionValue<'ctx>,
    pub moo_list_iter_len: FunctionValue<'ctx>,
    pub moo_list_iter_get: FunctionValue<'ctx>,
    // Dict
    pub moo_dict_get: FunctionValue<'ctx>,
    pub moo_dict_set: FunctionValue<'ctx>,
    pub moo_dict_has: FunctionValue<'ctx>,
    pub moo_dict_keys: FunctionValue<'ctx>,
    // Object
    pub moo_object_get: FunctionValue<'ctx>,
    pub moo_object_set: FunctionValue<'ctx>,
    pub moo_object_set_parent: FunctionValue<'ctx>,
    // Events
    pub moo_event_on: FunctionValue<'ctx>,
    pub moo_event_emit: FunctionValue<'ctx>,
    // Print & convert
    pub moo_print: FunctionValue<'ctx>,
    pub moo_to_string: FunctionValue<'ctx>,
    // Error & truthiness
    pub moo_throw: FunctionValue<'ctx>,
    pub moo_try_enter: FunctionValue<'ctx>,
    pub moo_try_check: FunctionValue<'ctx>,
    pub moo_try_leave: FunctionValue<'ctx>,
    pub moo_get_error: FunctionValue<'ctx>,
    pub moo_is_truthy: FunctionValue<'ctx>,
    pub moo_is_none: FunctionValue<'ctx>,
    // Reference Counting
    pub moo_retain: FunctionValue<'ctx>,
    pub moo_release: FunctionValue<'ctx>,
    // Stdlib
    // String extras
    pub moo_string_upper: FunctionValue<'ctx>,
    pub moo_string_lower: FunctionValue<'ctx>,
    pub moo_string_trim: FunctionValue<'ctx>,
    pub moo_string_split: FunctionValue<'ctx>,
    pub moo_string_replace: FunctionValue<'ctx>,
    pub moo_string_contains: FunctionValue<'ctx>,
    pub moo_string_slice: FunctionValue<'ctx>,
    // Stdlib
    pub moo_abs: FunctionValue<'ctx>,
    pub moo_sqrt: FunctionValue<'ctx>,
    pub moo_round: FunctionValue<'ctx>,
    pub moo_floor: FunctionValue<'ctx>,
    pub moo_ceil: FunctionValue<'ctx>,
    pub moo_min: FunctionValue<'ctx>,
    pub moo_max: FunctionValue<'ctx>,
    pub moo_random: FunctionValue<'ctx>,
    pub moo_type_of: FunctionValue<'ctx>,
    pub moo_input: FunctionValue<'ctx>,
    pub moo_length: FunctionValue<'ctx>,
    pub moo_range: FunctionValue<'ctx>,
    pub moo_time: FunctionValue<'ctx>,
    pub moo_syscall: FunctionValue<'ctx>,
    pub moo_breakpoint: FunctionValue<'ctx>,
    pub moo_index_get: FunctionValue<'ctx>,
    pub moo_index_set: FunctionValue<'ctx>,
    // Freeze/Immutable
    pub moo_freeze: FunctionValue<'ctx>,
    pub moo_is_frozen: FunctionValue<'ctx>,
    // File I/O
    pub moo_file_read: FunctionValue<'ctx>,
    pub moo_file_write: FunctionValue<'ctx>,
    pub moo_file_append: FunctionValue<'ctx>,
    pub moo_file_lines: FunctionValue<'ctx>,
    pub moo_file_exists: FunctionValue<'ctx>,
    pub moo_file_delete: FunctionValue<'ctx>,
    pub moo_dir_list: FunctionValue<'ctx>,
    // Thread & Channel
    pub moo_thread_spawn: FunctionValue<'ctx>,
    pub moo_thread_wait: FunctionValue<'ctx>,
    pub moo_thread_done: FunctionValue<'ctx>,
    pub moo_channel_new: FunctionValue<'ctx>,
    pub moo_channel_send: FunctionValue<'ctx>,
    pub moo_channel_recv: FunctionValue<'ctx>,
    pub moo_channel_close: FunctionValue<'ctx>,
    // JSON
    pub moo_json_parse: FunctionValue<'ctx>,
    pub moo_json_string: FunctionValue<'ctx>,
    // HTTP
    pub moo_http_get: FunctionValue<'ctx>,
    pub moo_http_post: FunctionValue<'ctx>,
    // Crypto & Security
    pub moo_sha256: FunctionValue<'ctx>,
    pub moo_secure_random: FunctionValue<'ctx>,
    pub moo_base64_encode: FunctionValue<'ctx>,
    pub moo_base64_decode: FunctionValue<'ctx>,
    pub moo_sanitize_html: FunctionValue<'ctx>,
    pub moo_sanitize_sql: FunctionValue<'ctx>,
    // Database
    pub moo_db_connect: FunctionValue<'ctx>,
    pub moo_db_execute: FunctionValue<'ctx>,
    pub moo_db_query: FunctionValue<'ctx>,
    pub moo_db_close: FunctionValue<'ctx>,
    // 3D Input
    pub moo_3d_key_pressed: FunctionValue<'ctx>,
    // Result-Typ
    pub moo_result_ok: FunctionValue<'ctx>,
    pub moo_result_err: FunctionValue<'ctx>,
    pub moo_result_is_ok: FunctionValue<'ctx>,
    pub moo_result_is_err: FunctionValue<'ctx>,
    pub moo_result_unwrap: FunctionValue<'ctx>,
    // Grafik (SDL2)
    pub moo_window_create: FunctionValue<'ctx>,
    pub moo_window_clear: FunctionValue<'ctx>,
    pub moo_window_update: FunctionValue<'ctx>,
    pub moo_window_is_open: FunctionValue<'ctx>,
    pub moo_window_close: FunctionValue<'ctx>,
    pub moo_draw_rect: FunctionValue<'ctx>,
    pub moo_draw_circle: FunctionValue<'ctx>,
    pub moo_draw_line: FunctionValue<'ctx>,
    pub moo_draw_pixel: FunctionValue<'ctx>,
    // Grafik Input (SDL2)
    pub moo_key_pressed: FunctionValue<'ctx>,
    pub moo_mouse_x: FunctionValue<'ctx>,
    pub moo_mouse_y: FunctionValue<'ctx>,
    pub moo_mouse_pressed: FunctionValue<'ctx>,
    pub moo_delay: FunctionValue<'ctx>,
    // 3D Grafik (OpenGL/Software)
    pub moo_3d_create: FunctionValue<'ctx>,
    pub moo_3d_is_open: FunctionValue<'ctx>,
    pub moo_3d_clear: FunctionValue<'ctx>,
    pub moo_3d_update: FunctionValue<'ctx>,
    pub moo_3d_close: FunctionValue<'ctx>,
    pub moo_3d_perspective: FunctionValue<'ctx>,
    pub moo_3d_camera: FunctionValue<'ctx>,
    pub moo_3d_rotate: FunctionValue<'ctx>,
    pub moo_3d_translate: FunctionValue<'ctx>,
    pub moo_3d_push: FunctionValue<'ctx>,
    pub moo_3d_pop: FunctionValue<'ctx>,
    pub moo_3d_triangle: FunctionValue<'ctx>,
    pub moo_3d_cube: FunctionValue<'ctx>,
    pub moo_3d_sphere: FunctionValue<'ctx>,
    // Regex (POSIX)
    pub moo_regex_new: FunctionValue<'ctx>,
    pub moo_regex_match: FunctionValue<'ctx>,
    pub moo_regex_find: FunctionValue<'ctx>,
    pub moo_regex_find_all: FunctionValue<'ctx>,
    pub moo_regex_replace: FunctionValue<'ctx>,
    // Netzwerk (TCP/UDP)
    pub moo_tcp_server: FunctionValue<'ctx>,
    pub moo_tcp_connect: FunctionValue<'ctx>,
    pub moo_udp_socket: FunctionValue<'ctx>,
    pub moo_socket_accept: FunctionValue<'ctx>,
    pub moo_socket_read: FunctionValue<'ctx>,
    pub moo_socket_write: FunctionValue<'ctx>,
    pub moo_socket_close: FunctionValue<'ctx>,
    // Profiler
    pub moo_profile_enter: FunctionValue<'ctx>,
    pub moo_profile_exit: FunctionValue<'ctx>,
    pub moo_profile_report: FunctionValue<'ctx>,
}

impl<'ctx> RuntimeBindings<'ctx> {
    pub fn declare(context: &'ctx Context, module: &Module<'ctx>) -> Self {
        let ptr_type = context.ptr_type(AddressSpace::default());
        let i32_type = context.i32_type();
        let i8_type = context.i8_type();
        let f64_type = context.f64_type();
        let void_type = context.void_type();
        let bool_type = context.bool_type();

        // MooValue = { uint8_t tag, [7 padding], union { double, ptr } data }
        // C-ABI: sizeof = 16, tag at offset 0, data at offset 8
        // LLVM: { i64, double } passt exakt (8 + 8 = 16 Bytes, korrektes Alignment)
        let i64_type = context.i64_type();
        // MooValue = { uint64_t tag, uint64_t data } — exakt 16 Bytes
        let moo_value_type = context.struct_type(
            &[i64_type.into(), i64_type.into()],
            false,
        );

        let mv: BasicMetadataTypeEnum = moo_value_type.into();
        let mv1 = &[mv];
        let mv2 = &[mv, mv];
        let mv3 = &[mv, mv, mv];

        // Makro fuer Deklaration
        macro_rules! decl {
            ($name:expr, ret $ret:expr, $($params:expr),*) => {
                module.add_function($name, $ret.fn_type(&[$($params.into()),*], false), None)
            };
        }

        // MooValue-returning, N MooValue params
        macro_rules! decl_mv_mv {
            ($name:expr, $params:expr) => {
                module.add_function($name, moo_value_type.fn_type($params, false), None)
            };
        }

        Self {
            moo_value_type,

            // Konstruktoren
            moo_number: decl_mv_mv!("moo_number", &[f64_type.into()]),
            moo_string_new: decl_mv_mv!("moo_string_new", &[ptr_type.into()]),
            moo_bool_fn: decl_mv_mv!("moo_bool", &[bool_type.into()]),
            moo_none: decl_mv_mv!("moo_none", &[]),
            moo_list_new: decl_mv_mv!("moo_list_new", &[i32_type.into()]),
            moo_dict_new: decl_mv_mv!("moo_dict_new", &[]),
            moo_object_new: decl_mv_mv!("moo_object_new", &[ptr_type.into()]),
            moo_error: decl_mv_mv!("moo_error", &[ptr_type.into()]),

            // Ops (MooValue, MooValue) -> MooValue
            moo_add: decl_mv_mv!("moo_add", mv2),
            moo_sub: decl_mv_mv!("moo_sub", mv2),
            moo_mul: decl_mv_mv!("moo_mul", mv2),
            moo_div: decl_mv_mv!("moo_div", mv2),
            moo_mod: decl_mv_mv!("moo_mod", mv2),
            moo_pow: decl_mv_mv!("moo_pow", mv2),
            moo_neg: decl_mv_mv!("moo_neg", mv1),
            moo_eq: decl_mv_mv!("moo_eq", mv2),
            moo_neq: decl_mv_mv!("moo_neq", mv2),
            moo_lt: decl_mv_mv!("moo_lt", mv2),
            moo_gt: decl_mv_mv!("moo_gt", mv2),
            moo_lte: decl_mv_mv!("moo_lte", mv2),
            moo_gte: decl_mv_mv!("moo_gte", mv2),
            moo_and: decl_mv_mv!("moo_and", mv2),
            moo_or: decl_mv_mv!("moo_or", mv2),
            moo_not: decl_mv_mv!("moo_not", mv1),

            // Bitwise
            moo_bitand: decl_mv_mv!("moo_bitand", mv2),
            moo_bitor: decl_mv_mv!("moo_bitor", mv2),
            moo_bitxor: decl_mv_mv!("moo_bitxor", mv2),
            moo_bitnot: decl_mv_mv!("moo_bitnot", mv1),
            moo_lshift: decl_mv_mv!("moo_lshift", mv2),
            moo_rshift: decl_mv_mv!("moo_rshift", mv2),
            // Memory
            moo_mem_read: decl_mv_mv!("moo_mem_read", mv2),
            moo_mem_write: module.add_function("moo_mem_write", void_type.fn_type(mv3, false), None),

            // String
            moo_string_concat: decl_mv_mv!("moo_string_concat", mv2),
            moo_string_length: decl_mv_mv!("moo_string_length", mv1),
            moo_string_index: decl_mv_mv!("moo_string_index", mv2),

            // List
            moo_list_append: module.add_function("moo_list_append", void_type.fn_type(mv2, false), None),
            moo_list_get: decl_mv_mv!("moo_list_get", mv2),
            moo_list_set: module.add_function("moo_list_set", void_type.fn_type(mv3, false), None),
            moo_list_length: decl_mv_mv!("moo_list_length", mv1),
            moo_list_pop: decl_mv_mv!("moo_list_pop", mv1),
            moo_list_sort: decl_mv_mv!("moo_list_sort", mv1),
            moo_list_reverse: decl_mv_mv!("moo_list_reverse", mv1),
            moo_list_join: decl_mv_mv!("moo_list_join", mv2),
            moo_list_contains: decl_mv_mv!("moo_list_contains", mv2),
            moo_list_iter_len: module.add_function("moo_list_iter_len", i32_type.fn_type(mv1, false), None),
            moo_list_iter_get: decl_mv_mv!("moo_list_iter_get", &[mv, i32_type.into()]),

            // Dict
            moo_dict_get: decl_mv_mv!("moo_dict_get", mv2),
            moo_dict_set: module.add_function("moo_dict_set", void_type.fn_type(mv3, false), None),
            moo_dict_has: decl_mv_mv!("moo_dict_has", mv2),
            moo_dict_keys: decl_mv_mv!("moo_dict_keys", mv1),

            // Object
            moo_object_get: decl_mv_mv!("moo_object_get", &[mv, ptr_type.into()]),
            moo_object_set: module.add_function("moo_object_set", void_type.fn_type(&[mv, ptr_type.into(), mv], false), None),
            moo_object_set_parent: module.add_function("moo_object_set_parent", void_type.fn_type(mv2, false), None),
            // Events
            moo_event_on: module.add_function("moo_event_on", void_type.fn_type(mv3, false), None),
            moo_event_emit: module.add_function("moo_event_emit", void_type.fn_type(mv2, false), None),

            // Print & convert
            moo_print: module.add_function("moo_print", void_type.fn_type(mv1, false), None),
            moo_to_string: decl_mv_mv!("moo_to_string", mv1),

            // Error & truthiness
            moo_throw: module.add_function("moo_throw", void_type.fn_type(mv1, false), None),
            moo_try_enter: module.add_function("moo_try_enter", void_type.fn_type(&[], false), None),
            moo_try_check: module.add_function("moo_try_check", i32_type.fn_type(&[], false), None),
            moo_try_leave: module.add_function("moo_try_leave", void_type.fn_type(&[], false), None),
            moo_get_error: decl_mv_mv!("moo_get_error", &[]),
            moo_is_truthy: module.add_function("moo_is_truthy", bool_type.fn_type(mv1, false), None),
            moo_is_none: module.add_function("moo_is_none", bool_type.fn_type(mv1, false), None),
            // Reference Counting
            moo_retain: module.add_function("moo_retain", void_type.fn_type(mv1, false), None),
            moo_release: module.add_function("moo_release", void_type.fn_type(mv1, false), None),

            // String extras
            moo_string_upper: decl_mv_mv!("moo_string_upper", mv1),
            moo_string_lower: decl_mv_mv!("moo_string_lower", mv1),
            moo_string_trim: decl_mv_mv!("moo_string_trim", mv1),
            moo_string_split: decl_mv_mv!("moo_string_split", mv2),
            moo_string_replace: decl_mv_mv!("moo_string_replace", &[mv, mv, mv]),
            moo_string_contains: decl_mv_mv!("moo_string_contains", mv2),
            moo_string_slice: decl_mv_mv!("moo_string_slice", mv3),
            // Stdlib
            moo_abs: decl_mv_mv!("moo_abs", mv1),
            moo_sqrt: decl_mv_mv!("moo_sqrt", mv1),
            moo_round: decl_mv_mv!("moo_round", mv1),
            moo_floor: decl_mv_mv!("moo_floor", mv1),
            moo_ceil: decl_mv_mv!("moo_ceil", mv1),
            moo_min: decl_mv_mv!("moo_min", mv2),
            moo_max: decl_mv_mv!("moo_max", mv2),
            moo_random: decl_mv_mv!("moo_random", &[]),
            moo_type_of: decl_mv_mv!("moo_type_of", mv1),
            moo_input: decl_mv_mv!("moo_input", mv1),
            moo_length: decl_mv_mv!("moo_length", mv1),
            moo_range: decl_mv_mv!("moo_range", mv2),
            moo_time: decl_mv_mv!("moo_time", &[]),
            moo_syscall: decl_mv_mv!("moo_syscall", &[mv, mv, mv, mv]),
            moo_breakpoint: module.add_function("moo_breakpoint", void_type.fn_type(mv1, false), None),
            moo_index_get: decl_mv_mv!("moo_index_get", mv2),
            moo_index_set: module.add_function("moo_index_set", void_type.fn_type(mv3, false), None),
            // Freeze/Immutable
            moo_freeze: decl_mv_mv!("moo_freeze", mv1),
            moo_is_frozen: decl_mv_mv!("moo_is_frozen", mv1),
            // File I/O
            moo_file_read: decl_mv_mv!("moo_file_read", mv1),
            moo_file_write: decl_mv_mv!("moo_file_write", mv2),
            moo_file_append: decl_mv_mv!("moo_file_append", mv2),
            moo_file_lines: decl_mv_mv!("moo_file_lines", mv1),
            moo_file_exists: decl_mv_mv!("moo_file_exists", mv1),
            moo_file_delete: decl_mv_mv!("moo_file_delete", mv1),
            moo_dir_list: decl_mv_mv!("moo_dir_list", mv1),
            // Thread & Channel
            moo_thread_spawn: decl_mv_mv!("moo_thread_spawn", mv2),
            moo_thread_wait: decl_mv_mv!("moo_thread_wait", mv1),
            moo_thread_done: decl_mv_mv!("moo_thread_done", mv1),
            moo_channel_new: decl_mv_mv!("moo_channel_new", mv1),
            moo_channel_send: module.add_function("moo_channel_send", void_type.fn_type(mv2, false), None),
            moo_channel_recv: decl_mv_mv!("moo_channel_recv", mv1),
            moo_channel_close: module.add_function("moo_channel_close", void_type.fn_type(mv1, false), None),
            // JSON
            moo_json_parse: decl_mv_mv!("moo_json_parse", mv1),
            moo_json_string: decl_mv_mv!("moo_json_string", mv1),
            // HTTP
            moo_http_get: decl_mv_mv!("moo_http_get", mv1),
            moo_http_post: decl_mv_mv!("moo_http_post", mv2),
            // Crypto & Security
            moo_sha256: decl_mv_mv!("moo_sha256", mv1),
            moo_secure_random: decl_mv_mv!("moo_secure_random", mv1),
            moo_base64_encode: decl_mv_mv!("moo_base64_encode", mv1),
            moo_base64_decode: decl_mv_mv!("moo_base64_decode", mv1),
            moo_sanitize_html: decl_mv_mv!("moo_sanitize_html", mv1),
            moo_sanitize_sql: decl_mv_mv!("moo_sanitize_sql", mv1),
            // Database
            moo_db_connect: decl_mv_mv!("moo_db_connect", mv1),
            moo_db_execute: decl_mv_mv!("moo_db_execute", mv2),
            moo_db_query: decl_mv_mv!("moo_db_query", mv2),
            moo_db_close: module.add_function("moo_db_close", void_type.fn_type(mv1, false), None),
            // Result-Typ
            moo_3d_key_pressed: decl_mv_mv!("moo_3d_key_pressed", mv2),
            moo_result_ok: decl_mv_mv!("moo_result_ok", mv1),
            moo_result_err: decl_mv_mv!("moo_result_err", mv1),
            moo_result_is_ok: decl_mv_mv!("moo_result_is_ok", mv1),
            moo_result_is_err: decl_mv_mv!("moo_result_is_err", mv1),
            moo_result_unwrap: decl_mv_mv!("moo_result_unwrap", mv1),
            // Grafik (SDL2)
            moo_window_create: decl_mv_mv!("moo_window_create", mv3),
            moo_window_clear: module.add_function("moo_window_clear", void_type.fn_type(mv2, false), None),
            moo_window_update: module.add_function("moo_window_update", void_type.fn_type(mv1, false), None),
            moo_window_is_open: decl_mv_mv!("moo_window_is_open", mv1),
            moo_window_close: module.add_function("moo_window_close", void_type.fn_type(mv1, false), None),
            moo_draw_rect: module.add_function("moo_draw_rect", void_type.fn_type(&[mv, mv, mv, mv, mv, mv], false), None),
            moo_draw_circle: module.add_function("moo_draw_circle", void_type.fn_type(&[mv, mv, mv, mv, mv], false), None),
            moo_draw_line: module.add_function("moo_draw_line", void_type.fn_type(&[mv, mv, mv, mv, mv, mv], false), None),
            moo_draw_pixel: module.add_function("moo_draw_pixel", void_type.fn_type(&[mv, mv, mv, mv], false), None),
            // Grafik Input (SDL2)
            moo_key_pressed: decl_mv_mv!("moo_key_pressed", mv1),
            moo_mouse_x: decl_mv_mv!("moo_mouse_x", mv1),
            moo_mouse_y: decl_mv_mv!("moo_mouse_y", mv1),
            moo_mouse_pressed: decl_mv_mv!("moo_mouse_pressed", mv1),
            moo_delay: module.add_function("moo_delay", void_type.fn_type(mv1, false), None),
            // 3D Grafik
            moo_3d_create: decl_mv_mv!("moo_3d_create", mv3),
            moo_3d_is_open: decl_mv_mv!("moo_3d_is_open", mv1),
            moo_3d_clear: module.add_function("moo_3d_clear", void_type.fn_type(&[mv, mv, mv, mv], false), None),
            moo_3d_update: module.add_function("moo_3d_update", void_type.fn_type(mv1, false), None),
            moo_3d_close: module.add_function("moo_3d_close", void_type.fn_type(mv1, false), None),
            moo_3d_perspective: module.add_function("moo_3d_perspective", void_type.fn_type(&[mv, mv, mv, mv], false), None),
            moo_3d_camera: module.add_function("moo_3d_camera", void_type.fn_type(&[mv, mv, mv, mv, mv, mv, mv], false), None),
            moo_3d_rotate: module.add_function("moo_3d_rotate", void_type.fn_type(&[mv, mv, mv, mv, mv], false), None),
            moo_3d_translate: module.add_function("moo_3d_translate", void_type.fn_type(&[mv, mv, mv, mv], false), None),
            moo_3d_push: module.add_function("moo_3d_push", void_type.fn_type(mv1, false), None),
            moo_3d_pop: module.add_function("moo_3d_pop", void_type.fn_type(mv1, false), None),
            moo_3d_triangle: module.add_function("moo_3d_triangle", void_type.fn_type(&[mv, mv, mv, mv, mv, mv, mv, mv, mv, mv, mv], false), None),
            moo_3d_cube: module.add_function("moo_3d_cube", void_type.fn_type(&[mv, mv, mv, mv, mv, mv], false), None),
            moo_3d_sphere: module.add_function("moo_3d_sphere", void_type.fn_type(&[mv, mv, mv, mv, mv, mv, mv], false), None),
            // Regex
            moo_regex_new: decl_mv_mv!("moo_regex_new", mv1),
            moo_regex_match: decl_mv_mv!("moo_regex_match", mv2),
            moo_regex_find: decl_mv_mv!("moo_regex_find", mv2),
            moo_regex_find_all: decl_mv_mv!("moo_regex_find_all", mv2),
            moo_regex_replace: decl_mv_mv!("moo_regex_replace", mv3),
            // Netzwerk
            moo_tcp_server: decl_mv_mv!("moo_tcp_server", mv1),
            moo_tcp_connect: decl_mv_mv!("moo_tcp_connect", mv2),
            moo_udp_socket: decl_mv_mv!("moo_udp_socket", mv1),
            moo_socket_accept: decl_mv_mv!("moo_socket_accept", mv1),
            moo_socket_read: decl_mv_mv!("moo_socket_read", mv2),
            moo_socket_write: module.add_function("moo_socket_write", void_type.fn_type(mv2, false), None),
            moo_socket_close: module.add_function("moo_socket_close", void_type.fn_type(mv1, false), None),
            // Profiler
            moo_profile_enter: module.add_function("moo_profile_enter", void_type.fn_type(mv1, false), None),
            moo_profile_exit: module.add_function("moo_profile_exit", void_type.fn_type(mv1, false), None),
            moo_profile_report: module.add_function("moo_profile_report", void_type.fn_type(&[], false), None),
        }
    }
}
