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
    // Stdlib
    // String extras
    pub moo_string_upper: FunctionValue<'ctx>,
    pub moo_string_lower: FunctionValue<'ctx>,
    pub moo_string_trim: FunctionValue<'ctx>,
    pub moo_string_split: FunctionValue<'ctx>,
    pub moo_string_replace: FunctionValue<'ctx>,
    pub moo_string_contains: FunctionValue<'ctx>,
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

            // String extras
            moo_string_upper: decl_mv_mv!("moo_string_upper", mv1),
            moo_string_lower: decl_mv_mv!("moo_string_lower", mv1),
            moo_string_trim: decl_mv_mv!("moo_string_trim", mv1),
            moo_string_split: decl_mv_mv!("moo_string_split", mv2),
            moo_string_replace: decl_mv_mv!("moo_string_replace", &[mv, mv, mv]),
            moo_string_contains: decl_mv_mv!("moo_string_contains", mv2),
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
        }
    }
}
