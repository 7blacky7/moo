; ModuleID = 'mini'
source_filename = "mini"

declare { i64, i64 } @moo_number(double)

declare { i64, i64 } @moo_string_new(ptr)

declare { i64, i64 } @moo_bool(i1)

declare { i64, i64 } @moo_none()

declare { i64, i64 } @moo_list_new(i32)

declare { i64, i64 } @moo_dict_new()

declare { i64, i64 } @moo_object_new(ptr)

declare { i64, i64 } @moo_error(ptr)

declare { i64, i64 } @moo_add({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_sub({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_mul({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_div({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_mod({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_pow({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_neg({ i64, i64 })

declare { i64, i64 } @moo_eq({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_neq({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_lt({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_gt({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_lte({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_gte({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_and({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_or({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_not({ i64, i64 })

declare { i64, i64 } @moo_bitand({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_bitor({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_bitxor({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_bitnot({ i64, i64 })

declare { i64, i64 } @moo_lshift({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_rshift({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_mem_read({ i64, i64 }, { i64, i64 })

declare void @moo_mem_write({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_string_concat({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_string_length({ i64, i64 })

declare { i64, i64 } @moo_string_index({ i64, i64 }, { i64, i64 })

declare void @moo_list_append({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_list_get({ i64, i64 }, { i64, i64 })

declare void @moo_list_set({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_list_length({ i64, i64 })

declare { i64, i64 } @moo_list_pop({ i64, i64 })

declare { i64, i64 } @moo_list_sort({ i64, i64 })

declare { i64, i64 } @moo_list_reverse({ i64, i64 })

declare { i64, i64 } @moo_list_join({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_list_contains({ i64, i64 }, { i64, i64 })

declare i32 @moo_list_iter_len({ i64, i64 })

declare { i64, i64 } @moo_list_iter_get({ i64, i64 }, i32)

declare { i64, i64 } @moo_dict_get({ i64, i64 }, { i64, i64 })

declare void @moo_dict_set({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_dict_has({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_dict_keys({ i64, i64 })

declare { i64, i64 } @moo_object_get({ i64, i64 }, ptr)

declare void @moo_object_set({ i64, i64 }, ptr, { i64, i64 })

declare void @moo_object_set_parent({ i64, i64 }, { i64, i64 })

declare void @moo_event_on({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_event_emit({ i64, i64 }, { i64, i64 })

declare void @moo_print({ i64, i64 })

declare { i64, i64 } @moo_to_string({ i64, i64 })

declare void @moo_throw({ i64, i64 })

declare void @moo_try_enter()

declare i32 @moo_try_check()

declare void @moo_try_leave()

declare { i64, i64 } @moo_get_error()

declare i1 @moo_is_truthy({ i64, i64 })

declare i1 @moo_is_none({ i64, i64 })

declare void @moo_retain({ i64, i64 })

declare void @moo_release({ i64, i64 })

declare { i64, i64 } @moo_string_upper({ i64, i64 })

declare { i64, i64 } @moo_string_lower({ i64, i64 })

declare { i64, i64 } @moo_string_trim({ i64, i64 })

declare { i64, i64 } @moo_string_split({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_string_replace({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_string_contains({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_string_slice({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_abs({ i64, i64 })

declare { i64, i64 } @moo_sqrt({ i64, i64 })

declare { i64, i64 } @moo_round({ i64, i64 })

declare { i64, i64 } @moo_floor({ i64, i64 })

declare { i64, i64 } @moo_ceil({ i64, i64 })

declare { i64, i64 } @moo_min({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_max({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_random()

declare { i64, i64 } @moo_type_of({ i64, i64 })

declare { i64, i64 } @moo_input({ i64, i64 })

declare { i64, i64 } @moo_length({ i64, i64 })

declare { i64, i64 } @moo_range({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_time()

declare { i64, i64 } @moo_syscall({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_breakpoint({ i64, i64 })

declare { i64, i64 } @moo_index_get({ i64, i64 }, { i64, i64 })

declare void @moo_index_set({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_freeze({ i64, i64 })

declare { i64, i64 } @moo_is_frozen({ i64, i64 })

declare { i64, i64 } @moo_file_read({ i64, i64 })

declare { i64, i64 } @moo_file_write({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_file_append({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_file_lines({ i64, i64 })

declare { i64, i64 } @moo_file_exists({ i64, i64 })

declare { i64, i64 } @moo_file_delete({ i64, i64 })

declare { i64, i64 } @moo_dir_list({ i64, i64 })

declare { i64, i64 } @moo_thread_spawn({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_thread_wait({ i64, i64 })

declare { i64, i64 } @moo_thread_done({ i64, i64 })

declare { i64, i64 } @moo_channel_new({ i64, i64 })

declare void @moo_channel_send({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_channel_recv({ i64, i64 })

declare void @moo_channel_close({ i64, i64 })

declare { i64, i64 } @moo_json_parse({ i64, i64 })

declare { i64, i64 } @moo_json_string({ i64, i64 })

declare { i64, i64 } @moo_http_get({ i64, i64 })

declare { i64, i64 } @moo_http_post({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_sha256({ i64, i64 })

declare { i64, i64 } @moo_secure_random({ i64, i64 })

declare { i64, i64 } @moo_base64_encode({ i64, i64 })

declare { i64, i64 } @moo_base64_decode({ i64, i64 })

declare { i64, i64 } @moo_sanitize_html({ i64, i64 })

declare { i64, i64 } @moo_sanitize_sql({ i64, i64 })

declare { i64, i64 } @moo_db_connect({ i64, i64 })

declare { i64, i64 } @moo_db_execute({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_db_query({ i64, i64 }, { i64, i64 })

declare void @moo_db_close({ i64, i64 })

declare { i64, i64 } @moo_3d_key_pressed({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_result_ok({ i64, i64 })

declare { i64, i64 } @moo_result_err({ i64, i64 })

declare { i64, i64 } @moo_result_is_ok({ i64, i64 })

declare { i64, i64 } @moo_result_is_err({ i64, i64 })

declare { i64, i64 } @moo_result_unwrap({ i64, i64 })

declare { i64, i64 } @moo_window_create({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_window_clear({ i64, i64 }, { i64, i64 })

declare void @moo_window_update({ i64, i64 })

declare { i64, i64 } @moo_window_is_open({ i64, i64 })

declare void @moo_window_close({ i64, i64 })

declare void @moo_draw_rect({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_draw_circle({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_draw_line({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_draw_pixel({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_key_pressed({ i64, i64 })

declare { i64, i64 } @moo_mouse_x({ i64, i64 })

declare { i64, i64 } @moo_mouse_y({ i64, i64 })

declare { i64, i64 } @moo_mouse_pressed({ i64, i64 })

declare void @moo_delay({ i64, i64 })

declare { i64, i64 } @moo_3d_create({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_3d_is_open({ i64, i64 })

declare void @moo_3d_clear({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_3d_update({ i64, i64 })

declare void @moo_3d_close({ i64, i64 })

declare void @moo_3d_perspective({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_3d_camera({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_3d_rotate({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_3d_translate({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_3d_push({ i64, i64 })

declare void @moo_3d_pop({ i64, i64 })

declare void @moo_3d_triangle({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_3d_cube({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare void @moo_3d_sphere({ i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_regex_new({ i64, i64 })

declare { i64, i64 } @moo_regex_match({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_regex_find({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_regex_find_all({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_regex_replace({ i64, i64 }, { i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_tcp_server({ i64, i64 })

declare { i64, i64 } @moo_tcp_connect({ i64, i64 }, { i64, i64 })

declare { i64, i64 } @moo_udp_socket({ i64, i64 })

declare { i64, i64 } @moo_socket_accept({ i64, i64 })

declare { i64, i64 } @moo_socket_read({ i64, i64 }, { i64, i64 })

declare void @moo_socket_write({ i64, i64 }, { i64, i64 })

declare void @moo_socket_close({ i64, i64 })

declare void @moo_profile_enter({ i64, i64 })

declare void @moo_profile_exit({ i64, i64 })

declare void @moo_profile_report()

define { i64, i64 } @quadrat({ i64, i64 } %0) {
entry:
  %x = alloca { i64, i64 }, align 8
  store { i64, i64 } %0, ptr %x, align 4
  %x1 = load { i64, i64 }, ptr %x, align 4
  %x2 = load { i64, i64 }, ptr %x, align 4
  %op = call { i64, i64 } @moo_mul({ i64, i64 } %x1, { i64, i64 } %x2)
  ret { i64, i64 } %op
}

define i32 @main() {
entry:
  ret i32 0
}
